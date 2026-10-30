// SPDX-License-Identifier: MIT
#include "tab_folder_item.hpp"

#include "path_item.hpp"
#include "path_stroke_item.hpp"
#include "tab_icon_item.hpp"
#include "tab_label_text_item.hpp"

#include <QFontMetrics>
#include <QPainterPath>
#include <algorithm>

namespace window_system::quick {
TabFolderItem::TabFolderItem(QQuickItem* parent)
    : QQuickItem(parent),
      background_(new PathItem(this)),
      dot_(new PathItem(this)),
      addBackground_(new PathItem(this)),
      chevron_(new PathStrokeItem(this)),
      addIcon_(new TabIconItem(this)),
      label_(new TabLabelTextItem(this)),
      name_(new TabLabelTextItem(this)),
      count_(new TabLabelTextItem(this))
{
    background_->setZ(0);
    dot_->setZ(1);
    addBackground_->setZ(1);
    label_->setZ(2);
    name_->setZ(2);
    count_->setZ(2);
    addIcon_->setZ(3);
    chevron_->setZ(3);
}

const QString& TabFolderItem::elidedText(const QString& source, const QFont& font, int width)
{
    if (elisionSource_ != source || elisionFont_ != font || elisionWidth_ != width) {
        elisionSource_ = source; elisionFont_ = font; elisionWidth_ = width;
        elisionResult_ = QFontMetrics(font).elidedText(source, Qt::ElideRight, width);
    }
    return elisionResult_;
}

void TabFolderItem::setText(TabLabelTextItem* item, const QRect& area, const QString& text,
                            const QFont& font, const QColor& color,
                            const TabStripLabelStyle& style)
{
    if (area.isEmpty() || text.isEmpty()) {
        item->clearLabel();
        return;
    }
    auto textStyle = style;
    textStyle.palette.text = color;
    TabLabelPresentation label;
    label.parts.tab = area;
    label.parts.title = area;
    label.state.shape = TabShape::Row;
    label.state.selected = true;
    label.state.title = text;
    label.font = font;
    label.direction = style.direction;
    label.naturalWidth = area.width();
    item->setLabel(label, textStyle, TabTextRole::Literal);
    item->setVisible(true);
    item->setPosition(area.topLeft());
}

bool TabFolderItem::setFolder(const TabFolderPaintState& state, const TabStripLabelStyle& style,
                              bool vertical)
{
    const TabPainter painter(style.metrics, style.palette);
    const QRect area(QPoint(), size().toSize());
    const QFontMetrics metrics(style.font);
    auto bold = style.font;
    bold.setWeight(QFont::DemiBold);
    const QFontMetrics boldMetrics(bold);
    label_->setVisible(false);
    name_->setVisible(false);
    count_->setVisible(false);
    addIcon_->setVisible(false);
    addBackground_->setVisible(false);
    chevron_->setVisible(false);
    dot_->setVisible(false);

    if (!vertical) {
        const QRect chip = painter.folderChipRect(area);
        if (state.name.isEmpty() && !state.collapsed) {
            background_->setVisible(false);
            const QRect dot(QPoint(), QSize(style.metrics.folderDot, style.metrics.folderDot));
            QPainterPath path;
            path.addEllipse(QRectF(dot.translated(area.center() - dot.center())));
            dot_->setPath(path);
            dot_->setFill(state.color);
            dot_->setVisible(true);
            return dot_->geometryError() == PathGeometryError::None;
        }
        auto fill = state.color;
        if (state.hovered || state.pressed) fill = fill.darker(state.pressed ? 118 : 108);
        QPainterPath chipPath;
        chipPath.addRoundedRect(QRectF(chip), style.metrics.radius, style.metrics.radius);
        background_->setPath(chipPath);
        background_->setFill(fill);
        background_->setVisible(true);
        const auto textArea = chip.adjusted(style.metrics.folderChipPadding, 0,
                                           -style.metrics.folderChipPadding, 0);
        const QString count = QString::number(state.count);
        const auto text = state.collapsed
            ? (state.name.isEmpty() ? count : state.name + QStringLiteral("  ") + count)
            : state.name;
        setText(label_, textArea, elidedText(text, style.font, textArea.width()),
                style.font, painter.textOn(fill), style);
        return background_->geometryError() == PathGeometryError::None;
    }

    auto wash = state.wash;
    if (state.hovered || state.pressed) wash = wash.darker(state.pressed ? 112 : 105);
    QPainterPath areaPath;
    areaPath.addRoundedRect(QRectF(area), style.metrics.radius, style.metrics.radius);
    background_->setPath(areaPath);
    background_->setFill(wash);
    background_->setVisible(true);

    const int size = style.metrics.folderButton;
    const QRect chevron(area.right() - size - 2, area.center().y() - size / 2, size, size);
    const QRect add = painter.folderAddRect(area);
    int x = area.left() + 10;
    const int end = (state.hovered ? add.left() : chevron.left()) - 4;
    const QString count = QString::number(state.count);
    const int countWidth = metrics.horizontalAdvance(count);
    const auto name = elidedText(state.name, bold, std::max(0, end - x - countWidth - 8));
    bool accepted = true;
    if (state.name.isEmpty()) {
        const QRect dot(x, area.center().y() - style.metrics.folderDot / 2,
                        style.metrics.folderDot, style.metrics.folderDot);
        QPainterPath path;
        path.addEllipse(QRectF(dot));
        dot_->setPath(path);
        dot_->setFill(state.color);
        dot_->setVisible(true);
        x += style.metrics.folderDot + 8;
    }
    if (!name.isEmpty()) {
        const QRect nameArea(x, area.top(), std::max(0, end - x), area.height());
        setText(name_, nameArea, name, bold, state.washText, style);
        x += boldMetrics.horizontalAdvance(name) + 8;
    }
    setText(count_, QRect(x, area.top(), std::max(0, end - x), area.height()), count,
            style.font, QColor::fromRgbF(state.washText.redF(), state.washText.greenF(),
                                         state.washText.blueF(), state.washText.alphaF() * 0.6),
            style);

    if (state.hovered && !state.addIcon.isNull()) {
        const int glyphSize = std::min({style.metrics.iconSize, add.width(), add.height()});
        const QRect glyph(QPoint(), QSize(glyphSize, glyphSize));
        addIcon_->setPosition(add.center() - glyph.center());
        addIcon_->setIcon(state.addIcon, glyph.size(), state.washText, true, false);
        addIcon_->setVisible(true);
        if (state.addHovered) {
            QPainterPath addPath;
            addPath.addRoundedRect(QRectF(add), style.metrics.controlRadius,
                                   style.metrics.controlRadius);
            addBackground_->setPath(addPath);
            auto washColor = state.washText;
            washColor.setAlphaF(washColor.alphaF() * 0.14);
            addBackground_->setFill(washColor);
            addBackground_->setVisible(true);
        }
    }

    QPainterPath arrow;
    const QPointF center = QRectF(chevron).center();
    if (state.collapsed) {
        arrow.moveTo(center.x() - 2, center.y() - 4);
        arrow.lineTo(center.x() + 2, center.y());
        arrow.lineTo(center.x() - 2, center.y() + 4);
    } else {
        arrow.moveTo(center.x() - 4, center.y() - 2);
        arrow.lineTo(center.x(), center.y() + 2);
        arrow.lineTo(center.x() + 4, center.y() - 2);
    }
    auto arrowColor = state.washText;
    arrowColor.setAlphaF(arrowColor.alphaF() * 0.75);
    chevron_->setSize(area.size());
    accepted = chevron_->setStroke(arrow, arrowColor, 1.5) && accepted;
    chevron_->setVisible(true);
    for (const auto* path : {background_, dot_, addBackground_})
        if (path->isVisible() && path->geometryError() != PathGeometryError::None) accepted = false;
    return accepted;
}

void TabFolderItem::clearFolder()
{
    background_->setVisible(false);
    dot_->setVisible(false);
    addBackground_->setVisible(false);
    chevron_->setVisible(false);
    addIcon_->setVisible(false);
    addIcon_->clearIcon();
    label_->clearLabel();
    name_->clearLabel();
    count_->clearLabel();
}

quint64 TabFolderItem::textLayoutBuilds() const
{
    return label_->renderStats().textLayoutBuilds + name_->renderStats().textLayoutBuilds
        + count_->renderStats().textLayoutBuilds;
}

} // namespace window_system::quick
