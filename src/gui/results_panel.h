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

#pragma once

#include <memory>

#include <QWidget>

#include "calculation/job_result.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTextBrowser;
class LinePlotWidget;
class MODiagramWidget;

/**
 * @brief Tabs presenting the results of a calculation
 */
class ResultsPanel : public QWidget {
    Q_OBJECT

private:
    std::shared_ptr<JobResult> result;
    QString job_dir;

    QTabWidget* tabs;

    // summary
    QTextBrowser* summary;

    // orbitals
    QWidget* tab_orbitals;
    QComboBox* combo_set;
    QPushButton* button_gallery;
    QPushButton* button_localize;
    bool localization_available = false;
    QTableWidget* table_orbitals;
    QCheckBox* check_show;
    QRadioButton* radio_auto;
    QRadioButton* radio_manual;
    QSpinBox* spin_fraction;
    QDoubleSpinBox* spin_isovalue;
    QComboBox* combo_grid;
    QSlider* slider_opacity;
    QLabel* label_orbital_info;
    QLabel* label_contributions;

    // MO diagram
    QWidget* tab_diagram;
    MODiagramWidget* diagram;
    QCheckBox* check_all_levels;

    // populations
    QWidget* tab_populations;
    QTableWidget* table_populations;
    QPushButton* button_bonding;

    // matrices
    QWidget* tab_matrices;
    QComboBox* combo_matrix;
    QPushButton* button_export_matrix;
    QTableWidget* table_matrix;
    QLabel* label_matrix_info;

    // optimization
    QWidget* tab_optimization;
    LinePlotWidget* plot_optimization;
    QSlider* slider_frame;
    QLabel* label_frame;

public:
    explicit ResultsPanel(QWidget* parent = nullptr);

    void set_result(std::shared_ptr<JobResult> result, const QString& job_dir);
    void clear();

    inline const std::shared_ptr<JobResult>& get_result() const {
        return this->result;
    }

    inline const QString& get_job_dir() const {
        return this->job_dir;
    }

    /**
     * @brief Select an orbital in the table (e.g. the HOMO after a new result)
     */
    void select_orbital(int set_index, int orbital_index);

    /**
     * @brief Show a tab by name (summary, orbitals, diagram, charges, matrices, optimization)
     */
    void show_tab(const QString& name);

    /**
     * @brief Index of the orbital set shown in the orbitals tab
     */
    int current_set() const;

    /**
     * @brief Whether orbitals can be localized now (Python ready, no job running)
     */
    void set_localization_available(bool available);

public slots:
    void on_orbital_shown(float isovalue, int nr_triangles);

signals:
    void orbital_selected(int set_index, int orbital_index);
    void orbital_hidden();
    void isovalue_changed(float isovalue, bool automatic);
    void fraction_changed(float fraction);
    void grid_spacing_changed(float spacing);
    void opacity_changed(float opacity);
    void trajectory_frame_selected(int frame);
    void gallery_requested();
    void localization_requested();
    void bonding_analysis_requested();

private:
    QWidget* build_summary_tab();
    QWidget* build_orbitals_tab();
    QWidget* build_diagram_tab();
    QWidget* build_populations_tab();
    QWidget* build_matrices_tab();
    QWidget* build_optimization_tab();

    void fill_summary();
    void fill_orbital_table();
    void fill_diagram();
    void fill_populations();
    void fill_matrix();
    void export_selected_matrix();
    void fill_optimization();

    void update_contributions();
    void emit_orbital_selection();
    void update_localize_button();
    int current_orbital() const;
};
