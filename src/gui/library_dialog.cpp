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

#include "library_dialog.h"

#include <algorithm>
#include <stdexcept>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int ROLE_HTML = Qt::UserRole + 1;     // rich text (formula with subscripts)

/**
 * @brief Draws rich text (subscripts) and uses white text on a dark selection
 */
class LibraryItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        this->initStyleOption(&opt, index);

        const QString html = index.data(ROLE_HTML).toString();
        const QString text = html.isEmpty() ? opt.text.toHtmlEscaped() : html;
        const Qt::Alignment align = opt.displayAlignment;

        // background, selection and focus from the style, the text ourselves
        opt.text.clear();
        const QWidget* w = opt.widget;
        QStyle* style = w != nullptr ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

        QColor color = opt.palette.color(QPalette::Text);
        if(opt.state & QStyle::State_Selected) {
            const QPalette::ColorGroup group = (opt.state & QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
            const QColor highlight = opt.palette.color(group, QPalette::Highlight);
            if(highlight.lightnessF() < 0.6) {
                color = Qt::white;
            }
        }

        QTextDocument doc;
        doc.setDocumentMargin(0);
        doc.setDefaultFont(opt.font);
        doc.setHtml(QString("<span style='color:%1'>%2</span>").arg(color.name(), text));

        const QRect r = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, w).adjusted(3, 0, -3, 0);
        const QSizeF size = doc.size();
        const double x = (align & Qt::AlignRight) ? r.right() - size.width() : r.left();
        const double y = r.top() + (r.height() - size.height()) / 2.0;

        painter->save();
        painter->setClipRect(r);
        painter->translate(x, y);
        doc.drawContents(painter);
        painter->restore();
    }
};

} // namespace

LibraryDialog::LibraryDialog(QWidget* parent) :
    QDialog(parent) {

    this->setWindowTitle("Molecule library");
    this->resize(460, 520);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Select a molecule to load. Small molecules calculate fastest."));

    this->filter = new QLineEdit;
    this->filter->setPlaceholderText("Filter by name or formula...");
    this->filter->setClearButtonEnabled(true);
    layout->addWidget(this->filter);

    this->list = new QTreeWidget;
    this->list->setColumnCount(3);
    this->list->setHeaderLabels({"Name", "Formula", "Atoms"});
    this->list->setRootIsDecorated(false);
    this->list->setAlternatingRowColors(true);
    this->list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    this->list->setItemDelegate(new LibraryItemDelegate(this->list));
    layout->addWidget(this->list, 1);

    this->molecules = load_library();
    for(size_t i = 0; i < this->molecules.size(); ++i) {
        const Molecule& mol = this->molecules[i];
        auto* item = new QTreeWidgetItem({mol.get_name(), mol.formula(), QString::number(mol.size())});
        item->setData(0, Qt::UserRole, (int)i);
        item->setData(1, ROLE_HTML, mol.formula_html());
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        this->list->addTopLevelItem(item);
    }
    if(this->list->topLevelItemCount() > 0) {
        this->list->setCurrentItem(this->list->topLevelItem(0));
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this->list, &QTreeWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(this->filter, &QLineEdit::textChanged, this, &LibraryDialog::apply_filter);
}

std::vector<Molecule> LibraryDialog::load_library() {
    std::vector<Molecule> result;
    const QDir dir(":/assets/molecules");
    for(const QString& f : dir.entryList({"*.xyz"}, QDir::Files, QDir::Name)) {
        try {
            result.push_back(Molecule::from_xyz_file(dir.filePath(f)));
        } catch(const std::exception& e) {
            qWarning() << "Cannot load library molecule" << f << ":" << e.what();
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const Molecule& a, const Molecule& b) {
        return QString::compare(a.get_name(), b.get_name(), Qt::CaseInsensitive) < 0;
    });
    return result;
}

Molecule LibraryDialog::selected_molecule() const {
    const auto* item = this->list->currentItem();
    if(item == nullptr || item->isHidden()) {
        return Molecule();
    }
    return this->molecules[item->data(0, Qt::UserRole).toInt()];
}

void LibraryDialog::apply_filter(const QString& text) {
    QTreeWidgetItem* first_visible = nullptr;
    for(int i = 0; i < this->list->topLevelItemCount(); ++i) {
        auto* item = this->list->topLevelItem(i);
        const bool match = text.isEmpty() ||
                           item->text(0).contains(text, Qt::CaseInsensitive) ||
                           item->text(1).contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
        if(match && first_visible == nullptr) {
            first_visible = item;
        }
    }
    if(first_visible != nullptr && (this->list->currentItem() == nullptr || this->list->currentItem()->isHidden())) {
        this->list->setCurrentItem(first_visible);
    }
}
