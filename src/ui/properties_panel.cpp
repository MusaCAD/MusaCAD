#include "musacad/ui/properties_panel.hpp"

#include <array>
#include <cstdio>
#include <string>

#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>

#include "musacad/core/entity_handle.hpp"

namespace musacad::ui {

using core::PropEditor;
using core::PropertyField;
using core::PropertyId;
using core::PropertyValue;

namespace {

// AutoCAD standard lineweight ladder (hundredths of a mm); index 0 of the combo
// is "ByLayer", the rest map to these.
constexpr std::array<int, 10> kLwLadder = {0, 9, 13, 18, 25, 30, 50, 70, 100, 200};

QString lw_label(int hundredths) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f mm", hundredths / 100.0);
    return QString::fromLatin1(buf);
}

const char* kVaries = "*VARIES*";

} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("PropertiesPanel"));
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(0, 0, 0, 0);
    rebuild();
}

bool PropertiesPanel::has_field(PropertyId id) const {
    for (const PropertyField& f : summary_.fields) {
        if (f.id == id) {
            return true;
        }
    }
    return false;
}
bool PropertiesPanel::field_varies(PropertyId id) const {
    for (const PropertyField& f : summary_.fields) {
        if (f.id == id) {
            return f.varies;
        }
    }
    return false;
}
core::PropertyValue PropertiesPanel::field_value(PropertyId id) const {
    for (const PropertyField& f : summary_.fields) {
        if (f.id == id) {
            return f.value;
        }
    }
    return {};
}
void PropertiesPanel::test_commit(PropertyId id, const PropertyValue& v) { emit_edit(id, v); }

QWidget* PropertiesPanel::editor_widget(PropertyId id) const {
    const auto it = editors_.find(static_cast<int>(id));
    return it == editors_.end() ? nullptr : it->second;
}

void PropertiesPanel::emit_edit(PropertyId id, const PropertyValue& v) {
    if (edit_cb_) {
        edit_cb_(id, v);
    }
}

void PropertiesPanel::update_view(const core::SelectionSummary& summary,
                                  const std::vector<core::Layer>& layers,
                                  std::uint16_t current_layer) {
    if (have_view_ && summary == summary_ && layers == layers_ && current_layer == current_layer_) {
        return; // nothing changed -> no rebuild
    }
    summary_ = summary;
    layers_ = layers;
    current_layer_ = current_layer;
    have_view_ = true;
    rebuild();
}

void PropertiesPanel::rebuild() {
    editors_.clear();
    if (body_ != nullptr) {
        root_->removeWidget(body_);
        body_->deleteLater();
        body_ = nullptr;
    }
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(6, 6, 6, 6);
    col->setSpacing(8);

    // Header: the selection's type + count.
    auto* header = new QLabel(content);
    header->setObjectName(QStringLiteral("PropertiesHeader"));
    if (summary_.count == 0) {
        const QString layer = current_layer_ < layers_.size()
                                  ? QString::fromStdString(layers_[current_layer_].name)
                                  : QString();
        header->setText(QStringLiteral("No selection  (current layer: %1)").arg(layer));
    } else {
        header->setText(QString::fromStdString(summary_.type_label));
    }
    col->addWidget(header);

    // Group the fields and lay each out under its group box (in first-seen order).
    QFormLayout* current_form = nullptr;
    std::string current_group;
    for (const PropertyField& f : summary_.fields) {
        if (current_form == nullptr || f.group != current_group) {
            auto* box = new QGroupBox(QString::fromStdString(f.group), content);
            current_form = new QFormLayout(box);
            col->addWidget(box);
            current_group = f.group;
        }
        const QString label = QString::fromStdString(f.label);
        const PropertyId id = f.id;
        const bool varies = f.varies;

        switch (f.editor) {
        case PropEditor::ReadOnly: {
            current_form->addRow(label,
                                 new QLabel(varies ? QString::fromLatin1(kVaries)
                                                   : QString::fromStdString(f.value.text)));
            break;
        }
        case PropEditor::Number: {
            auto* e = new QLineEdit();
            e->setValidator(new QDoubleValidator(e));
            if (varies) {
                e->setPlaceholderText(QString::fromLatin1(kVaries));
            } else {
                e->setText(QString::number(f.value.num, 'g', 6));
            }
            connect(e, &QLineEdit::editingFinished, this, [this, id, e] {
                bool ok = false;
                const double v = e->text().toDouble(&ok);
                if (ok) {
                    PropertyValue pv;
                    pv.num = v;
                    emit_edit(id, pv);
                }
            });
            current_form->addRow(label, e);
            editors_[static_cast<int>(id)] = e;
            break;
        }
        case PropEditor::Text: {
            auto* e = new QLineEdit();
            if (varies) {
                e->setPlaceholderText(QString::fromLatin1(kVaries));
            } else {
                e->setText(QString::fromStdString(f.value.text));
            }
            connect(e, &QLineEdit::editingFinished, this, [this, id, e] {
                PropertyValue pv;
                pv.text = e->text().toStdString();
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        case PropEditor::TextContentEdit: {
            const bool multiline = summary_.kind_plus1 ==
                                   static_cast<std::uint8_t>(
                                       static_cast<int>(core::EntityKind::MText) + 1);
            const std::string initial = varies ? std::string() : f.value.text;
            auto* btn = new QPushButton(QStringLiteral("Edit text…"));
            connect(btn, &QPushButton::clicked, this, [this, id, multiline, initial] {
                QDialog dlg(this);
                dlg.setWindowTitle(QStringLiteral("Edit contents"));
                auto* lay = new QVBoxLayout(&dlg);
                QPlainTextEdit* multi = nullptr;
                QLineEdit* single = nullptr;
                if (multiline) {
                    multi = new QPlainTextEdit(QString::fromStdString(initial), &dlg);
                    lay->addWidget(multi);
                    dlg.resize(340, 160);
                } else {
                    single = new QLineEdit(QString::fromStdString(initial), &dlg);
                    lay->addWidget(single);
                }
                auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                                &dlg);
                lay->addWidget(bb);
                connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
                connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
                if (dlg.exec() == QDialog::Accepted) {
                    PropertyValue pv;
                    pv.text = multiline ? multi->toPlainText().toStdString()
                                        : single->text().toStdString();
                    emit_edit(id, pv);
                }
            });
            current_form->addRow(label, btn);
            break;
        }
        case PropEditor::LayerCombo: {
            auto* e = new QComboBox();
            for (const core::Layer& l : layers_) {
                e->addItem(QString::fromStdString(l.name));
            }
            if (varies) {
                e->addItem(QString::fromLatin1(kVaries));
                e->setCurrentIndex(e->count() - 1);
            } else {
                e->setCurrentIndex(f.value.choice);
            }
            connect(e, &QComboBox::activated, this, [this, id](int index) {
                PropertyValue pv;
                pv.choice = index;
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        case PropEditor::ColorOverride: {
            // The "inherited" state is ByLayer for entity colour, ByStyle for a
            // dimension element colour (same flag, different label).
            const QString inherit =
                f.group == "Dimension" ? QStringLiteral("ByStyle") : QStringLiteral("ByLayer");
            auto* btn = new QPushButton();
            if (varies) {
                btn->setText(QString::fromLatin1(kVaries));
            } else if (f.value.flag) {
                btn->setText(inherit);
            } else {
                const core::Rgb c = f.value.color;
                btn->setText(QStringLiteral("%1, %2, %3").arg(c.r).arg(c.g).arg(c.b));
            }
            connect(btn, &QPushButton::clicked, this, [this, id] {
                const QColor picked = QColorDialog::getColor(Qt::white, this,
                                                             QStringLiteral("Select color"));
                if (picked.isValid()) {
                    PropertyValue pv;
                    pv.flag = false;
                    pv.color = {static_cast<std::uint8_t>(picked.red()),
                                static_cast<std::uint8_t>(picked.green()),
                                static_cast<std::uint8_t>(picked.blue())};
                    emit_edit(id, pv);
                }
            });
            // A second control for the inherited (ByLayer/ByStyle) state -> reset.
            auto* reset = new QPushButton(inherit);
            connect(reset, &QPushButton::clicked, this, [this, id] {
                PropertyValue pv;
                pv.flag = true;
                emit_edit(id, pv);
            });
            auto* row = new QWidget();
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(btn);
            h->addWidget(reset);
            current_form->addRow(label, row);
            break;
        }
        case PropEditor::NumberOverride: {
            // A number field + a "ByStyle" reset button. Editing the number sets the
            // override (flag=false); the button resets to ByStyle (flag=true).
            auto* e = new QLineEdit();
            e->setValidator(new QDoubleValidator(e));
            if (varies) {
                e->setPlaceholderText(QString::fromLatin1(kVaries));
            } else {
                e->setText(QString::number(f.value.num, 'g', 6));
                if (f.value.flag) {
                    e->setToolTip(QStringLiteral("ByStyle"));
                }
            }
            connect(e, &QLineEdit::editingFinished, this, [this, id, e] {
                bool ok = false;
                const double v = e->text().toDouble(&ok);
                if (ok) {
                    PropertyValue pv;
                    pv.flag = false; // a typed value sets the override
                    pv.num = v;
                    emit_edit(id, pv);
                }
            });
            auto* reset = new QPushButton(QStringLiteral("ByStyle"));
            connect(reset, &QPushButton::clicked, this, [this, id] {
                PropertyValue pv;
                pv.flag = true; // reset to style
                emit_edit(id, pv);
            });
            auto* row = new QWidget();
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(e);
            h->addWidget(reset);
            current_form->addRow(label, row);
            editors_[static_cast<int>(id)] = e;
            break;
        }
        case PropEditor::DimArrowTypeCombo: {
            auto* e = new QComboBox();
            e->addItems({QStringLiteral("ByStyle"), QStringLiteral("Filled"),
                         QStringLiteral("Tick"), QStringLiteral("Open"), QStringLiteral("Dot")});
            if (varies) {
                e->addItem(QString::fromLatin1(kVaries));
                e->setCurrentIndex(e->count() - 1);
            } else {
                e->setCurrentIndex(f.value.choice);
            }
            connect(e, &QComboBox::activated, this, [this, id](int index) {
                PropertyValue pv;
                pv.choice = index; // 0 = ByStyle
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        case PropEditor::DimPlacementCombo: {
            auto* e = new QComboBox();
            e->addItems({QStringLiteral("ByStyle"), QStringLiteral("Above"),
                         QStringLiteral("Centered")});
            if (varies) {
                e->addItem(QString::fromLatin1(kVaries));
                e->setCurrentIndex(e->count() - 1);
            } else {
                e->setCurrentIndex(f.value.choice);
            }
            connect(e, &QComboBox::activated, this, [this, id](int index) {
                PropertyValue pv;
                pv.choice = index; // 0 = ByStyle
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        case PropEditor::LinetypeCombo: {
            auto* e = new QComboBox();
            e->addItems({QStringLiteral("ByLayer"), QStringLiteral("Continuous"),
                         QStringLiteral("Dashed"), QStringLiteral("Center"),
                         QStringLiteral("Hidden")});
            if (varies) {
                e->addItem(QString::fromLatin1(kVaries));
                e->setCurrentIndex(e->count() - 1);
            } else {
                e->setCurrentIndex(f.value.flag ? 0 : f.value.choice + 1);
            }
            connect(e, &QComboBox::activated, this, [this, id](int index) {
                PropertyValue pv;
                pv.flag = (index == 0);
                pv.choice = index > 0 ? index - 1 : 0;
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        case PropEditor::LineweightCombo: {
            auto* e = new QComboBox();
            e->addItem(QStringLiteral("ByLayer"));
            for (const int hw : kLwLadder) {
                e->addItem(lw_label(hw));
            }
            if (varies) {
                e->addItem(QString::fromLatin1(kVaries));
                e->setCurrentIndex(e->count() - 1);
            } else if (f.value.flag) {
                e->setCurrentIndex(0);
            } else {
                int idx = 1;
                for (std::size_t i = 0; i < kLwLadder.size(); ++i) {
                    if (kLwLadder[i] == static_cast<int>(f.value.num)) {
                        idx = static_cast<int>(i) + 1;
                    }
                }
                e->setCurrentIndex(idx);
            }
            connect(e, &QComboBox::activated, this, [this, id](int index) {
                PropertyValue pv;
                pv.flag = (index == 0);
                if (index > 0 && index <= static_cast<int>(kLwLadder.size())) {
                    pv.num = kLwLadder[static_cast<std::size_t>(index - 1)];
                }
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        case PropEditor::JustifyCombo: {
            auto* e = new QComboBox();
            e->addItems({QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
            if (varies) {
                e->addItem(QString::fromLatin1(kVaries));
                e->setCurrentIndex(e->count() - 1);
            } else {
                e->setCurrentIndex(f.value.choice);
            }
            connect(e, &QComboBox::activated, this, [this, id](int index) {
                PropertyValue pv;
                pv.choice = index;
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        case PropEditor::AttachCombo: {
            auto* e = new QComboBox();
            e->addItems({QStringLiteral("Top Left"), QStringLiteral("Top Center"),
                         QStringLiteral("Top Right"), QStringLiteral("Middle Left"),
                         QStringLiteral("Middle Center"), QStringLiteral("Middle Right"),
                         QStringLiteral("Bottom Left"), QStringLiteral("Bottom Center"),
                         QStringLiteral("Bottom Right")});
            if (varies) {
                e->addItem(QString::fromLatin1(kVaries));
                e->setCurrentIndex(e->count() - 1);
            } else {
                e->setCurrentIndex(f.value.choice);
            }
            connect(e, &QComboBox::activated, this, [this, id](int index) {
                PropertyValue pv;
                pv.choice = index;
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        case PropEditor::FontCombo: {
            // The font dropdown: "Standard" (the built-in stroke font) + the system
            // outline (TTF/OTF) faces. value.text is the font name ("" = Standard).
            auto* e = new QComboBox();
            for (const std::string& fam : font_names_) {
                e->addItem(QString::fromStdString(fam));
            }
            if (e->count() == 0) {
                e->addItem(QStringLiteral("Standard"));
            }
            if (varies) {
                e->addItem(QString::fromLatin1(kVaries));
                e->setCurrentIndex(e->count() - 1);
            } else {
                const QString cur = f.value.text.empty() ? QStringLiteral("Standard")
                                                         : QString::fromStdString(f.value.text);
                const int idx = e->findText(cur);
                e->setCurrentIndex(idx < 0 ? 0 : idx);
            }
            connect(e, &QComboBox::activated, this, [this, id, e](int index) {
                const QString name = e->itemText(index);
                PropertyValue pv;
                pv.text = (name == QStringLiteral("Standard") || name == QString::fromLatin1(kVaries))
                              ? std::string()
                              : name.toStdString();
                emit_edit(id, pv);
            });
            current_form->addRow(label, e);
            break;
        }
        }
    }

    col->addStretch(1);
    scroll->setWidget(content);
    body_ = scroll;
    root_->addWidget(body_);
}

} // namespace musacad::ui
