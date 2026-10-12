/**************************************************************************
 *   This file is part of PRAXIS.                                         *
 *                                                                        *
 *   Author: Ivo Filot <ivo@ivofilot.nl>                                  *
 *                                                                        *
 *   PRAXIS is free software:                                             *
 *   you can redistribute it and/or modify it under the terms of the      *
 *   GNU General Public License as published by the Free Software         *
 *   Foundation, either version 3 of the License, or (at your option)     *
 *   any later version.                                                   *
 *                                                                        *
 *   PRAXIS is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty          *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.              *
 *   See the GNU General Public License for more details.                 *
 *                                                                        *
 *   You should have received a copy of the GNU General Public License    *
 *   along with this program.  If not, see http://www.gnu.org/licenses/.  *
 *                                                                        *
 **************************************************************************/

#include <cmath>
#include <limits>
#include <map>
#include <utility>

#include <QDir>
#include <QComboBox>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QXmlStreamReader>
#include <QtTest>

#include "calculation/job_result.h"
#include "calculation/matrix_export.h"
#include "calculation/job_runner.h"
#include "calculation/population_analysis.h"
#include "calculation/python_environment.h"
#include "config.h"
#include "calculation/job_script_writer.h"
#include "calculation/job_spec.h"
#include "data/molecule.h"
#include "gui/scene.h"
#include "gui/results_panel.h"
#include "orbitals/marching_cubes.h"
#include "orbitals/orbital_builder.h"
#include "orbitals/scalar_field.h"
#include "util/message_log.h"

namespace {

QString data_file(const QString& name) {
    return QDir(TEST_DATA_DIR).filePath(name);
}

Molecule water() {
    return Molecule::from_xyz_string("3\nwater\n"
                                     "O 0.0 0.0 0.1368220\n"
                                     "H 0.0 0.7697660 -0.5472890\n"
                                     "H 0.0 -0.7697660 -0.5472890\n", "water");
}

/**
 * @brief Check that a mesh is closed, correctly oriented and lies on a sphere
 */
void verify_sphere_mesh(const IsoMesh& mesh, float radius, float tol) {
    QVERIFY(!mesh.empty());
    QCOMPARE(mesh.positions.size(), mesh.normals.size());
    QCOMPARE(mesh.indices.size() % 3, (size_t)0);

    for(size_t i = 0; i < mesh.positions.size(); ++i) {
        const glm::vec3& p = mesh.positions[i];
        QVERIFY2(std::abs(glm::length(p) - radius) < tol, "vertex does not lie on sphere");
        QVERIFY2(glm::dot(mesh.normals[i], p) > 0.0f, "normal does not point outward");
        QVERIFY(std::abs(glm::length(mesh.normals[i]) - 1.0f) < 1e-4f);
    }

    // every edge is shared by exactly two triangles with opposite direction
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for(size_t t = 0; t < mesh.indices.size(); t += 3) {
        const uint32_t a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
        const glm::vec3 face = glm::cross(mesh.positions[b] - mesh.positions[a],
                                          mesh.positions[c] - mesh.positions[a]);
        QVERIFY2(glm::dot(face, mesh.positions[a]) > 0.0f, "triangle is wound clockwise");
        edges[{a, b}]++;
        edges[{b, c}]++;
        edges[{c, a}]++;
    }
    for(const auto& e : edges) {
        QVERIFY2(e.second == 1, "directed edge used more than once");
        QVERIFY2(edges.count({e.first.second, e.first.first}) == 1, "mesh is not closed");
    }
}

} // namespace

class TestCore : public QObject {
    Q_OBJECT

private slots:
    void matrix_export_delimited_data() {
        QTest::addColumn<int>("format");
        QTest::addColumn<QChar>("separator");
        QTest::newRow("csv") << int(MatrixExportFormat::Csv) << QChar(',');
        QTest::newRow("text") << int(MatrixExportFormat::Text) << QChar('\t');
    }

    void matrix_export_delimited() {
        QFETCH(int, format);
        QFETCH(QChar, separator);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DenseMatrix matrix;
        matrix.rows = 2;
        matrix.cols = 3;
        matrix.data = {1.2345678901234567, -2.5, 0.0, 1e-200, 1e200, -0.0000123456789};
        const QString path = dir.filePath("matrix");
        const QLocale original = QLocale();
        QLocale::setDefault(QLocale(QLocale::Dutch, QLocale::Netherlands));
        export_matrix(matrix, path, MatrixExportFormat(format));
        QLocale::setDefault(original);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows = QString::fromUtf8(file.readAll()).trimmed().split('\n');
        QCOMPARE(rows.size(), matrix.rows);
        for(int row = 0; row < matrix.rows; ++row) {
            const auto columns = rows[row].split(separator);
            QCOMPARE(columns.size(), matrix.cols);
            for(int col = 0; col < matrix.cols; ++col) {
                bool ok;
                const double value = columns[col].toDouble(&ok);
                QVERIFY(ok);
                QCOMPARE(value, matrix(row, col));
            }
        }
    }

    void matrix_export_excel() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DenseMatrix matrix;
        matrix.rows = 2;
        matrix.cols = 28; // exercise column references beyond Z
        for(int i = 0; i < matrix.rows * matrix.cols; ++i) {
            matrix.data.push_back((i - 30) * 1.2345678901234567);
        }
        const QString path = dir.filePath("matrix.xlsx");
        export_matrix(matrix, path, MatrixExportFormat::Excel);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QDataStream zip(&file);
        zip.setByteOrder(QDataStream::LittleEndian);
        QMap<QByteArray, QByteArray> parts;
        while(!file.atEnd()) {
            quint32 signature;
            zip >> signature;
            if(signature == 0x02014b50u) {
                break;
            }
            QCOMPARE(signature, 0x04034b50u);
            quint16 version, flags, compression, time, date, name_size, extra_size;
            quint32 crc, compressed_size, size;
            zip >> version >> flags >> compression >> time >> date >> crc
                >> compressed_size >> size >> name_size >> extra_size;
            QCOMPARE(compression, quint16(0));
            QCOMPARE(compressed_size, size);
            const QByteArray name = file.read(name_size);
            file.read(extra_size);
            const QByteArray data = file.read(size);
            QCOMPARE(data.size(), qsizetype(size));
            parts.insert(name, data);
        }
        QCOMPARE(parts.size(), 5);
        QVERIFY(parts.contains("[Content_Types].xml"));
        QVERIFY(parts.contains("_rels/.rels"));
        QVERIFY(parts.contains("xl/workbook.xml"));
        QVERIFY(parts.contains("xl/_rels/workbook.xml.rels"));
        QXmlStreamReader xml(parts.value("xl/worksheets/sheet1.xml"));
        QStringList references;
        std::vector<double> values;
        while(!xml.atEnd()) {
            xml.readNext();
            if(xml.isStartElement() && xml.name() == QStringLiteral("c")) {
                references << xml.attributes().value("r").toString();
            } else if(xml.isStartElement() && xml.name() == QStringLiteral("v")) {
                bool ok;
                values.push_back(xml.readElementText().toDouble(&ok));
                QVERIFY(ok);
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(values == matrix.data);
        QCOMPARE(references[26], QString("AA1"));
        QCOMPARE(references.back(), QString("AB2"));
    }

    void matrix_export_errors() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("existing.csv");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("keep me");
        file.close();
        DenseMatrix matrix;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, export_matrix(matrix, path, MatrixExportFormat::Csv));
        matrix.rows = 1;
        matrix.cols = 2;
        matrix.data = {1.0};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, export_matrix(matrix, path, MatrixExportFormat::Csv));
        matrix.data = {1.0, std::numeric_limits<double>::infinity()};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, export_matrix(matrix, path, MatrixExportFormat::Excel));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("keep me"));
        file.close();
        matrix.data = {1.0, 2.0};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                export_matrix(matrix, dir.filePath("absent/matrix.csv"), MatrixExportFormat::Csv));
    }

    void matrix_export_availability() {
        ResultsPanel panel;
        auto* button = panel.findChild<QPushButton*>("export_matrix_button");
        auto* selector = panel.findChild<QComboBox*>("matrix_selector");
        QVERIFY(button);
        QVERIFY(selector);
        QVERIFY(!button->isEnabled());
        auto result = JobResult::load(QString(TEST_DATA_DIR) + "/h2o_rhf_fb.json");
        panel.set_result(result, QString());
        QCOMPARE(selector->count(), int(result->matrices.size()));
        for(int i = 0; i < selector->count(); ++i) {
            selector->setCurrentIndex(i);
            QVERIFY(button->isEnabled());
        }
        selector->setCurrentIndex(-1);
        QVERIFY(!button->isEnabled());
        panel.clear();
        QVERIFY(!button->isEnabled());
    }

    void coefficient_matrices_data() {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<bool>("rectangular");
        QTest::newRow("canonical-and-localized") << QString("h2o_rhf_fb.json") << false;
        QTest::newRow("alpha-and-beta") << QString("o2_uhf.json") << false;
        QTest::newRow("dft") << QString("h2o_svwn5.json") << false;
        QTest::newRow("rectangular") << QString("h2o_rhf_fb.json") << true;
    }

    void coefficient_matrices() {
        QFETCH(QString, filename);
        QFETCH(bool, rectangular);
        QFile input(QString(TEST_DATA_DIR) + "/" + filename);
        QVERIFY(input.open(QIODevice::ReadOnly));
        auto document = QJsonDocument::fromJson(input.readAll());
        if(rectangular) {
            auto root = document.object();
            auto sets = root["orbitals"].toArray();
            for(int i = 0; i < sets.size(); ++i) {
                auto set = sets[i].toObject();
                for(const auto& key : {"energies", "occupations", "coefficients"}) {
                    auto values = set[key].toArray();
                    while(values.size() > 3) {
                        values.removeLast();
                    }
                    set[key] = values;
                }
                sets[i] = set;
            }
            root["orbitals"] = sets;
            document.setObject(root);
        }
        auto result = JobResult::parse(document.toJson());
        ResultsPanel panel;
        panel.set_result(result, QString());
        auto* selector = panel.findChild<QComboBox*>("matrix_selector");
        auto* table = panel.findChild<QTableWidget*>("matrix_table");
        auto* export_button = panel.findChild<QPushButton*>("export_matrix_button");
        QVERIFY(selector);
        QVERIFY(table);
        QVERIFY(export_button);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for(size_t i = 0; i < result->orbital_sets.size(); ++i) {
            const auto& set = result->orbital_sets[i];
            const QString key = QString("coefficients_%1").arg(i);
            const DenseMatrix* matrix = result->find_matrix(key);
            QVERIFY(matrix);
            QCOMPARE(matrix->rows, int(result->basis->size()));
            QCOMPARE(matrix->cols, int(set.size()));
            const int selected = selector->findData(key);
            QVERIFY(selected >= 0);
            QCOMPARE(selector->itemText(selected), QString("Coefficients (%1)").arg(set.label));
            selector->setCurrentIndex(selected);
            QVERIFY(export_button->isEnabled());
            QCOMPARE(table->rowCount(), matrix->rows);
            QCOMPARE(table->columnCount(), matrix->cols);
            for(int row = 0; row < matrix->rows; ++row) {
                QCOMPARE(table->verticalHeaderItem(row)->text(),
                         QString::fromStdString((*result->basis)[row].label));
                for(int col = 0; col < matrix->cols; ++col) {
                    QCOMPARE((*matrix)(row, col), set.coefficients[col][row]);
                    QCOMPARE(table->item(row, col)->data(Qt::UserRole).toDouble(), set.coefficients[col][row]);
                    QCOMPARE(table->horizontalHeaderItem(col)->text(), QString("MO %1").arg(col + 1));
                }
            }
            const QString path = dir.filePath(key + ".csv");
            export_matrix(*matrix, path, MatrixExportFormat::Csv);
            QFile output(path);
            QVERIFY(output.open(QIODevice::ReadOnly));
            const auto rows = QString::fromUtf8(output.readAll()).trimmed().split('\n');
            QCOMPARE(rows.size(), matrix->rows);
            for(int row = 0; row < matrix->rows; ++row) {
                const auto columns = rows[row].split(',');
                QCOMPARE(columns.size(), matrix->cols);
                for(int col = 0; col < matrix->cols; ++col) {
                    QCOMPARE(columns[col].toDouble(), set.coefficients[col][row]);
                }
            }
        }
        selector->setCurrentIndex(selector->findData("overlap"));
        QCOMPARE(table->horizontalHeaderItem(0)->text(),
                 QString::fromStdString((*result->basis)[0].label));
    }

    // ------------------------------------------------------------------
    // molecules
    // ------------------------------------------------------------------
    void molecule_parse_xyz() {
        const Molecule mol = water();
        QCOMPARE(mol.get_name(), QString("water"));
        QCOMPARE(mol.size(), (size_t)3);
        QCOMPARE(mol.formula(), QString("H2O"));
        QCOMPARE(mol.nuclear_charge(), 10);
        QVERIFY(std::abs(mol.get_atoms()[1].position.y - 0.7697660) < 1e-12);
    }

    void molecule_formula_hill() {
        Molecule mol("ethanol");
        mol.add_atom("O", glm::dvec3(0.0));
        mol.add_atom("H", glm::dvec3(0.0));
        mol.add_atom("C", glm::dvec3(0.0));
        mol.add_atom("C", glm::dvec3(0.0));
        QCOMPARE(mol.formula(), QString("C2HO"));
    }

    void molecule_formula_html() {
        QCOMPARE(water().formula_html(), QString("H<sub>2</sub>O"));
        const Molecule benzene = Molecule::from_xyz_file(":/assets/molecules/benzene.xyz");
        QCOMPARE(benzene.formula_html(), QString("C<sub>6</sub>H<sub>6</sub>"));
    }

    void molecule_parse_errors() {
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Molecule::from_xyz_string("", "x"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Molecule::from_xyz_string("2\nx\nH 0 0 0\n", "x"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Molecule::from_xyz_string("1\nx\nXx 0 0 0\n", "x"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Molecule::from_xyz_string("1\nx\nH 0 a 0\n", "x"));
    }

    void molecule_library_resources() {
        const QStringList files = QDir(":/assets/molecules").entryList({"*.xyz"});
        QVERIFY(files.size() > 10);
        for(const auto& f : files) {
            const Molecule mol = Molecule::from_xyz_file(":/assets/molecules/" + f);
            QVERIFY2(!mol.empty(), qPrintable(f));
        }
    }

    // ------------------------------------------------------------------
    // job specification
    // ------------------------------------------------------------------
    void jobspec_validation() {
        JobSpec spec;
        spec.molecule = water();
        QVERIFY(spec.validate().isEmpty());

        // doublet water cation is fine with UHF but not RHF
        spec.charge = 1;
        spec.multiplicity = 2;
        QVERIFY(!spec.validate().isEmpty());
        spec.method = HFMethod::Unrestricted;
        QVERIFY(spec.validate().isEmpty());

        // parity mismatch
        spec.multiplicity = 1;
        QVERIFY(!spec.validate().isEmpty());

        // Foster-Boys only for RHF
        spec.charge = 0;
        spec.foster_boys = true;
        QVERIFY(!spec.validate().isEmpty());
        spec.method = HFMethod::Restricted;
        QVERIFY(spec.validate().isEmpty());

        // no geometry optimization for a single atom
        spec.molecule = Molecule::from_xyz_string("1\nhe\nHe 0 0 0\n", "he");
        spec.foster_boys = false;
        spec.type = JobType::GeometryOptimization;
        QVERIFY(!spec.validate().isEmpty());
    }

    void jobspec_validation_dft() {
        JobSpec spec;
        spec.molecule = water();
        spec.theory = Theory::DFT;
        QVERIFY(spec.validate().isEmpty());
        QCOMPARE(spec.description(), QString("SVWN5/STO-3G single point"));

        // Foster-Boys works on the (closed-shell) Kohn-Sham orbitals
        spec.foster_boys = true;
        QVERIFY(spec.validate().isEmpty());

        // PyDFT ignores the charge and only does closed-shell single points
        spec.charge = 2;
        QVERIFY(spec.validate().contains("neutral"));
        spec.charge = 0;
        spec.multiplicity = 3;
        QVERIFY(spec.validate().contains("closed-shell"));
        spec.multiplicity = 1;
        spec.type = JobType::GeometryOptimization;
        QVERIFY(spec.validate().contains("geometry optimization"));
        spec.type = JobType::SinglePoint;

        // the method setting belongs to Hartree-Fock and is ignored
        spec.method = HFMethod::Unrestricted;
        QVERIFY(spec.validate().isEmpty());

        spec.functional = "b3lyp";
        QVERIFY(!spec.validate().isEmpty());
        spec.functional = "pbe";
        spec.angular_points = 77;
        QVERIFY(!spec.validate().isEmpty());
        spec.angular_points = 194;
        QVERIFY(spec.validate().isEmpty());

        // grids only exist for H-Ar
        spec.molecule = Molecule::from_xyz_string("2\nKCl\nK 0 0 0\nCl 0 0 2.67\n", "kcl");
        QVERIFY(spec.validate().contains("K"));
        QVERIFY(!spec.validate().contains("Cl"));
        spec.molecule = Molecule::from_xyz_string("2\nHCl\nH 0 0 0\nCl 0 0 1.27\n", "hcl");
        QVERIFY(spec.validate().isEmpty());
    }

    // ------------------------------------------------------------------
    // script generation
    // ------------------------------------------------------------------
    void script_python_string() {
        QCOMPARE(JobScriptWriter::python_string("abc"), QString("'abc'"));
        QCOMPARE(JobScriptWriter::python_string("it's"), QString("'it\\'s'"));
        QCOMPARE(JobScriptWriter::python_string("a\\b"), QString("'a\\\\b'"));
        QCOMPARE(JobScriptWriter::python_string("a\nb"), QString("'a\\nb'"));
    }

    void script_generation() {
        JobSpec spec;
        spec.molecule = water();
        spec.foster_boys = true;
        QString src = JobScriptWriter::generate(spec);
        QVERIFY(src.contains("HF(mol, 'sto3g').rhf("));
        QVERIFY(src.contains("FosterBoys(res"));
        QVERIFY(src.contains("fosterboys=fb"));
        QVERIFY(!src.contains("GeometryOptimization"));
        QCOMPARE(src.count("mol.add_atom("), 3);

        spec.foster_boys = false;
        spec.method = HFMethod::Unrestricted;
        src = JobScriptWriter::generate(spec);
        QVERIFY(src.contains(".uhf("));
        QVERIFY(src.contains("multiplicity=JOB['multiplicity']"));

        spec.method = HFMethod::Restricted;
        spec.type = JobType::GeometryOptimization;
        src = JobScriptWriter::generate(spec);
        QVERIFY(src.contains("GeometryOptimization(mol, 'sto3g'"));
        QVERIFY(src.contains("geomopt=opt"));
    }

    void script_generation_dft() {
        JobSpec spec;
        spec.molecule = water();
        spec.theory = Theory::DFT;
        spec.functional = "pbe";
        spec.foster_boys = true;
        QString src = JobScriptWriter::generate(spec);
        QVERIFY(src.contains("from pydft import DFT"));
        QVERIFY(src.contains("from pyqint import Molecule, FosterBoys"));
        QVERIFY(src.contains("'program': 'pydft'"));
        QVERIFY(src.contains("'functional': 'pbe'"));
        QVERIFY(src.contains("functional=JOB['functional']"));
        QVERIFY(src.contains("res = dft.scf(tol=JOB['tolerance'], verbose=True)"));
        QVERIFY(src.contains("logging.basicConfig(stream=sys.stdout"));
        QVERIFY(src.contains("FosterBoys(res"));
        QVERIFY(!src.contains("nangpts"));
        QVERIFY(!src.contains(".rhf("));
        QVERIFY(!src.contains("HF("));
        QCOMPARE(src.count("mol.add_atom("), 3);

        spec.angular_points = 194;
        spec.foster_boys = false;
        src = JobScriptWriter::generate(spec);
        QVERIFY(src.contains("'angular_points': 194"));
        QVERIFY(src.contains("nangpts=JOB['angular_points']"));
        QVERIFY(!src.contains("FosterBoys"));
    }

    void script_write_directory() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        JobSpec spec;
        spec.molecule = water();
        QVERIFY(JobScriptWriter::write_job_directory(spec, dir.path()).isEmpty());
        QVERIFY(QFileInfo::exists(dir.filePath("job.py")));
        QVERIFY(QFileInfo::exists(dir.filePath("praxis_export.py")));
        QVERIFY(QFileInfo::exists(dir.filePath("molecule.xyz")));
    }

    void script_localization() {
        const QString src = JobScriptWriter::generate_localization("result.json", 7, 3);
        QVERIFY(src.contains("from pyqint import FosterBoys"));
        QVERIFY(src.contains("res = load_result('result.json')"));
        QVERIFY(src.contains("FosterBoys(res, seed=7).run(nr_runners=3)"));
        QVERIFY(src.contains("add_localization('result.json', fb"));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(!JobScriptWriter::write_localization_script(dir.filePath("result.json"), 42, 1).isEmpty());
        QVERIFY(QFile::copy(data_file("h2o_rhf_fb.json"), dir.filePath("result.json")));
        QVERIFY(JobScriptWriter::write_localization_script(dir.filePath("result.json"), 42, 1).isEmpty());
        QVERIFY(QFileInfo::exists(dir.filePath("localize.py")));
        QVERIFY(QFileInfo::exists(dir.filePath("praxis_export.py")));
    }

    /**
     * @brief End-to-end test: run a generated script with a real PyQInt
     *
     * Only executed when PRAXIS_TEST_PYTHON points to a Python
     * interpreter with PyQInt installed.
     */
    void script_end_to_end() {
        const QString python = qEnvironmentVariable("PRAXIS_TEST_PYTHON");
        if(python.isEmpty()) {
            QSKIP("PRAXIS_TEST_PYTHON not set");
        }

        QTemporaryDir dir;
        JobSpec spec;
        spec.molecule = water();
        spec.foster_boys = true;
        QVERIFY(JobScriptWriter::write_job_directory(spec, dir.path()).isEmpty());

        QProcess proc;
        proc.setWorkingDirectory(dir.path());
        proc.start(python, {"-u", "job.py"});
        QVERIFY(proc.waitForFinished(120000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().constData());

        auto res = JobResult::load(dir.filePath("result.json"));
        QVERIFY(std::abs(res->total_energy() - (-74.9623204342175)) < 1e-6);
        QCOMPARE(res->orbital_sets.size(), (size_t)2);
    }

    /**
     * @brief Localize the orbitals of an existing result with a real PyQInt
     *
     * Only executed when PRAXIS_TEST_PYTHON is set (see script_end_to_end).
     */
    void localization_end_to_end() {
        const QString python = qEnvironmentVariable("PRAXIS_TEST_PYTHON");
        if(python.isEmpty()) {
            QSKIP("PRAXIS_TEST_PYTHON not set");
        }

        QTemporaryDir dir;
        JobSpec spec;
        spec.molecule = water();
        QVERIFY(JobScriptWriter::write_job_directory(spec, dir.path()).isEmpty());

        QProcess proc;
        proc.setWorkingDirectory(dir.path());
        proc.start(python, {"-u", "job.py"});
        QVERIFY(proc.waitForFinished(120000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().constData());
        auto before = JobResult::load(dir.filePath("result.json"));
        QCOMPARE(before->orbital_sets.size(), (size_t)1);

        QVERIFY(JobScriptWriter::write_localization_script(dir.filePath("result.json"), 42, 1).isEmpty());
        proc.start(python, {"-u", "localize.py"});
        QVERIFY(proc.waitForFinished(120000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().constData());

        auto after = JobResult::load(dir.filePath("result.json"));
        QCOMPARE(after->orbital_sets.size(), (size_t)2);
        QVERIFY(after->localization.present);
        QVERIFY(after->job["foster_boys"].toBool());
        QCOMPARE(after->total_energy(), before->total_energy());

        // the sum over the occupied orbitals is invariant under localization
        using namespace PopulationAnalysis;
        const double canonical = evaluate(*after, 0, 0, 1, Kind::Hamilton).occupied_sum;
        const double localized = evaluate(*after, 1, 0, 1, Kind::Hamilton).occupied_sum;
        QVERIFY(std::abs(canonical - localized) < 1e-8);
    }

    /**
     * @brief End-to-end test: run a generated DFT script with a real PyDFT,
     *        then localize the Kohn-Sham orbitals afterwards
     *
     * Only executed when PRAXIS_TEST_PYTHON is set (see script_end_to_end)
     * and PyDFT is installed in that interpreter.
     */
    void script_end_to_end_dft() {
        const QString python = qEnvironmentVariable("PRAXIS_TEST_PYTHON");
        if(python.isEmpty()) {
            QSKIP("PRAXIS_TEST_PYTHON not set");
        }
        QProcess proc;
        proc.start(python, {"-c", "import pydft"});
        QVERIFY(proc.waitForFinished(60000));
        if(proc.exitCode() != 0) {
            QSKIP("PyDFT is not installed in PRAXIS_TEST_PYTHON");
        }

        QTemporaryDir dir;
        JobSpec spec;
        spec.molecule = water();
        spec.theory = Theory::DFT;
        spec.functional = "svwn5";
        QVERIFY(JobScriptWriter::write_job_directory(spec, dir.path()).isEmpty());

        proc.setWorkingDirectory(dir.path());
        proc.start(python, {"-u", "job.py"});
        QVERIFY(proc.waitForFinished(300000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().constData());
        QVERIFY(QString::fromUtf8(proc.readAllStandardOutput()).contains(" | E = "));

        auto res = JobResult::load(dir.filePath("result.json"));
        QVERIFY(res->is_dft());
        QCOMPARE(res->functional, QString("svwn5"));
        QVERIFY(res->converged);
        QVERIFY(!res->pydft_version.isEmpty());
        QVERIFY(std::abs(res->total_energy() - (-74.9258131094)) < 1e-6);
        QCOMPARE(res->orbital_sets.size(), (size_t)1);

        QVERIFY(JobScriptWriter::write_localization_script(dir.filePath("result.json"), 42, 1).isEmpty());
        proc.start(python, {"-u", "localize.py"});
        QVERIFY(proc.waitForFinished(120000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().constData());

        auto after = JobResult::load(dir.filePath("result.json"));
        QCOMPARE(after->orbital_sets.size(), (size_t)2);
        QVERIFY(after->localization.present);
        QVERIFY(after->is_dft());
    }

    /**
     * @brief Full workflow: bootstrap the managed environment with uv and
     *        run a job through JobRunner
     *
     * Downloads Python and PyQInt, hence only executed when
     * PRAXIS_TEST_UV points to a uv executable.
     */
    void environment_install_and_run() {
        const QString uv = qEnvironmentVariable("PRAXIS_TEST_UV");
        if(uv.isEmpty()) {
            QSKIP("PRAXIS_TEST_UV not set");
        }

        QStandardPaths::setTestModeEnabled(true);
        QDir(PythonEnvironment::root_directory()).removeRecursively();
        QSettings().setValue("python/uv_path", uv);
        PythonEnvironment::set_interpreter_override(QString());

        PythonEnvironment env;

        // nothing installed yet
        QSignalSpy check_spy(&env, &PythonEnvironment::finished);
        env.check();
        QCOMPARE(env.get_state(), PythonEnvironment::State::Missing);

        // install
        QSignalSpy install_spy(&env, &PythonEnvironment::finished);
        env.install();
        QVERIFY(install_spy.wait(600000));
        QVERIFY2(install_spy.front().at(0).toBool(), qPrintable(install_spy.front().at(1).toString()));
        QCOMPARE(env.get_state(), PythonEnvironment::State::Ready);
        QCOMPARE(env.get_pyqint_version(), QString(PYQINT_PINNED_VERSION));
        QCOMPARE(env.get_pydft_version(), QString(PYDFT_PINNED_VERSION));
        QVERIFY(env.has_pydft());
        QVERIFY(env.get_python_version().startsWith(PYTHON_MANAGED_VERSION));

        // the virtual environment must be based on the standalone Python
        // downloaded by uv, not on a Python installation of the system
        QVERIFY(QFileInfo(env.python_executable()).absoluteFilePath().startsWith(PythonEnvironment::root_directory()));
        const QDir interpreters(QDir(PythonEnvironment::root_directory()).filePath("interpreters"));
        QVERIFY2(!interpreters.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(), "no managed interpreter");
        QFile cfg(QDir(PythonEnvironment::env_directory()).filePath("pyvenv.cfg"));
        QVERIFY(cfg.open(QIODevice::ReadOnly));
        const QString cfgtext = QString::fromUtf8(cfg.readAll());
        QVERIFY2(cfgtext.contains(interpreters.absolutePath()), qPrintable(cfgtext));

        // run a UHF job on the water cation through the job runner
        JobRunner runner(&env);
        JobSpec spec;
        spec.molecule = water();
        spec.charge = 1;
        spec.multiplicity = 2;
        spec.method = HFMethod::Unrestricted;
        QSignalSpy iter_spy(&runner, &JobRunner::scf_iteration);
        QSignalSpy job_spy(&runner, &JobRunner::finished);
        QVERIFY(runner.start(spec).isEmpty());
        QVERIFY(job_spy.wait(120000));
        QVERIFY2(job_spy.front().at(0).toBool(), qPrintable(job_spy.front().at(1).toString()));
        QVERIFY(iter_spy.size() > 3);

        auto res = JobResult::load(job_spy.front().at(2).toString());
        QVERIFY(res->is_unrestricted());
        QCOMPARE(res->nelec, 9);
        QCOMPARE(res->nalpha, 5);
        QCOMPARE(res->nbeta, 4);
        QVERIFY(QFileInfo::exists(QDir(runner.get_job_directory()).filePath("output.log")));

        // and a DFT job; its SCF iterations are reported as well
        JobSpec dft;
        dft.molecule = water();
        dft.theory = Theory::DFT;
        QSignalSpy dft_iter_spy(&runner, &JobRunner::scf_iteration);
        QSignalSpy dft_spy(&runner, &JobRunner::finished);
        QVERIFY(runner.start(dft).isEmpty());
        QVERIFY(dft_spy.wait(300000));
        QVERIFY2(dft_spy.front().at(0).toBool(), qPrintable(dft_spy.front().at(1).toString()));
        QVERIFY(dft_iter_spy.size() > 3);
        QVERIFY(JobResult::load(dft_spy.front().at(2).toString())->is_dft());

        QDir(PythonEnvironment::root_directory()).removeRecursively();
        QDir(JobRunner::jobs_directory()).removeRecursively();
    }

    // ------------------------------------------------------------------
    // result parsing
    // ------------------------------------------------------------------
    void result_rhf() {
        auto res = JobResult::load(data_file("h2o_rhf_fb.json"));
        QCOMPARE(res->method, QString("rhf"));
        QCOMPARE(res->nelec, 10);
        QCOMPARE(res->positions.size(), (size_t)3);
        QCOMPARE(res->basis->size(), (size_t)7);
        QVERIFY(std::abs(res->total_energy() - (-74.9623204342175)) < 1e-10);

        QCOMPARE(res->orbital_sets.size(), (size_t)2);
        QCOMPARE(res->orbital_sets[0].label, QString("Canonical"));
        QCOMPARE(res->orbital_sets[0].homo(), 4);
        QCOMPARE(res->orbital_sets[1].label, QString("Foster-Boys"));
        QVERIFY(res->localization.present);

        // neutral molecule: charges sum to zero
        double qm = 0.0, ql = 0.0;
        for(double q : res->mulliken) qm += q;
        for(double q : res->lowdin) ql += q;
        QVERIFY(std::abs(qm) < 1e-8);
        QVERIFY(std::abs(ql) < 1e-8);

        QVERIFY(res->matrices.size() >= 6);
        QCOMPARE(res->matrices.front().first, QString("overlap"));
        const DenseMatrix& S = res->matrices.front().second;
        QCOMPARE(S.rows, 7);
        for(int i = 0; i < 7; ++i) {
            QVERIFY(std::abs(S(i, i) - 1.0) < 1e-6);
        }

        QVERIFY(!res->optimization);
    }

    void result_uhf() {
        auto res = JobResult::load(data_file("o2_uhf.json"));
        QVERIFY(res->is_unrestricted());
        QCOMPARE(res->multiplicity, 3);
        QCOMPARE(res->nalpha, 9);
        QCOMPARE(res->nbeta, 7);
        QCOMPARE(res->orbital_sets.size(), (size_t)2);
        QCOMPARE(res->orbital_sets[0].spin, QString("alpha"));
        QCOMPARE(res->orbital_sets[0].homo(), 8);
        QCOMPARE(res->orbital_sets[1].homo(), 6);
    }

    void result_geomopt() {
        auto res = JobResult::load(data_file("h2o_geomopt.json"));
        QVERIFY(res->optimization != nullptr);
        QVERIFY(res->optimization->frames.size() > 2);
        QCOMPARE(res->optimization->frames.size(), res->optimization->energies.size());
        // optimization lowers the energy
        QVERIFY(res->optimization->energies.back() < res->optimization->energies.front());
    }

    void result_dft() {
        auto res = JobResult::load(data_file("h2o_svwn5.json"));
        QVERIFY(res->is_dft());
        QVERIFY(!res->is_unrestricted());
        QCOMPARE(res->method, QString("rks"));
        QCOMPARE(res->functional, QString("svwn5"));
        QCOMPARE(res->method_label(), QString("Kohn-Sham DFT (SVWN5)"));
        QVERIFY(res->converged);
        QVERIFY(!res->pydft_version.isEmpty());
        QCOMPARE(res->nelec, 10);
        QCOMPARE(res->orbital_sets.size(), (size_t)2);
        QCOMPARE(res->orbital_sets[0].homo(), 4);
        QCOMPARE(res->orbital_sets[1].label, QString("Foster-Boys"));

        // the energy terms add up to the total energy
        double sum = 0.0;
        bool has_ec = false;
        for(const auto& comp : res->energy_components) {
            if(comp.first != "energy") {
                sum += comp.second;
            }
            has_ec |= comp.first == "ec";
        }
        QVERIFY(has_ec);
        QVERIFY(std::abs(sum - res->total_energy()) < 1e-8);

        QVERIFY(res->find_matrix("hartree") != nullptr);
        QVERIFY(res->find_matrix("xc") != nullptr);
        QCOMPARE(res->find_matrix("fock")->label, QString("F (Kohn-Sham)"));

        // grid-based timings are flattened to scalars
        QVERIFY(!res->timing.empty());

        // the bonding analysis works with the Kohn-Sham matrix
        using namespace PopulationAnalysis;
        QVERIFY(evaluate(*res, 0, 0, 1, Kind::Hamilton).occupied_sum < 0.0);
    }

    void result_errors() {
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, JobResult::parse("not json"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, JobResult::parse("{\"schema\": 99}"));
    }

    // ------------------------------------------------------------------
    // orbital evaluation (reference values computed with PyQInt 1.4.3)
    // ------------------------------------------------------------------
    void basis_functions_match_pyqint() {
        auto res = JobResult::load(data_file("h2o_rhf_fb.json"));

        const std::vector<std::pair<glm::dvec3, std::vector<double>>> reference = {
            {{0.3, -0.2, 0.5}, {4.273799176987582e-01, 3.891130251512668e-01, 4.950376301549634e-01,
                                -3.300250867699756e-01, 3.984127380390816e-01, 4.536241963068757e-02,
                                6.418954439821872e-02}},
            {{-0.5, 0.1, 0.0}, {1.577040578294883e-01, 3.857203883009980e-01, -5.947967758066635e-01,
                                1.189593551613327e-01, -3.075766564572279e-01, 8.642397596189072e-02,
                                7.044264950321390e-02}},
        };

        for(const auto& ref : reference) {
            for(size_t i = 0; i < res->basis->size(); ++i) {
                const double v = BasisSet::evaluate_function((*res->basis)[i], ref.first);
                QVERIFY2(std::abs(v - ref.second[i]) < 1e-12, qPrintable(QString("basis function %1").arg(i)));
            }
        }

        // molecular orbitals: HOMO and LUMO
        const auto& set = res->orbital_sets[0];
        QVERIFY(std::abs(res->basis->evaluate({0.3, -0.2, 0.5}, set.coefficients[4]) - (-4.950376301549570e-01)) < 1e-12);
        QVERIFY(std::abs(res->basis->evaluate({0.3, -0.2, 0.5}, set.coefficients[5]) - 1.487280998491095e-01) < 1e-12);
        QVERIFY(std::abs(res->basis->evaluate({-0.5, 0.1, 0.0}, set.coefficients[5]) - (-3.934128678474843e-01)) < 1e-12);
    }

    void orbital_normalization_on_grid() {
        auto res = JobResult::load(data_file("h2o_rhf_fb.json"));
        const Molecule mol = res->get_molecule();
        glm::vec3 offset;
        auto structure = mol.to_structure(&offset);

        std::vector<glm::vec3> atoms;
        for(const auto& a : mol.get_atoms()) {
            atoms.push_back(glm::vec3(a.position) - offset);
        }

        OrbitalBuilder builder(res->basis, atoms, offset);
        OrbitalGridSettings settings;
        settings.padding = 4.0f;
        settings.spacing = 0.05f;

        // HOMO (O 2p lone pair) integrates to one
        auto field = builder.build_field(res->orbital_sets[0].coefficients[4], settings);
        double sum = 0.0;
        for(float v : field->data()) {
            sum += (double)v * v;
        }
        const double dv = std::pow(settings.spacing * 1.8897259886, 3);
        QVERIFY2(std::abs(sum * dv - 1.0) < 1e-3, qPrintable(QString("norm = %1").arg(sum * dv)));

        // suggested isovalue encloses the requested fraction
        const float iso = OrbitalBuilder::suggest_isovalue(*field, 0.9f);
        QVERIFY(iso > 0.0f && iso < field->max_abs());

        auto meshes = OrbitalBuilder::build_meshes(*field, iso);
        QVERIFY(!meshes.positive.empty());
        QVERIFY(!meshes.negative.empty());
    }

    // ------------------------------------------------------------------
    // marching cubes
    // ------------------------------------------------------------------
    void marching_cubes_sphere() {
        const float h = 0.05f;
        const unsigned int n = 61;
        ScalarField field(glm::vec3(-1.5f), h, {n, n, n});
        for(unsigned int k = 0; k < n; ++k) {
            for(unsigned int j = 0; j < n; ++j) {
                for(unsigned int i = 0; i < n; ++i) {
                    const glm::vec3 p = field.position(i, j, k);
                    field.set(i, j, k, 1.0f - glm::dot(p, p));
                }
            }
        }

        // positive isovalue: region f > 0.5 is a sphere with radius sqrt(0.5)
        verify_sphere_mesh(marching_cubes(field, 0.5f), std::sqrt(0.5f), 0.01f);

        // negative isovalue on the negated field gives the same sphere
        for(auto& v : field.data()) {
            v = -v;
        }
        verify_sphere_mesh(marching_cubes(field, -0.5f), std::sqrt(0.5f), 0.01f);
    }

    void sphere_mesh_closed() {
        verify_sphere_mesh(sphere_mesh(glm::vec3(0.0f), 1.3f), 1.3f, 1e-4f);
        verify_sphere_mesh(sphere_mesh(glm::vec3(0.0f), 0.5f, 2, 3), 0.5f, 1e-4f);
    }

    // ------------------------------------------------------------------
    // population analysis (reference values: pyqint.PopulationAnalysis of
    // PyQInt 1.4.3, applied to the same result file)
    // ------------------------------------------------------------------
    void population_analysis_matches_pyqint() {
        using namespace PopulationAnalysis;
        auto res = JobResult::load(data_file("h2o_rhf_fb.json"));

        const std::vector<std::pair<Kind, std::vector<double>>> reference = {
            {Kind::Hamilton, {0.010564272477614252, -0.17690957386029055, -0.20225887625011651,
                              0.06465670972825072, 0.0, 1.2546056460312225, 0.5292282637842698}},
            {Kind::Overlap, {-0.0005850653331922144, 0.1164840014884288, 0.15362552584584527,
                             -0.026278002411380414, 0.0, -0.7466838433755053, -0.4019747949939212}},
            {Kind::BondIndex, {0.00017394459037625858, -0.000515505309857355, 0.31325173518340066,
                               0.2513698309347904, 0.0, -0.492174775870477, -0.8196509098246463}},
        };
        for(const auto& ref : reference) {
            const PairPopulation pop = evaluate(*res, 0, 0, 1, ref.first);
            QCOMPARE(pop.values.size(), ref.second.size());
            QCOMPARE(pop.spin_factor, 2.0);
            double occ = 0.0;
            for(size_t k = 0; k < ref.second.size(); ++k) {
                QVERIFY2(std::abs(pop.values[k] - ref.second[k]) < 1e-10, qPrintable(short_name(ref.first)));
                if(k < 5) {
                    occ += ref.second[k];
                }
            }
            QVERIFY(std::abs(pop.occupied_sum - occ) < 1e-10);
        }

        // O-H bonding: negative MOHP, positive MOOP summed over occupied orbitals
        QVERIFY(evaluate(*res, 0, 0, 1, Kind::Hamilton).occupied_sum < 0.0);
        QVERIFY(evaluate(*res, 0, 0, 1, Kind::Overlap).occupied_sum > 0.0);

        // symmetric in the two atoms, and invariant under localization
        for(Kind k : {Kind::Hamilton, Kind::Overlap, Kind::BondIndex}) {
            const PairPopulation ab = evaluate(*res, 0, 0, 1, k);
            const PairPopulation ba = evaluate(*res, 0, 1, 0, k);
            const PairPopulation fb = evaluate(*res, 1, 0, 1, k);
            for(size_t i = 0; i < ab.values.size(); ++i) {
                QVERIFY(std::abs(ab.values[i] - ba.values[i]) < 1e-12);
            }
            QVERIFY(std::abs(ab.occupied_sum - fb.occupied_sum) < 1e-8);
        }

        QVERIFY_THROWS_EXCEPTION(std::runtime_error, evaluate(*res, 0, 1, 1, Kind::Hamilton));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, evaluate(*res, 0, 0, 3, Kind::Hamilton));
        QCOMPARE(basis_functions_on_atom(*res->basis, 0).size(), (size_t)5);   // O: 1s 2s 2p
        QCOMPARE(basis_functions_on_atom(*res->basis, 1).size(), (size_t)1);   // H: 1s
    }

    void population_analysis_uhf() {
        using namespace PopulationAnalysis;
        auto res = JobResult::load(data_file("o2_uhf.json"));
        const PairPopulation alpha = evaluate(*res, 0, 0, 1, Kind::Hamilton);
        const PairPopulation beta = evaluate(*res, 1, 0, 1, Kind::Hamilton);
        QCOMPARE(alpha.matrix_key, QString("fock_alpha"));
        QCOMPARE(beta.matrix_key, QString("fock_beta"));
        QCOMPARE(alpha.spin_factor, 1.0);
        QVERIFY(alpha.occupied_sum + beta.occupied_sum < 0.0);     // O2 is bound
        QCOMPARE(evaluate(*res, 0, 0, 1, Kind::BondIndex).matrix_key, QString("density_alpha"));
    }

    void orbital_frontier_labels() {
        auto res = JobResult::load(data_file("h2o_rhf_fb.json"));
        const OrbitalSet& set = res->orbital_sets[0];
        QCOMPARE(set.frontier_label(4), QString("HOMO"));
        QCOMPARE(set.frontier_label(5), QString("LUMO"));
        QCOMPARE(set.frontier_label(2), QString("HOMO-2"));
        QCOMPARE(set.frontier_label(6), QString("LUMO+1"));
        QCOMPARE(res->find_orbital_set("Foster-Boys"), 1);
        QCOMPARE(res->find_orbital_set("nope"), -1);
        QVERIFY(res->find_matrix("fock") != nullptr);
        QVERIFY(res->find_matrix("fock_alpha") == nullptr);
    }

    // ------------------------------------------------------------------
    // camera orientation
    // ------------------------------------------------------------------
    void principal_axes_orientation() {
        constexpr float pi = 3.14159265358979323846f;
        // planar "molecule" in a tilted plane with normal n; widest along a
        const QVector3D n = QVector3D(1.0f, 1.0f, 1.0f).normalized();
        const QVector3D a = QVector3D(1.0f, -1.0f, 0.0f).normalized();
        const QVector3D b = QVector3D::crossProduct(n, a);
        std::vector<QVector3D> points;
        for(int i = 0; i < 6; ++i) {
            const float phi = (float)i * pi / 3.0f;
            points.push_back(3.0f * std::cos(phi) * a + 1.5f * std::sin(phi) * b);
        }

        // camera looks along +y with +z up: face-on puts the normal along y
        const QMatrix4x4 face = Scene::principal_axes_rotation(points, true);
        QVERIFY(std::abs(std::abs(face.mapVector(n).y()) - 1.0f) < 1e-4f);
        QVERIFY(std::abs(std::abs(face.mapVector(a).x()) - 1.0f) < 1e-4f);

        // edge-on puts the normal along the vertical axis of the screen
        const QMatrix4x4 edge = Scene::principal_axes_rotation(points, false);
        QVERIFY(std::abs(std::abs(edge.mapVector(n).z()) - 1.0f) < 1e-4f);

        // proper rotations
        for(const QMatrix4x4& m : {face, edge}) {
            QVERIFY(std::abs(m.determinant() - 1.0) < 1e-4);
        }

        // fixed alignments: TOP looks down the z-axis
        const QMatrix4x4 top = Scene::alignment_rotation(CameraAlignment::TOP);
        QVERIFY(std::abs(std::abs(top.mapVector(QVector3D(0, 0, 1)).y()) - 1.0f) < 1e-4f);
    }

    void marching_cubes_empty() {
        ScalarField field(glm::vec3(0.0f), 0.1f, {10, 10, 10});
        QVERIFY(marching_cubes(field, 0.5f).empty());
    }

    void message_log_bounded() {
        MessageLog log(3);
        QSignalSpy spy(&log, &MessageLog::message_logged);
        for(int i = 0; i < 5; ++i) {
            log.record(QtDebugMsg, "default", QString("message %1").arg(i));
        }
        log.record(QtWarningMsg, "praxis.test", "something odd");
        QCOMPARE(spy.count(), 6);

        qint64 seq = 0;
        const QStringList lines = log.entries(&seq);
        QCOMPARE(seq, 6);
        QCOMPARE(lines.size(), 3);                  // the oldest messages are dropped
        QVERIFY(lines[0].endsWith("debug    message 3"));
        QVERIFY(lines[2].endsWith("warning  [praxis.test] something odd"));

        log.clear();
        QVERIFY(log.entries().isEmpty());
    }
};

QTEST_MAIN(TestCore)
#include "test_core.moc"
