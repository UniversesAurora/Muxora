// SPDX-License-Identifier: MIT
#include "tab_label_text_item.hpp"

#include <QFontMetricsF>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGOpacityNode>
#include <QSGTextNode>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace window_system::quick {
namespace {

QString visibleTitle(const TabLabelPresentation& label)
{
    if (label.state.iconOnly || label.state.shape == TabShape::Tile)
        return label.state.title.left(1).toUpper();
    QString text = label.state.title;
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    QString visible;
    visible.reserve(text.size());
    for (qsizetype index = 0; index < text.size(); ++index) {
        if (text[index] != QLatin1Char('&')) {
            visible.append(text[index]);
        } else if (index + 1 < text.size() && text[index + 1] == QLatin1Char('&')) {
            visible.append(QLatin1Char('&'));
            ++index;
        }
    }
    return visible;
}

bool nearlyEqual(qreal a, qreal b)
{
    return qAbs(a - b) < 0.0001;
}

struct TextSegment {
    QRectF clip;
    qreal cutOpacity = 1;
    qreal coverFactor = 0;

    bool operator==(const TextSegment& other) const
    {
        return clip == other.clip && nearlyEqual(cutOpacity, other.cutOpacity)
            && nearlyEqual(coverFactor, other.coverFactor);
    }
};

void appendSegment(QVector<TextSegment>& segments, qreal begin, qreal end,
                   const TextSegment& templateSegment)
{
    if (end <= begin) return;
    auto segment = templateSegment;
    segment.clip.setLeft(begin);
    segment.clip.setRight(end);
    if (!segments.isEmpty()) {
        auto& previous = segments.last();
        if (nearlyEqual(previous.clip.right(), begin)
            && nearlyEqual(previous.cutOpacity, segment.cutOpacity)
            && nearlyEqual(previous.coverFactor, segment.coverFactor)) {
            previous.clip.setRight(end);
            return;
        }
    }
    segments.append(segment);
}

QVector<TextSegment> titleSegments(const TabLabelPresentation& label, const TabTitleFade& fade)
{
    QVector<TextSegment> segments;
    if (label.parts.title.isEmpty() || label.state.title.isEmpty()) return segments;
    const auto tabOrigin = label.parts.tab.topLeft();
    TextSegment shape;
    shape.clip = QRectF(label.parts.title.translated(-tabOrigin));
    if (!fade.active()) {
        segments.append(shape);
        return segments;
    }

    const qreal localBegin = shape.clip.left();
    const qreal localEnd = shape.clip.right();
    const auto globalToLocal = [tabOrigin](qreal x) { return x - tabOrigin.x(); };
    QVector<qreal> boundaries{localBegin, localEnd};
    const auto addInterval = [&](qreal begin, qreal end) {
        begin = std::clamp(begin, fade.left, fade.right);
        end = std::clamp(end, begin, fade.right);
        if (end <= begin) return;
        boundaries.append(globalToLocal(begin));
        boundaries.append(globalToLocal(end));
        for (int x = int(std::floor(begin)); x < int(std::ceil(end)); ++x) {
            boundaries.append(std::clamp(globalToLocal(qreal(x)), localBegin, localEnd));
        }
    };
    if (fade.cut && fade.fadeLength > 0) {
        addInterval(fade.right - fade.fadeLength, fade.right);
    }
    if (fade.covering) {
        const qreal coverBegin = fade.reverse ? fade.left : fade.actionEnd - fade.actionFade;
        const qreal coverEnd = fade.reverse ? fade.actionEnd + fade.actionFade : fade.actionEnd;
        addInterval(coverBegin, coverEnd);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    for (qsizetype index = 0; index + 1 < boundaries.size(); ++index) {
        const auto sliceBegin = boundaries[index];
        const auto sliceEnd = boundaries[index + 1];
        const auto sample = tabOrigin.x() + (sliceBegin + sliceEnd) / 2;
        auto slice = shape;
        slice.cutOpacity = fade.cutOpacityAt(sample);
        slice.coverFactor = fade.actionOpacity > 0
            ? (1 - fade.coverOpacityAt(sample)) / fade.actionOpacity : 0;
        appendSegment(segments, sliceBegin, sliceEnd, slice);
    }
    return segments;
}

bool sameSource(const TabLabelPresentation& a, const TabLabelPresentation& b,
                const QString& aText, const QString& bText)
{
    return aText == bText && a.font == b.font && a.direction == b.direction
        && a.parts.tab.size() == b.parts.tab.size()
        && a.parts.title.translated(-a.parts.tab.topLeft())
            == b.parts.title.translated(-b.parts.tab.topLeft())
        && a.state.iconOnly == b.state.iconOnly
        && a.state.shape == b.state.shape;
}

bool sameMetrics(const TabMetrics& a, const TabMetrics& b)
{
    return a.rowHeight == b.rowHeight && a.edgeFade == b.edgeFade
        && a.titleFade == b.titleFade;
}

class RectangleClipNode : public QSGClipNode {
public:
    RectangleClipNode()
        : geometry_(QSGGeometry::defaultAttributes_Point2D(), 4, 6, QSGGeometry::UnsignedShortType)
    {
        geometry_.setDrawingMode(QSGGeometry::DrawTriangles);
        geometry_.setVertexDataPattern(QSGGeometry::StaticPattern);
        geometry_.setIndexDataPattern(QSGGeometry::StaticPattern);
        auto* indices = geometry_.indexDataAsUShort();
        indices[0] = 0; indices[1] = 1; indices[2] = 2;
        indices[3] = 2; indices[4] = 1; indices[5] = 3;
        setGeometry(&geometry_);
        setIsRectangular(false);
    }

    bool sync(const QRectF& rect, quint64 revision)
    {
        if (revision_ == revision) return false;
        auto* vertices = geometry_.vertexDataAsPoint2D();
        vertices[0].set(float(rect.left()), float(rect.top()));
        vertices[1].set(float(rect.right()), float(rect.top()));
        vertices[2].set(float(rect.left()), float(rect.bottom()));
        vertices[3].set(float(rect.right()), float(rect.bottom()));
        revision_ = revision;
        markDirty(QSGNode::DirtyGeometry);
        return true;
    }

private:
    QSGGeometry geometry_;
    quint64 revision_ = 0;
};

class SegmentNode final : public RectangleClipNode {
public:
    SegmentNode()
    {
        cutOpacity = new QSGOpacityNode;
        coverOpacity = new QSGOpacityNode;
        appendChildNode(cutOpacity);
        cutOpacity->appendChildNode(coverOpacity);
    }

    QSGOpacityNode* cutOpacity;
    QSGOpacityNode* coverOpacity;
    QSGTextNode* text = nullptr;
    quint64 textRevision = 0;
    QPointF textOrigin;
    QColor textColor;
};

class SceneNode final : public QSGTransformNode {
public:
    SceneNode()
    {
        presenceOpacity = new QSGOpacityNode;
        titleOpacity = new QSGOpacityNode;
        appendChildNode(presenceOpacity);
        presenceOpacity->appendChildNode(titleOpacity);
    }

    QSGOpacityNode* presenceOpacity;
    QSGOpacityNode* titleOpacity;
    QVector<SegmentNode*> segments;
    std::unique_ptr<QTextLayout> layout;
    quint64 layoutRevision = 0;
    QPointF layoutOrigin;
};

} // namespace

struct TabLabelTextItem::Prepared {
    TabLabelPresentation label;
    TabMetrics metrics;
    QColor color;
    TabTitleFade fade;
    QString text;
    QVector<TextSegment> segments;
    quint64 revision = 0;
    quint64 layoutRevision = 0;
};

TabLabelTextItem::TabLabelTextItem(QQuickItem* parent)
    : QQuickItem(parent), prepared_(std::make_unique<Prepared>())
{
    setFlag(ItemHasContents);
}

TabLabelTextItem::~TabLabelTextItem() = default;

bool TabLabelTextItem::setLabel(const TabLabelPresentation& label, const TabStripLabelStyle& style)
{
    if (label.parts.tab.isEmpty() || label.parts.title.isEmpty() || label.state.title.isEmpty()) {
        clearLabel();
        return true;
    }
    const auto text = visibleTitle(label);
    const auto color = tabLabelTextColor(label, style.palette);
    if (hasFrame_) {
        const auto& previous = *prepared_;
        const auto& old = previous.label;
        const bool unchanged = previous.text == text && previous.color == color
            && sameMetrics(previous.metrics, style.metrics)
            && old.font == label.font && old.direction == label.direction
            && old.parts.tab == label.parts.tab && old.parts.title == label.parts.title
            && old.parts.action == label.parts.action
            && old.state.iconOnly == label.state.iconOnly && old.state.shape == label.state.shape
            && old.state.selected == label.state.selected && old.state.hovered == label.state.hovered
            && old.state.lifted == label.state.lifted && old.state.actionShown == label.state.actionShown
            && old.state.actionReserved == label.state.actionReserved
            && nearlyEqual(old.state.titleOpacity, label.state.titleOpacity)
            && nearlyEqual(old.state.actionOpacity, label.state.actionOpacity)
            && old.naturalWidth == label.naturalWidth
            && old.preset == label.preset
            && nearlyEqual(old.presence.opacity, label.presence.opacity)
            && nearlyEqual(old.presence.scale, label.presence.scale)
            && old.presence.offset == label.presence.offset
            && nearlyEqual(previous.fade.actionOpacity, label.state.actionOpacity);
        if (unchanged) return true;
    }
    auto& next = *prepared_;
    const auto fade = tabLabelTitleFade(label, style.metrics);
    const bool layoutChanged = !hasFrame_ || !sameSource(next.label, label, next.text, text);
    ++next.revision;
    if (layoutChanged) next.layoutRevision = ++layoutRevision_;
    next.label = label;
    next.metrics = style.metrics;
    next.color = color;
    next.fade = fade;
    next.text = text;
    next.segments = titleSegments(label, fade);
    hasFrame_ = true;
    if (width() != label.parts.tab.width()) setWidth(label.parts.tab.width());
    if (height() != label.parts.tab.height()) setHeight(label.parts.tab.height());
    update();
    return true;
}

void TabLabelTextItem::clearLabel()
{
    hasFrame_ = false;
    prepared_ = std::make_unique<Prepared>();
    prepared_->revision = layoutRevision_;
    update();
}

TabLabelTextStats TabLabelTextItem::renderStats() const
{
    return {sceneGraphBuilds_.load(std::memory_order_relaxed),
            textLayoutBuilds_.load(std::memory_order_relaxed),
            clipUpdates_.load(std::memory_order_relaxed)};
}

qsizetype TabLabelTextItem::segmentCount() const { return prepared_->segments.size(); }

void TabLabelTextItem::itemChange(ItemChange change, const ItemChangeData&)
{
    if (change == ItemDevicePixelRatioHasChanged && hasFrame_)
        prepared_->layoutRevision = ++layoutRevision_;
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) update();
}

QSGNode* TabLabelTextItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!hasFrame_ || !window()) {
        delete previous;
        return nullptr;
    }
    auto* root = dynamic_cast<SceneNode*>(previous);
    if (!root) {
        delete previous;
        root = new SceneNode;
        sceneGraphBuilds_.fetch_add(1, std::memory_order_relaxed);
    }
    const auto& data = *prepared_;
    const auto& label = data.label;
    QMatrix4x4 presence;
    presence.translate(float(label.presence.offset.x()), float(label.presence.offset.y()));
    presence.scale(float(label.presence.scale));
    if (root->matrix() != presence) root->setMatrix(presence);
    if (root->presenceOpacity->opacity() != label.presence.opacity)
        root->presenceOpacity->setOpacity(label.presence.opacity);
    if (root->titleOpacity->opacity() != label.state.titleOpacity)
        root->titleOpacity->setOpacity(label.state.titleOpacity);

    // Build the complete single-line layout once. Every fade slice clips this
    // same shaping; no format range or substring changes complex text.
    QTextLayout* layout = root->layout.get();
    if (!layout || root->layoutRevision != data.layoutRevision) {
        root->layout = std::make_unique<QTextLayout>(data.text, label.font);
        root->layoutRevision = data.layoutRevision;
        textLayoutBuilds_.fetch_add(1, std::memory_order_relaxed);
        layout = root->layout.get();
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        option.setTextDirection(label.direction);
        option.setAlignment(Qt::AlignLeft);
        option.setFlags(QTextOption::IncludeTrailingSpaces);
        layout->setTextOption(option);
        layout->setCacheEnabled(true);
        layout->beginLayout();
        auto line = layout->createLine();
        if (line.isValid()) {
            line.setLineWidth(0x01000000);
            line.setPosition({});
        }
        layout->endLayout();
    }

    const auto title = QRectF(label.parts.title.translated(-label.parts.tab.topLeft()));
    QFontMetricsF metrics(label.font);
    const qreal lineWidth = layout->lineCount() > 0
        ? layout->lineAt(0).horizontalAdvance() : metrics.horizontalAdvance(data.text);
    const qreal lineHeight = layout->lineCount() > 0
        ? layout->lineAt(0).ascent() + layout->lineAt(0).descent() : metrics.height();
    const bool centered = label.state.iconOnly || label.state.shape == TabShape::Tile;
    const qreal x = centered ? (title.width() - lineWidth) / 2.0
        : label.direction == Qt::RightToLeft ? title.width() - lineWidth : 0;
    const qreal fixedY = std::floor((title.height() - lineHeight) / 2.0 * 64.0) / 64.0;
    root->layoutOrigin = QPointF(title.left() + x, title.top() + fixedY);

    while (root->segments.size() > data.segments.size()) {
        auto* node = root->segments.takeLast();
        root->titleOpacity->removeChildNode(node);
        delete node;
    }
    while (root->segments.size() < data.segments.size()) {
        auto* node = new SegmentNode;
        root->segments.append(node);
        root->titleOpacity->appendChildNode(node);
    }
    const auto color = data.color;
    for (qsizetype index = 0; index < data.segments.size(); ++index) {
        const auto& segment = data.segments[index];
        auto* node = root->segments[index];
        if (node->sync(segment.clip, data.revision + quint64(index) + 1))
            clipUpdates_.fetch_add(1, std::memory_order_relaxed);
        node->cutOpacity->setOpacity(segment.cutOpacity);
        node->coverOpacity->setOpacity(1 - label.state.actionOpacity * segment.coverFactor);
        if (!node->text) {
            node->text = window()->createTextNode();
            if (!node->text) continue;
            node->text->setRenderType(QSGTextNode::NativeRendering);
            node->text->setFiltering(QSGTexture::Linear);
            node->coverOpacity->appendChildNode(node->text);
        }
        node->text->setViewport(QRectF(QPointF(), label.parts.tab.size()));
        if (node->textRevision != data.layoutRevision || node->textOrigin != root->layoutOrigin
            || node->textColor != color) {
            node->text->clear();
            node->text->setColor(color);
            node->text->addTextLayout(root->layoutOrigin, layout);
            node->textRevision = data.layoutRevision;
            node->textOrigin = root->layoutOrigin;
            node->textColor = color;
        }
    }
    return root;
}

} // namespace window_system::quick
