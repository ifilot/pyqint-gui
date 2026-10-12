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

#include "job_result.h"

#include <stdexcept>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include "data/units.h"
#include "job_spec.h"

namespace {

constexpr int SUPPORTED_SCHEMA = 1;

std::runtime_error parse_error(const QString& msg) {
    return std::runtime_error(("Invalid result file: " + msg).toStdString());
}

std::vector<double> to_vector(const QJsonValue& v) {
    std::vector<double> result;
    const QJsonArray arr = v.toArray();
    result.reserve(arr.size());
    for(const auto& x : arr) {
        result.push_back(x.toDouble());
    }
    return result;
}

glm::dvec3 to_vec3(const QJsonValue& v) {
    const QJsonArray arr = v.toArray();
    if(arr.size() != 3) {
        throw parse_error("expected a 3-vector");
    }
    return glm::dvec3(arr[0].toDouble(), arr[1].toDouble(), arr[2].toDouble());
}

std::vector<glm::dvec3> to_vec3_list(const QJsonValue& v) {
    std::vector<glm::dvec3> result;
    for(const auto& x : v.toArray()) {
        result.push_back(to_vec3(x));
    }
    return result;
}

DenseMatrix to_matrix(const QString& label, const QJsonValue& v) {
    DenseMatrix mat;
    mat.label = label;
    const QJsonArray rows = v.toArray();
    mat.rows = rows.size();
    mat.cols = mat.rows > 0 ? rows[0].toArray().size() : 0;
    mat.data.reserve((size_t)mat.rows * mat.cols);
    for(const auto& row : rows) {
        const QJsonArray r = row.toArray();
        if(r.size() != mat.cols) {
            throw parse_error(QString("matrix '%1' is not rectangular").arg(label));
        }
        for(const auto& x : r) {
            mat.data.push_back(x.toDouble());
        }
    }
    return mat;
}

} // namespace

int OrbitalSet::homo() const {
    int result = -1;
    for(size_t i = 0; i < this->occupations.size(); ++i) {
        if(this->occupations[i] > 0.0) {
            result = (int)i;
        }
    }
    return result;
}

QString OrbitalSet::frontier_label(int index) const {
    const int h = this->homo();
    if(h < 0) {
        return QString();
    }
    if(index == h) return "HOMO";
    if(index == h + 1) return "LUMO";
    if(index < h && h - index <= 3) return QString("HOMO-%1").arg(h - index);
    if(index > h + 1 && index - h - 1 <= 3) return QString("LUMO+%1").arg(index - h - 1);
    return QString();
}

std::shared_ptr<JobResult> JobResult::load(const QString& filename) {
    QFile f(filename);
    if(!f.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QString("Cannot open %1").arg(filename).toStdString());
    }
    return parse(f.readAll());
}

std::shared_ptr<JobResult> JobResult::parse(const QByteArray& data) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if(err.error != QJsonParseError::NoError) {
        throw parse_error(err.errorString());
    }
    const QJsonObject root = doc.object();

    auto res = std::make_shared<JobResult>();

    res->schema = root["schema"].toInt();
    if(res->schema != SUPPORTED_SCHEMA) {
        throw parse_error(QString("unsupported schema version %1 (expected %2)")
                          .arg(res->schema).arg(SUPPORTED_SCHEMA));
    }

    // generator
    const QJsonObject gen = root["generator"].toObject();
    res->pyqint_version = gen["pyqint_version"].toString();
    res->pydft_version = gen["pydft_version"].toString();
    res->python_version = gen["python_version"].toString();
    res->created = gen["created"].toString();
    res->job = root["job"].toObject();

    // molecule
    const QJsonObject mol = root["molecule"].toObject();
    res->molecule_name = mol["name"].toString();
    res->charge = mol["charge"].toInt();
    for(const auto& a : mol["atoms"].toArray()) {
        const QJsonObject atom = a.toObject();
        res->elements.push_back(atom["element"].toString());
        res->atomic_numbers.push_back(atom["atomic_number"].toInt());
        res->positions.push_back(to_vec3(atom["position"]));
    }
    if(res->positions.empty()) {
        throw parse_error("no atoms");
    }

    // basis set
    res->basis = std::make_shared<BasisSet>();
    for(const auto& b : root["basis"].toArray()) {
        const QJsonObject bobj = b.toObject();
        BasisFunction bf;
        bf.center = to_vec3(bobj["center"]);
        bf.atom = bobj["atom"].toInt(-1);
        bf.label = bobj["label"].toString().toStdString();
        for(const auto& g : bobj["gtos"].toArray()) {
            const QJsonArray p = g.toArray();
            if(p.size() != 5) {
                throw parse_error("GTO primitive should have five entries");
            }
            bf.gtos.emplace_back(p[0].toDouble(), p[1].toDouble(),
                                 p[2].toInt(), p[3].toInt(), p[4].toInt());
        }
        res->basis->add(bf);
    }

    // SCF
    const QJsonObject scf = root["scf"].toObject();
    res->method = scf["method"].toString();
    res->functional = scf["functional"].toString(res->job["functional"].toString());
    res->converged = scf["converged"].toBool(true);
    res->nelec = scf["nelec"].toInt();
    res->multiplicity = scf["multiplicity"].toInt(1);
    res->nalpha = scf["nalpha"].toInt(res->nelec / 2);
    res->nbeta = scf["nbeta"].toInt(res->nelec / 2);
    res->scf_energies = to_vector(scf["iterations"]);

    static const char* component_order[] = {
        "energy", "ekin", "enuc", "erep", "ex", "ecore", "ej", "ex_alpha", "ex_beta", "ec", "enucrep"
    };
    const QJsonObject comps = scf["components"].toObject();
    for(const char* key : component_order) {
        if(comps.contains(key) && comps[key].isDouble()) {
            res->energy_components.emplace_back(key, comps[key].toDouble());
        }
    }

    // orbitals
    const size_t nbf = res->basis->size();
    for(const auto& o : root["orbitals"].toArray()) {
        const QJsonObject oobj = o.toObject();
        OrbitalSet set;
        set.label = oobj["label"].toString();
        set.spin = oobj["spin"].toString();
        set.energies = to_vector(oobj["energies"]);
        set.occupations = to_vector(oobj["occupations"]);
        for(const auto& c : oobj["coefficients"].toArray()) {
            set.coefficients.push_back(to_vector(c));
            if(set.coefficients.back().size() != nbf) {
                throw parse_error(QString("orbital coefficients of '%1' do not match basis size").arg(set.label));
            }
        }
        if(set.coefficients.size() != set.energies.size()) {
            throw parse_error(QString("number of orbitals in '%1' is inconsistent").arg(set.label));
        }
        res->orbital_sets.push_back(std::move(set));
    }

    // matrices (keep a didactic order)
    static const char* matrix_order[] = {
        "overlap", "kinetic", "nuclear", "hcore", "transform", "hartree", "xc", "fock", "fock_alpha",
        "fock_beta", "density", "density_alpha", "density_beta"
    };
    const QJsonObject mats = root["matrices"].toObject();
    for(const char* key : matrix_order) {
        if(mats.contains(key)) {
            const QJsonObject m = mats[key].toObject();
            res->matrices.emplace_back(key, to_matrix(m["label"].toString(key), m["data"]));
        }
    }

    // Orbital coefficients are stored per orbital in JSON. Present C with
    // basis functions as rows and molecular orbitals as columns, as in PyQInt.
    for(size_t index = 0; index < res->orbital_sets.size(); ++index) {
        const OrbitalSet& set = res->orbital_sets[index];
        if(nbf == 0 || set.coefficients.empty()) {
            continue;
        }
        DenseMatrix coefficients;
        coefficients.label = QString("Coefficients (%1)").arg(set.label);
        coefficients.rows = int(nbf);
        coefficients.cols = int(set.coefficients.size());
        coefficients.data.resize(nbf * set.coefficients.size());
        for(int row = 0; row < coefficients.rows; ++row) {
            for(int col = 0; col < coefficients.cols; ++col) {
                coefficients.data[size_t(row) * coefficients.cols + col] = set.coefficients[col][row];
            }
        }
        res->matrices.emplace_back(QString("coefficients_%1").arg(index), std::move(coefficients));
    }

    // populations
    const QJsonObject pop = root["populations"].toObject();
    res->mulliken = to_vector(pop["mulliken"]);
    res->lowdin = to_vector(pop["lowdin"]);

    // timing
    const QJsonObject timing = root["timing"].toObject();
    for(auto it = timing.begin(); it != timing.end(); ++it) {
        if(it.value().isDouble()) {
            res->timing.emplace_back(it.key(), it.value().toDouble());
        }
    }

    // localization
    if(root.contains("localization")) {
        const QJsonObject loc = root["localization"].toObject();
        res->localization.present = true;
        res->localization.r2start = loc["r2start"].toDouble();
        res->localization.r2final = loc["r2final"].toDouble();
        res->localization.iterations = loc["iterations"].toInt();
    }

    // geometry optimization
    if(root.contains("optimization")) {
        const QJsonObject opt = root["optimization"].toObject();
        res->optimization = std::make_unique<OptimizationTrajectory>();
        res->optimization->success = opt["success"].toBool();
        res->optimization->message = opt["message"].toString();
        res->optimization->energies = to_vector(opt["energies"]);
        for(const auto& f : opt["frames"].toArray()) {
            res->optimization->frames.push_back(to_vec3_list(f));
            if(res->optimization->frames.back().size() != res->positions.size()) {
                throw parse_error("trajectory frame has the wrong number of atoms");
            }
        }
        for(const auto& f : opt["forces"].toArray()) {
            res->optimization->forces.push_back(to_vec3_list(f));
        }
    }

    return res;
}

QString JobResult::method_label() const {
    if(this->is_dft()) {
        return QString("Kohn-Sham DFT (%1)").arg(JobSpec::functional_label(this->functional));
    }
    return this->is_unrestricted() ? "Unrestricted Hartree-Fock" : "Restricted Hartree-Fock";
}

double JobResult::total_energy() const {
    for(const auto& item : this->energy_components) {
        if(item.first == "energy") {
            return item.second;
        }
    }
    return this->scf_energies.empty() ? 0.0 : this->scf_energies.back();
}

const DenseMatrix* JobResult::find_matrix(const QString& key) const {
    for(const auto& m : this->matrices) {
        if(m.first == key) {
            return &m.second;
        }
    }
    return nullptr;
}

int JobResult::find_orbital_set(const QString& label) const {
    for(size_t i = 0; i < this->orbital_sets.size(); ++i) {
        if(this->orbital_sets[i].label == label) {
            return (int)i;
        }
    }
    return -1;
}

Molecule JobResult::get_molecule() const {
    Molecule mol(this->molecule_name);
    for(size_t i = 0; i < this->positions.size(); ++i) {
        mol.add_atom(this->elements[i], this->positions[i] * BOHR_TO_ANGSTROM);
    }
    return mol;
}

Molecule JobResult::get_trajectory_molecule(size_t frame) const {
    if(!this->optimization || frame >= this->optimization->frames.size()) {
        return this->get_molecule();
    }
    Molecule mol(this->molecule_name);
    const auto& pos = this->optimization->frames[frame];
    for(size_t i = 0; i < pos.size(); ++i) {
        mol.add_atom(this->elements[i], pos[i] * BOHR_TO_ANGSTROM);
    }
    return mol;
}

QString JobResult::energy_component_label(const QString& key) {
    if(key == "energy") return "Total energy";
    if(key == "ekin") return "Kinetic energy";
    if(key == "enuc") return "Electron-nuclear attraction";
    if(key == "erep") return "Electron-electron repulsion (Coulomb)";
    if(key == "ex") return "Exchange energy";
    if(key == "ecore") return "Core energy (kinetic + attraction)";
    if(key == "ej") return "Coulomb (Hartree) energy";
    if(key == "ex_alpha") return "Exchange energy (α)";
    if(key == "ex_beta") return "Exchange energy (β)";
    if(key == "ec") return "Correlation energy";
    if(key == "enucrep") return "Nuclear repulsion";
    return key;
}
