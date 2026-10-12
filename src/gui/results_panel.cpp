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

#include "results_panel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "calculation/job_spec.h"
#include "calculation/matrix_export.h"
#include "data/units.h"
#include "icons.h"
#include "line_plot_widget.h"
#include "mo_diagram_widget.h"

namespace {

QTableWidgetItem* number_item(double value, int decimals) {
    auto* item = new QTableWidgetItem(QString::number(value, 'f', decimals));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setData(Qt::UserRole, value);
    return item;
}

QTableWidgetItem* text_item(const QString& text) {
    return new QTableWidgetItem(text);
}

QString fmt(double v, int decimals = 6) {
    return QString::number(v, 'f', decimals);
}

} // namespace

ResultsPanel::ResultsPanel(QWidget* parent) :
    QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    this->tabs = new QTabWidget;
    this->tabs->setDocumentMode(true);
    layout->addWidget(this->tabs);

    this->tabs->addTab(this->build_summary_tab(), "Summary");
    this->tab_orbitals = this->build_orbitals_tab();
    this->tabs->addTab(this->tab_orbitals, "Orbitals");
    this->tab_diagram = this->build_diagram_tab();
    this->tabs->addTab(this->tab_diagram, "MO diagram");
    this->tab_populations = this->build_populations_tab();
    this->tabs->addTab(this->tab_populations, "Charges");
    this->tab_matrices = this->build_matrices_tab();
    this->tabs->addTab(this->tab_matrices, "Matrices");
    this->tab_optimization = this->build_optimization_tab();
    this->tabs->addTab(this->tab_optimization, "Optimization");

    this->clear();
}

// ---------------------------------------------------------------------------
// construction of the tabs
// ---------------------------------------------------------------------------
QWidget* ResultsPanel::build_summary_tab() {
    this->summary = new QTextBrowser;
    this->summary->setOpenExternalLinks(true);
    return this->summary;
}

QWidget* ResultsPanel::build_orbitals_tab() {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);

    auto* lset = new QHBoxLayout;
    lset->addWidget(new QLabel("Orbitals:"));
    this->combo_set = new QComboBox;
    lset->addWidget(this->combo_set, 1);
    this->button_gallery = new QPushButton(bluecurve_icon("gnome-stock-insert-table"), "Gallery");
    this->button_gallery->setToolTip("Show all orbitals side by side in a separate window (Ctrl+G)");
    lset->addWidget(this->button_gallery);
    this->button_localize = new QPushButton(bluecurve_icon("gnome-run"), "Localize");
    lset->addWidget(this->button_localize);
    layout->addLayout(lset);

    this->table_orbitals = new QTableWidget;
    this->table_orbitals->setColumnCount(5);
    this->table_orbitals->setHorizontalHeaderLabels({"#", "Label", "Energy (Ht)", "Energy (eV)", "Occ."});
    this->table_orbitals->verticalHeader()->setVisible(false);
    this->table_orbitals->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->table_orbitals->setSelectionMode(QAbstractItemView::SingleSelection);
    this->table_orbitals->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->table_orbitals->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    this->table_orbitals->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(this->table_orbitals, 1);

    this->label_contributions = new QLabel;
    this->label_contributions->setWordWrap(true);
    this->label_contributions->setTextFormat(Qt::RichText);
    layout->addWidget(this->label_contributions);

    // isosurface settings
    auto* group = new QGroupBox("Isosurface");
    auto* form = new QFormLayout(group);

    this->check_show = new QCheckBox("Show selected orbital");
    this->check_show->setChecked(true);
    form->addRow(this->check_show);

    auto* lauto = new QHBoxLayout;
    this->radio_auto = new QRadioButton("Enclose");
    this->radio_auto->setChecked(true);
    this->radio_auto->setToolTip("Choose the isovalue such that the surface encloses this fraction of the electron density");
    this->spin_fraction = new QSpinBox;
    this->spin_fraction->setRange(10, 99);
    this->spin_fraction->setValue(90);
    this->spin_fraction->setSuffix(" % of density");
    lauto->addWidget(this->radio_auto);
    lauto->addWidget(this->spin_fraction, 1);
    form->addRow(lauto);

    auto* lman = new QHBoxLayout;
    this->radio_manual = new QRadioButton("Isovalue");
    this->spin_isovalue = new QDoubleSpinBox;
    this->spin_isovalue->setDecimals(4);
    this->spin_isovalue->setRange(0.0005, 5.0);
    this->spin_isovalue->setSingleStep(0.005);
    this->spin_isovalue->setValue(0.05);
    this->spin_isovalue->setEnabled(false);
    lman->addWidget(this->radio_manual);
    lman->addWidget(this->spin_isovalue, 1);
    form->addRow(lman);

    auto* bgroup = new QButtonGroup(w);
    bgroup->addButton(this->radio_auto);
    bgroup->addButton(this->radio_manual);

    this->combo_grid = new QComboBox;
    this->combo_grid->addItem("Coarse (fast)", 0.20);
    this->combo_grid->addItem("Normal", 0.12);
    this->combo_grid->addItem("Fine (slow)", 0.07);
    this->combo_grid->setCurrentIndex(1);
    form->addRow("Grid:", this->combo_grid);

    this->slider_opacity = new QSlider(Qt::Horizontal);
    this->slider_opacity->setRange(10, 100);
    this->slider_opacity->setValue(80);
    form->addRow("Opacity:", this->slider_opacity);

    this->label_orbital_info = new QLabel;
    this->label_orbital_info->setWordWrap(true);
    form->addRow(this->label_orbital_info);
    layout->addWidget(group);

    // connections
    connect(this->combo_set, &QComboBox::currentIndexChanged, this, [this]() {
        this->fill_orbital_table();
    });
    connect(this->button_gallery, &QPushButton::clicked, this, &ResultsPanel::gallery_requested);
    connect(this->button_localize, &QPushButton::clicked, this, &ResultsPanel::localization_requested);
    connect(this->table_orbitals, &QTableWidget::itemSelectionChanged, this, [this]() {
        this->update_contributions();
        this->emit_orbital_selection();
    });
    connect(this->check_show, &QCheckBox::toggled, this, [this]() {
        this->emit_orbital_selection();
    });
    connect(this->radio_auto, &QRadioButton::toggled, this, [this](bool automatic) {
        this->spin_fraction->setEnabled(automatic);
        this->spin_isovalue->setEnabled(!automatic);
        emit isovalue_changed((float)this->spin_isovalue->value(), automatic);
    });
    connect(this->spin_fraction, &QSpinBox::valueChanged, this, [this](int v) {
        emit fraction_changed(v / 100.0f);
    });
    connect(this->spin_isovalue, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if(this->radio_manual->isChecked()) {
            emit isovalue_changed((float)v, false);
        }
    });
    connect(this->combo_grid, &QComboBox::currentIndexChanged, this, [this]() {
        emit grid_spacing_changed((float)this->combo_grid->currentData().toDouble());
    });
    connect(this->slider_opacity, &QSlider::valueChanged, this, [this](int v) {
        emit opacity_changed(v / 100.0f);
    });

    return w;
}

QWidget* ResultsPanel::build_diagram_tab() {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    this->diagram = new MODiagramWidget;
    layout->addWidget(this->diagram, 1);
    this->check_all_levels = new QCheckBox("Show all orbitals (including core and high-lying virtual orbitals)");
    layout->addWidget(this->check_all_levels);

    connect(this->check_all_levels, &QCheckBox::toggled, this->diagram, &MODiagramWidget::set_show_all);
    connect(this->diagram, &MODiagramWidget::orbital_clicked, this, [this](int column, int orbital) {
        this->select_orbital(column, orbital);
    });
    return w;
}

QWidget* ResultsPanel::build_populations_tab() {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    auto* info = new QLabel("Partial atomic charges (in units of the elementary charge) obtained by "
                            "dividing the electron density over the atoms.");
    info->setWordWrap(true);
    layout->addWidget(info);
    this->table_populations = new QTableWidget;
    this->table_populations->setColumnCount(4);
    this->table_populations->setHorizontalHeaderLabels({"#", "Element", "Mulliken", "Löwdin"});
    this->table_populations->verticalHeader()->setVisible(false);
    this->table_populations->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->table_populations->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(this->table_populations, 1);

    auto* bonding = new QGroupBox("Bonding between two atoms");
    auto* lbonding = new QVBoxLayout(bonding);
    auto* binfo = new QLabel("Which molecular orbitals bond or antibond a pair of atoms? The orbital-resolved "
                             "Hamilton, overlap and bond-index populations (MOHP, MOOP, MOBI) answer this.");
    binfo->setWordWrap(true);
    lbonding->addWidget(binfo);
    this->button_bonding = new QPushButton(bluecurve_icon("accessories-calculator"), "Orbital bonding analysis...");
    lbonding->addWidget(this->button_bonding, 0, Qt::AlignLeft);
    layout->addWidget(bonding);
    connect(this->button_bonding, &QPushButton::clicked, this, &ResultsPanel::bonding_analysis_requested);
    return w;
}

QWidget* ResultsPanel::build_matrices_tab() {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    auto* lsel = new QHBoxLayout;
    lsel->addWidget(new QLabel("Matrix:"));
    this->combo_matrix = new QComboBox;
    this->combo_matrix->setObjectName("matrix_selector");
    lsel->addWidget(this->combo_matrix, 1);
    this->button_export_matrix = new QPushButton("Export...");
    this->button_export_matrix->setObjectName("export_matrix_button");
    this->button_export_matrix->setEnabled(false);
    this->button_export_matrix->setToolTip("Save the selected matrix as Excel, CSV or plain text at full precision.");
    lsel->addWidget(this->button_export_matrix);
    connect(this->button_export_matrix, &QPushButton::clicked, this, &ResultsPanel::export_selected_matrix);
    layout->addLayout(lsel);

    this->table_matrix = new QTableWidget;
    this->table_matrix->setObjectName("matrix_table");
    this->table_matrix->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->table_matrix->horizontalHeader()->setDefaultSectionSize(72);
    layout->addWidget(this->table_matrix, 1);

    this->label_matrix_info = new QLabel;
    this->label_matrix_info->setWordWrap(true);
    layout->addWidget(this->label_matrix_info);

    connect(this->combo_matrix, &QComboBox::currentIndexChanged, this, [this]() {
        this->fill_matrix();
    });
    return w;
}

QWidget* ResultsPanel::build_optimization_tab() {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    this->plot_optimization = new LinePlotWidget;
    this->plot_optimization->set_labels("Energy during optimization", "Energy evaluation", "Energy (Ht)");
    this->plot_optimization->set_x_offset(1);
    layout->addWidget(this->plot_optimization, 1);

    auto* lslider = new QHBoxLayout;
    lslider->addWidget(new QLabel("Geometry:"));
    this->slider_frame = new QSlider(Qt::Horizontal);
    lslider->addWidget(this->slider_frame, 1);
    layout->addLayout(lslider);
    this->label_frame = new QLabel;
    this->label_frame->setWordWrap(true);
    layout->addWidget(this->label_frame);

    connect(this->slider_frame, &QSlider::valueChanged, this, [this](int frame) {
        if(!this->result || !this->result->optimization) {
            return;
        }
        const auto& opt = *this->result->optimization;
        this->plot_optimization->set_highlight(frame);
        this->label_frame->setText(QString("Evaluation %1 of %2: E = %3 Ht")
                                   .arg(frame + 1).arg(opt.energies.size())
                                   .arg(fmt(opt.energies[frame], 8)));
        emit trajectory_frame_selected(frame);
    });
    connect(this->plot_optimization, &LinePlotWidget::point_clicked, this->slider_frame, &QSlider::setValue);
    return w;
}

// ---------------------------------------------------------------------------
// filling the tabs
// ---------------------------------------------------------------------------
void ResultsPanel::clear() {
    this->result.reset();
    this->job_dir.clear();

    this->summary->setHtml("<p><i>No results yet.</i></p>"
                           "<p>Load a molecule, choose the settings on the left and press "
                           "<b>Run calculation</b>. Results of earlier calculations can be "
                           "opened via <b>File &rarr; Open result</b>.</p>");
    {
        QSignalBlocker b1(this->combo_set);
        QSignalBlocker b2(this->combo_matrix);
        this->combo_set->clear();
        this->combo_matrix->clear();
    }
    this->table_orbitals->setRowCount(0);
    this->table_populations->setRowCount(0);
    this->table_matrix->clear();
    this->table_matrix->setRowCount(0);
    this->table_matrix->setColumnCount(0);
    this->diagram->clear();
    this->plot_optimization->clear();
    this->label_contributions->clear();
    this->label_orbital_info->clear();
    this->label_matrix_info->clear();
    this->button_export_matrix->setEnabled(false);
    this->button_gallery->setEnabled(false);
    this->button_bonding->setEnabled(false);
    this->button_localize->setEnabled(false);

    for(int i = 1; i < this->tabs->count(); ++i) {
        this->tabs->setTabEnabled(i, false);
    }
    this->tabs->setCurrentIndex(0);
}

void ResultsPanel::set_result(std::shared_ptr<JobResult> _result, const QString& _job_dir) {
    this->clear();
    this->result = std::move(_result);
    this->job_dir = _job_dir;

    for(int i = 1; i < this->tabs->count(); ++i) {
        this->tabs->setTabEnabled(i, true);
    }
    this->tabs->setTabEnabled(this->tabs->indexOf(this->tab_optimization), (bool)this->result->optimization);
    this->button_gallery->setEnabled(true);
    this->button_bonding->setEnabled(this->result->positions.size() >= 2);

    this->fill_summary();
    this->fill_diagram();
    this->fill_populations();
    this->fill_optimization();

    {
        QSignalBlocker blocker(this->combo_matrix);
        for(const auto& m : this->result->matrices) {
            this->combo_matrix->addItem(m.second.label, m.first);
        }
    }
    this->fill_matrix();

    {
        QSignalBlocker blocker(this->combo_set);
        for(const auto& set : this->result->orbital_sets) {
            this->combo_set->addItem(set.label);
        }
    }
    this->fill_orbital_table();
    this->update_localize_button();
}

void ResultsPanel::set_localization_available(bool available) {
    this->localization_available = available;
    this->update_localize_button();
}

void ResultsPanel::update_localize_button() {
    const bool restricted = this->result && !this->result->is_unrestricted();
    const bool localized = this->result && this->result->find_orbital_set("Foster-Boys") >= 0;
    this->button_localize->setVisible(!this->result || restricted);
    this->button_localize->setEnabled(restricted && !localized && this->localization_available);
    if(localized) {
        this->button_localize->setToolTip("The Foster-Boys localized orbitals are available in the list of orbitals.");
    } else if(!this->localization_available) {
        this->button_localize->setToolTip("Localization requires the Python environment and no running calculation.");
    } else {
        this->button_localize->setToolTip("Construct Foster-Boys localized orbitals from the occupied orbitals.\n"
                                          "The SCF calculation is not repeated.");
    }
}

void ResultsPanel::fill_summary() {
    const JobResult& r = *this->result;
    const QString method = r.method_label();
    const QString type = r.optimization ? "geometry optimization" : "single point";
    const QString basis = r.job.contains("basis") ? JobSpec::basis_set_label(r.job["basis"].toString()) : QString("unknown");

    QString html;
    html += QString("<h2>%1 <small>(%2)</small></h2>").arg(r.molecule_name.toHtmlEscaped(), r.get_molecule().formula_html());
    html += QString("<p>%1 %2 &middot; basis set <b>%3</b><br>").arg(method, type, basis.toHtmlEscaped());
    html += QString("charge %1 &middot; multiplicity %2 &middot; %3 electrons").arg(r.charge).arg(r.multiplicity).arg(r.nelec);
    if(r.is_unrestricted()) {
        html += QString(" (%1 &alpha;, %2 &beta;)").arg(r.nalpha).arg(r.nbeta);
    }
    html += QString(" &middot; %1 basis functions</p>").arg(r.basis->size());

    const double e = r.total_energy();
    html += QString("<h3>Total energy</h3><p style='font-size:large'><b>%1 Ht</b><br>"
                    "= %2 eV = %3 kJ/mol</p>")
        .arg(fmt(e, 8), fmt(e * HARTREE_TO_EV, 4), fmt(e * HARTREE_TO_KJMOL, 2));

    html += "<h3>Energy decomposition</h3><table cellspacing='0' cellpadding='3'>";
    for(const auto& comp : r.energy_components) {
        if(comp.first == "energy") {
            continue;
        }
        html += QString("<tr><td>%1</td><td align='right'>%2 Ht</td></tr>")
            .arg(JobResult::energy_component_label(comp.first), fmt(comp.second, 8));
    }
    html += "</table>";

    // SCF convergence
    if(!r.scf_energies.empty()) {
        html += QString("<h3>SCF</h3><p>%1 iterations").arg(r.scf_energies.size());
        if(r.scf_energies.size() >= 2) {
            const double de = r.scf_energies[r.scf_energies.size() - 1] - r.scf_energies[r.scf_energies.size() - 2];
            html += QString("; last energy change %1 Ht").arg(QString::number(de, 'e', 2));
        }
        html += "</p>";
    }
    if(!r.converged) {
        html += "<p style='color:#c62828'><b>The SCF did not converge</b> within the maximum number of "
                "iterations; the energy and orbitals are not reliable.</p>";
    }

    // frontier orbitals
    QString frontier;
    for(const auto& set : r.orbital_sets) {
        if(set.label.startsWith("Foster")) {
            continue;
        }
        const int homo = set.homo();
        if(homo < 0 || homo + 1 >= (int)set.size()) {
            continue;
        }
        const double eh = set.energies[homo];
        const double el = set.energies[homo + 1];
        frontier += QString("<tr><td>%1</td><td align='right'>%2</td><td align='right'>%3</td><td align='right'>%4</td></tr>")
            .arg(set.label.toHtmlEscaped(), fmt(eh, 5), fmt(el, 5), fmt((el - eh) * HARTREE_TO_EV, 3));
    }
    if(!frontier.isEmpty()) {
        html += "<h3>Frontier orbitals</h3><table cellspacing='0' cellpadding='3'>"
                "<tr><th align='left'>Orbitals</th><th>HOMO (Ht)</th><th>LUMO (Ht)</th><th>Gap (eV)</th></tr>" +
                frontier + "</table>";
    }

    if(r.localization.present) {
        html += QString("<h3>Foster-Boys localization</h3><p>Converged in %1 iterations; "
                        "sum of squared orbital centroids increased from %2 to %3.</p>")
            .arg(r.localization.iterations).arg(fmt(r.localization.r2start, 4), fmt(r.localization.r2final, 4));
    }

    if(r.optimization) {
        const auto& opt = *r.optimization;
        html += QString("<h3>Geometry optimization</h3><p>%1 energy evaluations; "
                        "energy lowered by %2 kJ/mol.<br>Optimizer: %3</p>")
            .arg(opt.energies.size())
            .arg(fmt((opt.energies.front() - opt.energies.back()) * HARTREE_TO_KJMOL, 2))
            .arg(opt.message.toHtmlEscaped());
    }

    if(!r.timing.empty()) {
        html += "<h3>Timing</h3><table cellspacing='0' cellpadding='3'>";
        for(const auto& t : r.timing) {
            QString label = t.first;
            label.replace('_', ' ');
            html += QString("<tr><td>%1</td><td align='right'>%2 s</td></tr>").arg(label, fmt(t.second, 3));
        }
        html += "</table>";
    }

    QString programs = QString("PyQInt %1").arg(r.pyqint_version);
    if(!r.pydft_version.isEmpty()) {
        programs += QString(" &middot; PyDFT %1").arg(r.pydft_version);
    }
    html += QString("<p style='color:gray'><small>%1 &middot; Python %2 &middot; %3")
        .arg(programs, r.python_version, r.created);
    if(!this->job_dir.isEmpty()) {
        html += QString("<br><a href='%1'>Open job folder</a> (contains the Python script, the output and all results)")
            .arg(QUrl::fromLocalFile(this->job_dir).toString());
    }
    html += "</small></p>";

    this->summary->setHtml(html);
}

void ResultsPanel::fill_orbital_table() {
    this->table_orbitals->setRowCount(0);
    this->label_contributions->clear();
    const int s = this->combo_set->currentIndex();
    if(!this->result || s < 0) {
        return;
    }

    const OrbitalSet& set = this->result->orbital_sets[s];
    QSignalBlocker blocker(this->table_orbitals);
    this->table_orbitals->setRowCount((int)set.size());
    for(int i = 0; i < (int)set.size(); ++i) {
        auto* idx = new QTableWidgetItem(QString::number(i + 1));
        idx->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        this->table_orbitals->setItem(i, 0, idx);
        this->table_orbitals->setItem(i, 1, text_item(set.frontier_label(i)));
        this->table_orbitals->setItem(i, 2, number_item(set.energies[i], 5));
        this->table_orbitals->setItem(i, 3, number_item(set.energies[i] * HARTREE_TO_EV, 3));
        this->table_orbitals->setItem(i, 4, number_item(set.occupations[i], 0));

        if(set.occupations[i] > 0.0) {
            QFont f = this->table_orbitals->font();
            f.setBold(true);
            for(int c = 0; c < 5; ++c) {
                this->table_orbitals->item(i, c)->setFont(f);
            }
        }
    }
    blocker.unblock();

    // select the HOMO by default
    const int homo = std::max(0, set.homo());
    this->table_orbitals->selectRow(homo);
    this->table_orbitals->scrollToItem(this->table_orbitals->item(homo, 0), QAbstractItemView::PositionAtCenter);
}

int ResultsPanel::current_set() const {
    return this->combo_set->currentIndex();
}

int ResultsPanel::current_orbital() const {
    const auto rows = this->table_orbitals->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.front().row();
}

void ResultsPanel::select_orbital(int set_index, int orbital_index) {
    if(!this->result || set_index < 0 || set_index >= this->combo_set->count()) {
        return;
    }
    if(this->combo_set->currentIndex() != set_index) {
        this->combo_set->setCurrentIndex(set_index);
    }
    this->table_orbitals->selectRow(orbital_index);
    this->table_orbitals->scrollToItem(this->table_orbitals->item(orbital_index, 0));
}

void ResultsPanel::show_tab(const QString& name) {
    const QStringList names = {"summary", "orbitals", "diagram", "charges", "matrices", "optimization"};
    const int idx = names.indexOf(name.toLower());
    if(idx >= 0 && this->tabs->isTabEnabled(idx)) {
        this->tabs->setCurrentIndex(idx);
    }
}

void ResultsPanel::emit_orbital_selection() {
    const int s = this->current_set();
    const int o = this->current_orbital();
    this->diagram->set_selected(s, o);
    if(s >= 0 && o >= 0 && this->check_show->isChecked()) {
        this->label_orbital_info->setText("Evaluating orbital...");
        emit orbital_selected(s, o);
    } else {
        this->label_orbital_info->clear();
        emit orbital_hidden();
    }
}

void ResultsPanel::update_contributions() {
    const int s = this->current_set();
    const int o = this->current_orbital();
    if(!this->result || s < 0 || o < 0) {
        this->label_contributions->clear();
        return;
    }

    // largest contributions of basis functions to the orbital
    const auto& coeff = this->result->orbital_sets[s].coefficients[o];
    std::vector<size_t> idx(coeff.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        return std::abs(coeff[a]) > std::abs(coeff[b]);
    });

    QStringList parts;
    for(size_t k = 0; k < std::min<size_t>(5, idx.size()); ++k) {
        const size_t i = idx[k];
        if(std::abs(coeff[i]) < 0.05) {
            break;
        }
        parts << QString("%1&nbsp;%2").arg(QString::number(coeff[i], 'f', 3),
                                           QString::fromStdString((*this->result->basis)[i].label).toHtmlEscaped());
    }
    this->label_contributions->setText(QString("<b>Largest contributions:</b> %1").arg(parts.join(", ")));
}

void ResultsPanel::on_orbital_shown(float isovalue, int nr_triangles) {
    {
        QSignalBlocker blocker(this->spin_isovalue);
        this->spin_isovalue->setValue(isovalue);
    }
    this->label_orbital_info->setText(nr_triangles > 0 ?
        QString("Isovalue ±%1 (%2 triangles)").arg(isovalue, 0, 'f', 4).arg(nr_triangles) :
        QString("The isosurface is empty; try a smaller isovalue."));
}

void ResultsPanel::fill_diagram() {
    std::vector<MODiagramWidget::Column> cols;
    for(const auto& set : this->result->orbital_sets) {
        cols.push_back({set.label, set.spin, set.energies, set.occupations});
    }
    this->diagram->set_columns(cols);
}

void ResultsPanel::fill_populations() {
    const JobResult& r = *this->result;
    this->table_populations->setRowCount((int)r.elements.size());
    double sum_m = 0.0, sum_l = 0.0;
    for(int i = 0; i < (int)r.elements.size(); ++i) {
        auto* idx = new QTableWidgetItem(QString::number(i + 1));
        idx->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        this->table_populations->setItem(i, 0, idx);
        this->table_populations->setItem(i, 1, text_item(r.elements[i]));
        if(i < (int)r.mulliken.size()) {
            this->table_populations->setItem(i, 2, number_item(r.mulliken[i], 4));
            sum_m += r.mulliken[i];
        }
        if(i < (int)r.lowdin.size()) {
            this->table_populations->setItem(i, 3, number_item(r.lowdin[i], 4));
            sum_l += r.lowdin[i];
        }
    }

    // total row
    const int row = this->table_populations->rowCount();
    this->table_populations->insertRow(row);
    QFont bold = this->table_populations->font();
    bold.setBold(true);
    auto* total = text_item("Total");
    total->setFont(bold);
    this->table_populations->setItem(row, 1, total);
    auto* tm = number_item(sum_m, 4);
    auto* tl = number_item(sum_l, 4);
    tm->setFont(bold);
    tl->setFont(bold);
    this->table_populations->setItem(row, 2, tm);
    this->table_populations->setItem(row, 3, tl);
}

void ResultsPanel::fill_matrix() {
    this->table_matrix->clear();
    const int m = this->combo_matrix->currentIndex();
    this->button_export_matrix->setEnabled(false);
    if(!this->result || m < 0 || m >= (int)this->result->matrices.size()) {
        this->table_matrix->setRowCount(0);
        this->table_matrix->setColumnCount(0);
        return;
    }

    const DenseMatrix& mat = this->result->matrices[m].second;
    const QString& key = this->result->matrices[m].first;
    const bool coefficients = key.startsWith("coefficients_");
    this->button_export_matrix->setEnabled(mat.rows > 0 && mat.cols > 0);
    this->table_matrix->setRowCount(mat.rows);
    this->table_matrix->setColumnCount(mat.cols);

    QStringList labels;
    for(size_t i = 0; i < this->result->basis->size(); ++i) {
        labels << QString::fromStdString((*this->result->basis)[i].label);
    }
    if((int)labels.size() == mat.rows) {
        this->table_matrix->setVerticalHeaderLabels(labels);
    }
    if(coefficients) {
        QStringList orbitals;
        for(int col = 0; col < mat.cols; ++col) {
            orbitals << QString("MO %1").arg(col + 1);
        }
        this->table_matrix->setHorizontalHeaderLabels(orbitals);
    } else if((int)labels.size() == mat.cols && key != "transform") {
        this->table_matrix->setHorizontalHeaderLabels(labels);
    }

    double maxabs = 1e-12;
    for(double v : mat.data) {
        maxabs = std::max(maxabs, std::abs(v));
    }

    for(int i = 0; i < mat.rows; ++i) {
        for(int j = 0; j < mat.cols; ++j) {
            const double v = mat(i, j);
            auto* item = number_item(v, 4);
            const int alpha = (int)(150.0 * std::min(1.0, std::abs(v) / maxabs));
            item->setBackground(v >= 0 ? QColor(30, 120, 220, alpha) : QColor(230, 75, 50, alpha));
            item->setToolTip(QString("%1 = %2").arg(this->result->matrices[m].first).arg(v, 0, 'g', 12));
            this->table_matrix->setItem(i, j, item);
        }
    }

    const std::map<QString, QString> info = {
        {"overlap", "Sᵢⱼ = ⟨φᵢ|φⱼ⟩: overlap between basis functions."},
        {"kinetic", "Tᵢⱼ: kinetic energy integrals."},
        {"nuclear", "Vᵢⱼ: attraction between electrons and all nuclei."},
        {"hcore", "H = T + V: one-electron (core) Hamiltonian."},
        {"transform", "X: transformation to an orthonormal basis (XᵀSX = I)."},
        {"hartree", "Jᵢⱼ: Coulomb (Hartree) repulsion with the electron density, integrated on the DFT grid."},
        {"xc", "Vxcᵢⱼ: exchange-correlation potential of the functional, integrated on the DFT grid."},
        {"fock", this->result->is_dft() ? "F = H + J + Vxc: Kohn-Sham matrix at convergence."
                                        : "F = H + G(P): Fock matrix at convergence."},
        {"density", "P: density matrix; the diagonal of PS gives the Mulliken populations."},
    };
    auto it = info.find(key);
    this->label_matrix_info->setText(coefficients
        ? QString("C: molecular orbital coefficients in ψᵢ = Σμ Cμᵢ φμ. "
                  "Rows are basis functions; columns are molecular orbitals.")
        : (it != info.end() ? it->second : QString()));
}

void ResultsPanel::export_selected_matrix() {
    const int index = this->combo_matrix->currentIndex();
    const auto export_result = this->result;
    if(!export_result || index < 0 || index >= int(export_result->matrices.size())) {
        return;
    }

    const QString excel_filter = "Excel workbook (*.xlsx)";
    const QString csv_filter = "CSV file (*.csv)";
    const QString text_filter = "Plain text file (*.txt)";
    QString selected_filter = excel_filter;
    const QString suggested = QDir(this->job_dir).filePath(export_result->matrices[index].first);
    QString path = QFileDialog::getSaveFileName(this, "Export matrix", suggested,
                                              excel_filter + ";;" + csv_filter + ";;" + text_filter,
                                              &selected_filter);
    if(path.isEmpty()) {
        return;
    }

    MatrixExportFormat format = MatrixExportFormat::Excel;
    QString extension = "xlsx";
    if(selected_filter == csv_filter) {
        format = MatrixExportFormat::Csv;
        extension = "csv";
    } else if(selected_filter == text_filter) {
        format = MatrixExportFormat::Text;
        extension = "txt";
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    // An explicitly typed supported extension takes precedence over the filter.
    if(suffix == "xlsx") {
        format = MatrixExportFormat::Excel;
    } else if(suffix == "csv") {
        format = MatrixExportFormat::Csv;
    } else if(suffix == "txt") {
        format = MatrixExportFormat::Text;
    } else {
        path += "." + extension;
        if(QFileInfo::exists(path) &&
           QMessageBox::question(this, "Replace file?", "The file already exists. Replace it?",
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }
    try {
        export_matrix(export_result->matrices[index].second, path, format);
    } catch(const std::exception& error) {
        QMessageBox::warning(this, "Cannot export matrix", QString::fromUtf8(error.what()));
    }
}

void ResultsPanel::fill_optimization() {
    if(!this->result->optimization) {
        return;
    }
    const auto& opt = *this->result->optimization;
    this->plot_optimization->set_data(opt.energies);
    QSignalBlocker blocker(this->slider_frame);
    this->slider_frame->setRange(0, std::max(0, (int)opt.frames.size() - 1));
    this->slider_frame->setValue(this->slider_frame->maximum());
    this->plot_optimization->set_highlight(this->slider_frame->maximum());
    this->label_frame->setText("Drag the slider (or click in the graph) to show the geometry at each step. "
                               "Orbitals are only available for the final geometry.");
}
