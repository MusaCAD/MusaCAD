// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/dimstyle_dialog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "musacad/core/dimension.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/text/stroke_font.hpp"

namespace musacad::ui {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

constexpr std::array<std::pair<const char*, core::Rgb>, 7> kNamedColors = {{
    {"Red", {255, 0, 0}},
    {"Yellow", {255, 255, 0}},
    {"Green", {0, 255, 0}},
    {"Cyan", {0, 255, 255}},
    {"Blue", {0, 0, 255}},
    {"Magenta", {255, 0, 255}},
    {"White", {255, 255, 255}},
}};

/// The standard lineweights (hundredths of a millimetre), as the Layer and Properties
/// lists offer them.
constexpr std::array<int, 24> kLineweights = {0,  5,  9,  13, 15, 18,  20,  25,  30,  35,  40,  50,
                                              53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};

QIcon swatch(const QColor& c) {
    QPixmap px(14, 14);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(0, 0, 0, 120));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(1.5, 1.5, 11.0, 11.0), 2.5, 2.5);
    return QIcon(px);
}

/// ByLayer, the seven named colours, the style's own colour if it is none of them, and
/// "Select Colour..." to choose any.
QComboBox* color_box(const core::ElementColor& c, QWidget* parent) {
    auto* box = new QComboBox(parent);
    box->addItem(swatch(QColor(200, 200, 200)), QStringLiteral("ByLayer"));
    int at = 0;
    for (const auto& [name, rgb] : kNamedColors) {
        const QColor q(rgb.r, rgb.g, rgb.b);
        box->addItem(swatch(q), QString::fromLatin1(name), QVariant::fromValue(q));
        if (!c.by_layer && c.color == rgb) {
            at = box->count() - 1;
        }
    }
    if (!c.by_layer && at == 0) {
        const QColor q(c.color.r, c.color.g, c.color.b);
        box->addItem(swatch(q), QStringLiteral("RGB %1,%2,%3").arg(c.color.r).arg(c.color.g).arg(c.color.b),
                     QVariant::fromValue(q));
        at = box->count() - 1;
    }
    box->addItem(QStringLiteral("Select Colour..."), QStringLiteral("pick"));
    box->setCurrentIndex(at);
    auto prev = std::make_shared<int>(at);
    QObject::connect(box, &QComboBox::activated, box, [box, prev](int i) {
        if (i != box->count() - 1) {
            *prev = i;
            return;
        }
        const QVariant was = box->itemData(*prev);
        const QColor start = was.userType() == QMetaType::QColor ? was.value<QColor>() : QColor(Qt::white);
        const QColor q = QColorDialog::getColor(start, box, QStringLiteral("Select Colour"));
        if (!q.isValid()) {
            box->setCurrentIndex(*prev);
            return;
        }
        box->insertItem(box->count() - 1, swatch(q), QStringLiteral("RGB %1,%2,%3").arg(q.red()).arg(q.green()).arg(q.blue()),
                        QVariant::fromValue(q));
        *prev = box->count() - 2;
        box->setCurrentIndex(*prev);
    });
    return box;
}

core::ElementColor read_color(const QComboBox* box) {
    const QVariant v = box->currentData();
    if (v.userType() != QMetaType::QColor) {
        return core::ElementColor{}; // ByLayer
    }
    const QColor q = v.value<QColor>();
    return core::ElementColor{false,
                              {static_cast<std::uint8_t>(q.red()), static_cast<std::uint8_t>(q.green()),
                               static_cast<std::uint8_t>(q.blue())}};
}

QDoubleSpinBox* length_box(double v, double min, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setDecimals(4);
    box->setRange(min, 1e6);
    box->setSingleStep(0.25);
    box->setValue(v);
    box->setMinimumWidth(130);
    return box;
}

/// A small picture of an arrowhead type, drawn as the dimension draws it.
QIcon arrow_icon(std::uint8_t type) {
    QPixmap px(28, 14);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink(225, 228, 232);
    p.setPen(QPen(ink, 1.3));
    p.drawLine(QPointF(8.0, 7.0), QPointF(27.0, 7.0));
    switch (type) {
    case 1: // tick
        p.drawLine(QPointF(4.0, 12.0), QPointF(12.0, 2.0));
        break;
    case 2: // open
        p.drawLine(QPointF(2.0, 7.0), QPointF(12.0, 3.0));
        p.drawLine(QPointF(2.0, 7.0), QPointF(12.0, 11.0));
        break;
    case 3: // dot
        p.setBrush(ink);
        p.drawEllipse(QPointF(5.0, 7.0), 3.0, 3.0);
        break;
    default: { // closed filled
        p.setBrush(ink);
        QPainterPath tri;
        tri.moveTo(2.0, 7.0);
        tri.lineTo(12.0, 3.5);
        tri.lineTo(12.0, 10.5);
        tri.closeSubpath();
        p.drawPath(tri);
    }
    }
    return QIcon(px);
}

const char* arrow_name(std::uint8_t t) {
    switch (t) {
    case 1:
        return "Architectural tick";
    case 2:
        return "Open";
    case 3:
        return "Dot";
    default:
        return "Closed filled";
    }
}

/// "0", "0.0", "0.00" ... "0.00000000", in the style's separator: the precision as the
/// numbers will read.
QString precision_sample(int places, QChar sep) {
    QString s = QStringLiteral("0");
    if (places > 0) {
        s += sep;
        s += QString(places, QLatin1Char('0'));
    }
    return s;
}

QGroupBox* group(const QString& title, QLayout* body, QWidget* parent) {
    auto* g = new QGroupBox(title, parent);
    g->setLayout(body);
    return g;
}

/// A form row whose label takes the same width in every group, so the fields line up
/// down the whole tab.
void row(QFormLayout* f, const QString& text, QWidget* field) {
    auto* label = new QLabel(text);
    label->setMinimumWidth(170);
    f->addRow(label, field);
}

QFormLayout* form() {
    auto* f = new QFormLayout;
    f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    f->setHorizontalSpacing(16);
    f->setVerticalSpacing(10);
    f->setContentsMargins(12, 12, 12, 12);
    return f;
}

QWidget* page(QWidget* parent, std::initializer_list<QWidget*> groups) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(12);
    for (QWidget* g : groups) {
        v->addWidget(g);
    }
    v->addStretch(1);
    return w;
}

} // namespace

// ---------------------------------------------------------------------------
// The preview.
// ---------------------------------------------------------------------------
DimStylePreview::DimStylePreview(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize DimStylePreview::sizeHint() const { return {360, 270}; }
QSize DimStylePreview::minimumSizeHint() const { return {280, 210}; }

void DimStylePreview::set_style(const core::DimStyle& style) {
    style_ = style;
    update();
}

void DimStylePreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QColor(70, 76, 86));
    p.setBrush(QColor(28, 31, 37));
    p.drawRoundedRect(frame, 6.0, 6.0);

    using core::Vec2;
    struct Run {
        std::vector<Vec2> lines; // pairs
        std::vector<Vec2> tris;  // triples
        QColor color;
        double width = 1.2;
    };
    std::vector<Run> runs;
    const auto qc = [](core::Rgb c) { return QColor(c.r, c.g, c.b); };
    // The part, in the drawing's object colour; ByLayer dimensions in the default layer's.
    const QColor part(214, 160, 64);
    const core::Rgb by_layer{225, 228, 232};
    Run outline{{}, {}, part, 1.6};
    const Vec2 shape[] = {{0, 0}, {30, 0}, {46, 16}, {46, 30}, {18, 30}, {18, 22}, {0, 22}};
    for (std::size_t i = 0; i < std::size(shape); ++i) {
        outline.lines.push_back(shape[i]);
        outline.lines.push_back(shape[(i + 1) % std::size(shape)]);
    }
    const Vec2 hole{9, 11};
    constexpr double hole_r = 5.0;
    for (int i = 0; i < 48; ++i) {
        const double a0 = core::kTwoPi * i / 48.0;
        const double a1 = core::kTwoPi * (i + 1) / 48.0;
        outline.lines.push_back(hole + Vec2{std::cos(a0), std::sin(a0)} * hole_r);
        outline.lines.push_back(hole + Vec2{std::cos(a1), std::sin(a1)} * hole_r);
    }
    runs.push_back(outline);

    std::vector<core::DimData> dims;
    const auto add = [&](core::DimType t, Vec2 a, Vec2 b, Vec2 at, double aux) {
        core::DimData d;
        d.type = t;
        d.a = a;
        d.b = b;
        d.line_pt = at;
        d.aux = aux;
        dims.push_back(d);
    };
    add(core::DimType::Linear, {18, 30}, {46, 30}, {32, 37}, core::kPi);       // across the top
    add(core::DimType::Linear, {0, 0}, {0, 22}, {-7, 11}, core::kHalfPi);     // up the side
    add(core::DimType::Aligned, {30, 0}, {46, 16}, {43.0, 3.0}, 0.0);         // the slope
    add(core::DimType::Angular, {46, 16}, {46, 30}, {30, 0}, 7.0);            // the corner's angle
    const Vec2 leader_at{-4.0, -9.0};
    const Vec2 on_hole = hole + core::normalized(leader_at - hole) * hole_r;
    add(core::DimType::Radius, hole, on_hole, leader_at, 0.0);                 // the hole

    for (const core::DimData& d : dims) {
        const core::DimGeometry g = core::compute_dim_geometry(d, style_, by_layer);
        runs.push_back(Run{g.ext_lines, {}, qc(g.ext_color), 1.0});
        runs.push_back(Run{g.dim_lines, {}, qc(g.dim_color), 1.0});
        runs.push_back(Run{g.arrow_lines, g.arrow_fills, qc(g.arrow_color), 1.0});
        Run txt{{}, {}, qc(g.text_color), 1.1};
        core::text::append_text_segments(g.label, g.text_pos, g.text_height, g.text_rotation, g.text_justify, txt.lines);
        if (!g.label2.empty()) {
            core::text::append_text_segments(g.label2, g.label2_pos, g.text_height, g.text_rotation, g.text_justify,
                                             txt.lines);
        }
        runs.push_back(std::move(txt));
    }

    // Fit everything in, with room to breathe.
    double x0 = 1e300;
    double y0 = 1e300;
    double x1 = -1e300;
    double y1 = -1e300;
    for (const Run& r : runs) {
        for (const auto* pts : {&r.lines, &r.tris}) {
            for (const Vec2& v : *pts) {
                x0 = std::min(x0, v.x);
                y0 = std::min(y0, v.y);
                x1 = std::max(x1, v.x);
                y1 = std::max(y1, v.y);
            }
        }
    }
    if (!(x1 > x0) || !(y1 > y0)) {
        return;
    }
    const double margin = 18.0;
    const double sx = (width() - 2.0 * margin) / (x1 - x0);
    const double sy = (height() - 2.0 * margin) / (y1 - y0);
    const double s = std::max(1e-9, std::min(sx, sy));
    const double ox = (width() - (x1 - x0) * s) * 0.5;
    const double oy = (height() - (y1 - y0) * s) * 0.5;
    const auto to = [&](Vec2 v) { return QPointF(ox + (v.x - x0) * s, height() - oy - (v.y - y0) * s); };
    for (const Run& r : runs) {
        p.setPen(QPen(r.color, r.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        for (std::size_t i = 0; i + 1 < r.lines.size(); i += 2) {
            p.drawLine(to(r.lines[i]), to(r.lines[i + 1]));
        }
        if (!r.tris.empty()) {
            p.setBrush(r.color);
            for (std::size_t i = 0; i + 2 < r.tris.size(); i += 3) {
                const QPointF tri[3] = {to(r.tris[i]), to(r.tris[i + 1]), to(r.tris[i + 2])};
                p.drawPolygon(tri, 3);
            }
            p.setBrush(Qt::NoBrush);
        }
    }
}

QString dimstyle_summary(const core::DimStyle& s) {
    const char* moved = s.text_move == 1 ? "moved text keeps a leader to its line"
                        : s.text_move == 2 ? "moved text stands on its own"
                                           : "moved text takes its dimension line along";
    return QStringLiteral("Text %1 high, %2 the dimension line\nArrowheads: %3, %4\nValues like %5 and %6\n%7")
        .arg(qs(core::format_dim_value(s.text_height, core::DimStyle{})))
        .arg(s.text_above ? QStringLiteral("above") : QStringLiteral("centred on"))
        .arg(QString::fromLatin1(arrow_name(s.arrow_type)).toLower())
        .arg(qs(core::format_dim_value(s.arrow_size, core::DimStyle{})))
        .arg(qs(core::format_dim_value(1234.5, s)))
        .arg(qs(core::format_dim_value(0.5, s)))
        .arg(QString::fromLatin1(moved).left(1).toUpper() + QString::fromLatin1(moved).mid(1));
}

// ---------------------------------------------------------------------------
// The editor.
// ---------------------------------------------------------------------------
DimStyleEditor::DimStyleEditor(const core::DimStyle& style, const QString& title, QWidget* parent)
    : QDialog(parent), base_(style) {
    setWindowTitle(title);
    auto* tabs = new QTabWidget(this);
    tabs->setDocumentMode(false);

    // Lines.
    auto* dim_color = color_box(style.dim_color, this);
    auto* lineweight = new QComboBox(this);
    bool weight_listed = false;
    for (const int lw : kLineweights) {
        lineweight->addItem(QStringLiteral("%1 mm").arg(lw / 100.0, 0, 'f', 2), lw);
        if (lw == style.dim_lineweight) {
            lineweight->setCurrentIndex(lineweight->count() - 1);
            weight_listed = true;
        }
    }
    if (!weight_listed) { // a weight from elsewhere is kept, not rounded to a listed one
        lineweight->addItem(QStringLiteral("%1 mm").arg(style.dim_lineweight / 100.0, 0, 'f', 2),
                            static_cast<int>(style.dim_lineweight));
        lineweight->setCurrentIndex(lineweight->count() - 1);
    }
    auto* ext_color = color_box(style.ext_color, this);
    QDoubleSpinBox* ext_beyond = length_box(style.ext_extension, 0.0, this);
    QDoubleSpinBox* ext_offset = length_box(style.ext_offset, 0.0, this);
    QFormLayout* dl = form();
    row(dl, QStringLiteral("Colour:"), dim_color);
    row(dl, QStringLiteral("Lineweight:"), lineweight);
    QFormLayout* el = form();
    row(el, QStringLiteral("Colour:"), ext_color);
    row(el, QStringLiteral("Extend beyond dim lines:"), ext_beyond);
    row(el, QStringLiteral("Offset from origin:"), ext_offset);
    tabs->addTab(page(tabs, {group(QStringLiteral("Dimension lines"), dl, this),
                             group(QStringLiteral("Extension lines"), el, this)}),
                 QStringLiteral("Lines"));

    // Symbols and arrows.
    auto* arrow_type = new QComboBox(this);
    for (std::uint8_t t = 0; t < 4; ++t) {
        arrow_type->addItem(arrow_icon(t), QString::fromLatin1(arrow_name(t)));
    }
    arrow_type->setIconSize(QSize(28, 14));
    arrow_type->setCurrentIndex(style.arrow_type < 4 ? style.arrow_type : 0);
    QDoubleSpinBox* arrow_size = length_box(style.arrow_size, 0.0, this);
    auto* arrow_color = color_box(style.arrow_color, this);
    QFormLayout* af = form();
    row(af, QStringLiteral("Arrowheads:"), arrow_type);
    row(af, QStringLiteral("Arrow size:"), arrow_size);
    row(af, QStringLiteral("Colour:"), arrow_color);
    auto* leaders_note = new QLabel(QStringLiteral("Leaders and multileaders drawn in this style use these arrowheads."), this);
    leaders_note->setWordWrap(true);
    leaders_note->setEnabled(false);
    af->addRow(leaders_note);
    tabs->addTab(page(tabs, {group(QStringLiteral("Arrowheads"), af, this)}), QStringLiteral("Symbols and Arrows"));

    // Text.
    QDoubleSpinBox* text_height = length_box(style.text_height, 0.0001, this);
    auto* text_color = color_box(style.text_color, this);
    auto* vertical = new QComboBox(this);
    vertical->addItems({QStringLiteral("Above"), QStringLiteral("Centred")});
    vertical->setCurrentIndex(style.text_above ? 0 : 1);
    QFormLayout* ta = form();
    row(ta, QStringLiteral("Text height:"), text_height);
    row(ta, QStringLiteral("Text colour:"), text_color);
    QFormLayout* tp = form();
    row(tp, QStringLiteral("Vertical:"), vertical);
    tabs->addTab(page(tabs, {group(QStringLiteral("Text appearance"), ta, this),
                             group(QStringLiteral("Text placement"), tp, this)}),
                 QStringLiteral("Text"));

    // Fit.
    auto* fit_box = new QVBoxLayout;
    fit_box->setContentsMargins(12, 12, 12, 12);
    fit_box->setSpacing(8);
    auto* fit_intro = new QLabel(QStringLiteral("When the text does not fit between the extension lines:"), this);
    fit_intro->setWordWrap(true);
    fit_box->addWidget(fit_intro);
    auto* fits = new QButtonGroup(this);
    const QString fit_names[3] = {QStringLiteral("Move the text outside (best fit)"),
                                  QStringLiteral("Always keep the text between the extension lines"),
                                  QStringLiteral("Always place the text outside")};
    for (int i = 0; i < 3; ++i) {
        auto* rb = new QRadioButton(fit_names[i], this);
        fits->addButton(rb, i);
        fit_box->addWidget(rb);
    }
    if (QAbstractButton* b = fits->button(style.text_fit < 3 ? style.text_fit : 0)) {
        b->setChecked(true);
    }
    auto* move_box = new QVBoxLayout;
    move_box->setContentsMargins(12, 12, 12, 12);
    move_box->setSpacing(8);
    auto* move_intro = new QLabel(QStringLiteral("When the text is moved from where it would be, place it:"), this);
    move_intro->setWordWrap(true);
    move_box->addWidget(move_intro);
    auto* moves = new QButtonGroup(this);
    const QString move_names[3] = {QStringLiteral("Beside the dimension line (the line moves with it)"),
                                   QStringLiteral("Over the dimension line, with a leader"),
                                   QStringLiteral("Over the dimension line, without a leader")};
    for (int i = 0; i < 3; ++i) {
        auto* rb = new QRadioButton(move_names[i], this);
        moves->addButton(rb, i);
        move_box->addWidget(rb);
    }
    if (QAbstractButton* b = moves->button(style.text_move < 3 ? style.text_move : 0)) {
        b->setChecked(true);
    }
    tabs->addTab(page(tabs, {group(QStringLiteral("Fit options"), fit_box, this),
                             group(QStringLiteral("Text placement"), move_box, this)}),
                 QStringLiteral("Fit"));

    // Primary units.
    auto* separator = new QComboBox(this);
    separator->addItems({QStringLiteral("'.' (Period)"), QStringLiteral("',' (Comma)")});
    separator->setCurrentIndex(style.decimal_separator == ',' ? 1 : 0);
    auto* precision = new QComboBox(this);
    const auto fill_precision = [precision, separator](int keep) {
        const QChar sep = separator->currentIndex() == 1 ? QLatin1Char(',') : QLatin1Char('.');
        precision->blockSignals(true);
        precision->clear();
        for (int places = 0; places <= 8; ++places) {
            precision->addItem(precision_sample(places, sep));
        }
        precision->setCurrentIndex(std::clamp(keep, 0, 8));
        precision->blockSignals(false);
    };
    fill_precision(style.precision);
    QFormLayout* lu = form();
    row(lu, QStringLiteral("Unit format:"), new QLabel(QStringLiteral("Decimal"), this));
    row(lu, QStringLiteral("Precision:"), precision);
    row(lu, QStringLiteral("Decimal separator:"), separator);
    auto* leading = new QCheckBox(QStringLiteral("Leading  (0.50 reads .50)"), this);
    leading->setChecked((style.zero_suppression & core::kDimZinLeading) != 0);
    auto* trailing = new QCheckBox(QStringLiteral("Trailing  (12.50 reads 12.5)"), this);
    trailing->setChecked((style.zero_suppression & core::kDimZinTrailing) != 0);
    auto* zeros = new QVBoxLayout;
    zeros->setContentsMargins(12, 12, 12, 12);
    zeros->setSpacing(8);
    zeros->addWidget(leading);
    zeros->addWidget(trailing);
    tabs->addTab(page(tabs, {group(QStringLiteral("Linear dimensions"), lu, this),
                             group(QStringLiteral("Zero suppression"), zeros, this)}),
                 QStringLiteral("Primary Units"));

    auto* preview = new DimStylePreview(this);
    read_ = [=, this] {
        core::DimStyle s = base_;
        s.dim_color = read_color(dim_color);
        s.dim_lineweight = static_cast<std::uint8_t>(lineweight->currentData().toInt());
        s.ext_color = read_color(ext_color);
        s.ext_extension = ext_beyond->value();
        s.ext_offset = ext_offset->value();
        s.arrow_type = static_cast<std::uint8_t>(arrow_type->currentIndex());
        s.arrow_size = arrow_size->value();
        s.arrow_color = read_color(arrow_color);
        s.text_height = text_height->value();
        s.text_color = read_color(text_color);
        s.text_above = vertical->currentIndex() == 0;
        s.text_fit = static_cast<std::uint8_t>(std::max(0, fits->checkedId()));
        s.text_move = static_cast<std::uint8_t>(std::max(0, moves->checkedId()));
        s.precision = static_cast<std::uint8_t>(std::max(0, precision->currentIndex()));
        s.decimal_separator = separator->currentIndex() == 1 ? ',' : '.';
        s.zero_suppression = static_cast<std::uint8_t>((leading->isChecked() ? core::kDimZinLeading : 0) |
                                                       (trailing->isChecked() ? core::kDimZinTrailing : 0));
        return s;
    };
    const auto refresh_preview = [preview, this] { preview->set_style(read_()); };
    refresh_preview();
    for (QComboBox* c : {dim_color, lineweight, ext_color, arrow_type, arrow_color, text_color, vertical, precision}) {
        connect(c, &QComboBox::currentIndexChanged, this, refresh_preview);
    }
    connect(separator, &QComboBox::currentIndexChanged, this, [=] {
        fill_precision(precision->currentIndex());
        refresh_preview();
    });
    for (QDoubleSpinBox* b : {ext_beyond, ext_offset, arrow_size, text_height}) {
        connect(b, &QDoubleSpinBox::valueChanged, this, refresh_preview);
    }
    for (QCheckBox* b : {leading, trailing}) {
        connect(b, &QCheckBox::toggled, this, refresh_preview);
    }
    connect(fits, &QButtonGroup::idToggled, this, refresh_preview);
    connect(moves, &QButtonGroup::idToggled, this, refresh_preview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    body->addWidget(tabs, 3);
    auto* side = new QVBoxLayout;
    side->setSpacing(6);
    auto* preview_title = new QLabel(QStringLiteral("Preview"), this);
    QFont bold = preview_title->font();
    bold.setBold(true);
    preview_title->setFont(bold);
    side->addWidget(preview_title);
    side->addWidget(preview, 1);
    body->addLayout(side, 2);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 14);
    layout->setSpacing(14);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);
    resize(860, 520);
}

core::DimStyle DimStyleEditor::style() const { return read_ ? read_() : base_; }

// ---------------------------------------------------------------------------
// The manager.
// ---------------------------------------------------------------------------
DimStyleManager::DimStyleManager(std::vector<core::DimStyle> styles, std::uint16_t current,
                                 std::vector<std::string> unused, Submit submit, QWidget* parent)
    : QDialog(parent), styles_(std::move(styles)), current_(current), unused_(std::move(unused)),
      submit_(std::move(submit)) {
    setWindowTitle(QStringLiteral("Dimension Style Manager"));
    if (styles_.empty()) {
        styles_.push_back(core::DimStyle{"Standard"});
    }
    if (current_ >= styles_.size()) {
        current_ = 0;
    }
    QFont bold = font();
    bold.setBold(true);

    current_label_ = new QLabel(this);
    auto* styles_title = new QLabel(QStringLiteral("Styles"), this);
    styles_title->setFont(bold);
    list_ = new QListWidget(this);
    list_->setMinimumWidth(190);
    list_->setSpacing(2);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    filter_ = new QComboBox(this);
    filter_->addItems({QStringLiteral("All styles"), QStringLiteral("Styles in use")});
    auto* filter_row = new QHBoxLayout;
    filter_row->setSpacing(8);
    filter_row->addWidget(new QLabel(QStringLiteral("List:"), this));
    filter_row->addWidget(filter_, 1);
    auto* left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(styles_title);
    left->addWidget(list_, 1);
    left->addLayout(filter_row);

    preview_title_ = new QLabel(this);
    preview_title_->setFont(bold);
    preview_ = new DimStylePreview(this);
    description_ = new QLabel(this);
    description_->setWordWrap(true);
    description_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    description_->setMinimumHeight(70);
    auto* desc_box = new QVBoxLayout;
    desc_box->setContentsMargins(12, 10, 12, 10);
    desc_box->addWidget(description_);
    auto* middle = new QVBoxLayout;
    middle->setSpacing(8);
    middle->addWidget(preview_title_);
    middle->addWidget(preview_, 1);
    middle->addWidget(group(QStringLiteral("Description"), desc_box, this));

    current_btn_ = new QPushButton(QStringLiteral("Set Current"), this);
    auto* new_btn = new QPushButton(QStringLiteral("New..."), this);
    auto* modify_btn = new QPushButton(QStringLiteral("Modify..."), this);
    rename_btn_ = new QPushButton(QStringLiteral("Rename..."), this);
    delete_btn_ = new QPushButton(QStringLiteral("Delete"), this);
    auto* close_btn = new QPushButton(QStringLiteral("Close"), this);
    for (QPushButton* b : {current_btn_, new_btn, modify_btn, rename_btn_, delete_btn_, close_btn}) {
        b->setMinimumWidth(116);
    }
    connect(current_btn_, &QPushButton::clicked, this, [this] { set_current(); });
    connect(new_btn, &QPushButton::clicked, this, [this] { new_style(); });
    connect(modify_btn, &QPushButton::clicked, this, [this] { modify_style(); });
    connect(rename_btn_, &QPushButton::clicked, this, [this] { rename_style(); });
    connect(delete_btn_, &QPushButton::clicked, this, [this] { delete_style(); });
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    auto* right = new QVBoxLayout;
    right->setSpacing(8);
    right->addSpacing(styles_title->sizeHint().height() + 8);
    right->addWidget(current_btn_);
    right->addWidget(new_btn);
    right->addWidget(modify_btn);
    right->addSpacing(10);
    right->addWidget(rename_btn_);
    right->addWidget(delete_btn_);
    right->addStretch(1);
    right->addWidget(close_btn);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { show_selected(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { modify_style(); });
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        if (list_->itemAt(at) == nullptr) {
            return;
        }
        QMenu menu(this);
        QAction* cur = menu.addAction(QStringLiteral("Set Current"));
        QAction* mod = menu.addAction(QStringLiteral("Modify..."));
        menu.addSeparator();
        QAction* ren = menu.addAction(QStringLiteral("Rename..."));
        QAction* del = menu.addAction(QStringLiteral("Delete"));
        cur->setEnabled(current_btn_->isEnabled());
        ren->setEnabled(rename_btn_->isEnabled());
        del->setEnabled(delete_btn_->isEnabled());
        QAction* chosen = menu.exec(list_->viewport()->mapToGlobal(at));
        if (chosen == cur) {
            set_current();
        } else if (chosen == mod) {
            modify_style();
        } else if (chosen == ren) {
            rename_style();
        } else if (chosen == del) {
            delete_style();
        }
    });

    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    body->addLayout(left, 2);
    body->addLayout(middle, 4);
    body->addLayout(right);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 14);
    layout->setSpacing(12);
    layout->addWidget(current_label_);
    layout->addLayout(body, 1);
    refresh();
    for (std::size_t r = 0; r < shown_.size(); ++r) {
        if (shown_[r] == current_) {
            list_->setCurrentRow(static_cast<int>(r));
        }
    }
    resize(820, 480);
}

bool DimStyleManager::in_use(std::size_t i) const {
    return std::find(unused_.begin(), unused_.end(), styles_[i].name) == unused_.end();
}

void DimStyleManager::refresh() {
    const int keep = selected();
    list_->blockSignals(true);
    list_->clear();
    shown_.clear();
    const bool used_only = filter_->currentIndex() == 1;
    for (std::size_t i = 0; i < styles_.size(); ++i) {
        if (used_only && !in_use(i) && i != current_ && i != 0) {
            continue;
        }
        shown_.push_back(i);
        auto* item = new QListWidgetItem(qs(styles_[i].name), list_);
        item->setSizeHint(QSize(0, 26));
        if (i == current_) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setToolTip(QStringLiteral("The current style: new dimensions are drawn in it."));
        }
    }
    int row = 0;
    for (std::size_t r = 0; r < shown_.size(); ++r) {
        if (static_cast<int>(shown_[r]) == keep) {
            row = static_cast<int>(r);
        }
    }
    list_->setCurrentRow(row);
    list_->blockSignals(false);
    current_label_->setText(QStringLiteral("Current dimension style: <b>%1</b>").arg(qs(styles_[current_].name).toHtmlEscaped()));
    show_selected();
}

int DimStyleManager::selected() const {
    const int row = list_ != nullptr ? list_->currentRow() : -1;
    return row >= 0 && static_cast<std::size_t>(row) < shown_.size() ? static_cast<int>(shown_[static_cast<std::size_t>(row)])
                                                                    : static_cast<int>(current_);
}

void DimStyleManager::show_selected() {
    const auto i = static_cast<std::size_t>(selected());
    preview_title_->setText(QStringLiteral("Preview of: ") + qs(styles_[i].name));
    preview_->set_style(styles_[i]);
    QString text = dimstyle_summary(styles_[i]);
    if (i == current_) {
        text = QStringLiteral("The current style.\n") + text;
    }
    description_->setText(text);
    update_buttons();
}

void DimStyleManager::update_buttons() {
    const auto i = static_cast<std::size_t>(selected());
    current_btn_->setEnabled(i != current_);
    rename_btn_->setEnabled(i != 0);
    delete_btn_->setEnabled(i != 0 && i != current_ && !in_use(i));
    delete_btn_->setToolTip(i == 0            ? QStringLiteral("Standard is always there.")
                            : i == current_   ? QStringLiteral("The current style cannot be deleted.")
                            : in_use(i)       ? QStringLiteral("Dimensions use this style.")
                                              : QString());
}

void DimStyleManager::set_current() {
    const auto i = static_cast<std::size_t>(selected());
    current_ = static_cast<std::uint16_t>(i);
    submit_(core::SetCurrentDimStyleCommand{styles_[i].name, false});
    refresh();
}

void DimStyleManager::new_style() {
    const auto from = static_cast<std::size_t>(selected());
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Create New Dimension Style"),
                                               QStringLiteral("New style name (starting with %1):").arg(qs(styles_[from].name)),
                                               QLineEdit::Normal, QStringLiteral("Copy of ") + qs(styles_[from].name), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    for (const QChar c : name) {
        if (QStringLiteral("<>/\\\":;?*|,=`").contains(c)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("A style name cannot contain %1").arg(c));
            return;
        }
    }
    for (const core::DimStyle& s : styles_) {
        if (qs(s.name) == name) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("There is already a style called %1.").arg(name));
            return;
        }
    }
    core::DimStyle start = styles_[from];
    start.name = name.toStdString();
    DimStyleEditor editor(start, QStringLiteral("New Dimension Style: ") + name, this);
    if (editor.exec() != QDialog::Accepted) {
        return;
    }
    core::DimStyle made = editor.style();
    made.name = start.name;
    styles_.push_back(made);
    unused_.push_back(made.name); // nothing uses it yet
    submit_(core::AddDimStyleCommand{made});
    filter_->setCurrentIndex(0);
    refresh();
    list_->setCurrentRow(static_cast<int>(shown_.size() - 1));
}

void DimStyleManager::modify_style() {
    const auto i = static_cast<std::size_t>(selected());
    DimStyleEditor editor(styles_[i], QStringLiteral("Modify Dimension Style: ") + qs(styles_[i].name), this);
    if (editor.exec() != QDialog::Accepted) {
        return;
    }
    core::DimStyle changed = editor.style();
    changed.name = styles_[i].name;
    styles_[i] = changed;
    submit_(core::SetDimStyleCommand{static_cast<std::uint16_t>(i), changed});
    refresh();
}

void DimStyleManager::rename_style() {
    const auto i = static_cast<std::size_t>(selected());
    if (i == 0) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename Dimension Style"), QStringLiteral("New name:"),
                                               QLineEdit::Normal, qs(styles_[i].name), &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == qs(styles_[i].name)) {
        return;
    }
    for (const core::DimStyle& s : styles_) {
        if (qs(s.name) == name) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("There is already a style called %1.").arg(name));
            return;
        }
    }
    const std::string from = styles_[i].name;
    styles_[i].name = name.toStdString();
    for (std::string& u : unused_) {
        if (u == from) {
            u = styles_[i].name;
        }
    }
    submit_(core::RenameDimStyleCommand{from, styles_[i].name});
    refresh();
}

void DimStyleManager::delete_style() {
    const auto i = static_cast<std::size_t>(selected());
    if (i == 0 || i == current_ || in_use(i)) {
        return;
    }
    if (QMessageBox::question(this, windowTitle(), QStringLiteral("Delete the dimension style %1?").arg(qs(styles_[i].name))) !=
        QMessageBox::Yes) {
        return;
    }
    submit_(core::DeleteDimStyleCommand{styles_[i].name});
    styles_.erase(styles_.begin() + static_cast<std::ptrdiff_t>(i));
    if (current_ > i) {
        --current_;
    }
    refresh();
}

} // namespace musacad::ui
