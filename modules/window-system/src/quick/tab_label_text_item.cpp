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
#include <vector>

namespace window_system::quick {
namespace {

// QSGTextNode's viewport only culls lines vertically. Until it can consume
// visible shaped glyph runs, cap its complete input and the multiplied slice
// costs explicitly instead of assuming that a narrow clip bounds resources.
constexpr qsizetype maximumSourceUnits = 16384;
constexpr qsizetype maximumGlyphInstances = 128 * 1024;
constexpr qreal maximumGlyphPixels = 8 * 1024 * 1024;
constexpr int maximumFadeSteps = 1024;

bool nearlyEqual(qreal a, qreal b) { return qAbs(a - b) < 0.0001; }

bool initialOnly(const TabLabelPresentation& label, TabTextRole role)
{
    return role == TabTextRole::Title
        && (label.state.iconOnly || label.state.shape == TabShape::Tile);
}

struct TextVariant {
    std::unique_ptr<QTextLayout> layout;
    qreal advance = 0;
    qreal width = 0;
    qreal height = 0;
    qreal ascent = 0;
    qreal capHeight = 0;
    qsizetype glyphCount = 0;
    qreal glyphArea = 0;
};

struct ShapedText {
    std::vector<TextVariant> variants;
    bool hasLineSeparators = false;
};

std::shared_ptr<ShapedText> shapeText(const TabLabelPresentation& label, TabTextRole role)
{
    auto result = std::make_shared<ShapedText>();
    QString source = initialOnly(label, role)
        ? label.state.title.left(1).toUpper() : label.state.title;
    source.replace(QLatin1Char('\r'), QLatin1Char(' '));
    source.replace(QLatin1Char('\n'), QLatin1Char(' '));
    source.replace(QLatin1Char('\t'), QLatin1Char(' '));
    result->hasLineSeparators = source.contains(QChar::LineSeparator)
        || source.contains(QChar::ParagraphSeparator);
    const bool mnemonic = role == TabTextRole::Title && !initialOnly(label, role);
    // Match QPainter's length-variant selection and mnemonic formatting. These
    // formats are source semantics; opacity fades never add format boundaries.
    for (const auto& variant : source.split(QChar(0x009c))) {
        QString text;
        QList<QTextLayout::FormatRange> formats;
        text.reserve(variant.size());
        for (qsizetype index = 0; index < variant.size(); ++index) {
            if (mnemonic && variant[index] == QLatin1Char('&')) {
                if (++index == variant.size()) break;
                if (variant[index] != QLatin1Char('&')) {
                    QTextLayout::FormatRange format;
                    format.start = text.size();
                    format.length = 1;
                    format.format.setFontUnderline(true);
                    formats.append(format);
                }
            }
            text.append(variant[index]);
        }
        TextVariant shaped;
        shaped.layout = std::make_unique<QTextLayout>(text, label.font);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        option.setTextDirection(label.direction);
        option.setAlignment(Qt::AlignLeft);
        option.setFlags(QTextOption::IncludeTrailingSpaces);
        shaped.layout->setTextOption(option);
        shaped.layout->setFormats(formats);
        shaped.layout->setCacheEnabled(true);
        shaped.layout->beginLayout();
        const QFontMetricsF metrics(label.font);
        shaped.ascent = metrics.ascent();
        shaped.capHeight = metrics.capHeight();
        qreal height = -metrics.leading();
        while (true) {
            auto line = shaped.layout->createLine();
            if (!line.isValid()) break;
            line.setLineWidth(0x01000000);
            height = std::ceil(height + metrics.leading());
            line.setPosition({0, height});
            height += line.height();
            shaped.advance = std::max(shaped.advance, line.horizontalAdvance());
            shaped.width = std::max(shaped.width, line.naturalTextWidth());
            if (height >= label.parts.title.height()) break;
        }
        shaped.layout->endLayout();
        shaped.height = shaped.layout->lineCount() ? height : metrics.height();
        for (const auto& run : shaped.layout->glyphRuns()) {
            const auto font = run.rawFont();
            const auto glyphs = run.glyphIndexes();
            shaped.glyphCount += glyphs.size();
            for (const auto glyph : glyphs) {
                const auto bounds = font.boundingRect(glyph);
                shaped.glyphArea += (std::ceil(bounds.width()) + 2)
                    * (std::ceil(bounds.height()) + 2);
            }
        }
        result->variants.push_back(std::move(shaped));
    }
    return result;
}

struct TextSegment {
    QRectF clip;
    qreal cutOpacity = 1;
    qreal coverFactor = 0;
};

void appendSegment(QVector<TextSegment>& segments, qreal begin, qreal end,
                   const TextSegment& prototype)
{
    if (end <= begin) return;
    auto segment = prototype;
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
    const auto tabOrigin = label.parts.tab.topLeft();
    TextSegment shape;
    shape.clip = QRectF(label.parts.title.translated(-tabOrigin));
    if (!fade.active()) return {shape};
    const qreal localBegin = shape.clip.left(), localEnd = shape.clip.right();
    const auto local = [tabOrigin](qreal x) { return x - tabOrigin.x(); };
    QVector<qreal> boundaries{localBegin, localEnd};
    const auto addInterval = [&](qreal begin, qreal end) {
        begin = std::clamp(begin, fade.left, fade.right);
        end = std::clamp(end, begin, fade.right);
        if (end <= begin) return;
        boundaries.append(local(begin));
        boundaries.append(local(end));
        for (int x = int(std::floor(begin)); x < int(std::ceil(end)); ++x)
            boundaries.append(std::clamp(local(qreal(x)), localBegin, localEnd));
    };
    if (fade.cut && fade.fadeLength > 0)
        addInterval(fade.right - fade.fadeLength, fade.right);
    if (fade.covering)
        addInterval(fade.reverse ? fade.actionEnd : fade.actionEnd - fade.actionFade,
                    fade.reverse ? fade.actionEnd + fade.actionFade : fade.actionEnd);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    for (qsizetype index = 0; index + 1 < boundaries.size(); ++index) {
        const auto begin = boundaries[index], end = boundaries[index + 1];
        const auto sample = tabOrigin.x() + (begin + end) / 2;
        auto slice = shape;
        slice.cutOpacity = fade.cutOpacityAt(sample);
        slice.coverFactor = fade.coverFactorAt(sample);
        appendSegment(segments, begin, end, slice);
    }
    return segments;
}

bool sameSource(const TabLabelPresentation& a, const TabLabelPresentation& b,
                TabTextRole aRole, TabTextRole bRole, bool multiline)
{
    // A viewport change moves/clips already shaped glyphs. Only explicit line
    // separators make its height part of Qt's single-line formatting input.
    return aRole == bRole && a.state.title == b.state.title && a.font == b.font
        && a.direction == b.direction && initialOnly(a, aRole) == initialOnly(b, bRole)
        && (!multiline || a.parts.title.height() == b.parts.title.height());
}

bool sameFadeGeometry(const TabTitleFade& a, const TabTitleFade& b)
{
    return a.left == b.left && a.right == b.right && a.cut == b.cut
        && a.fadeLength == b.fadeLength && a.covering == b.covering
        && a.reverse == b.reverse && a.actionEnd == b.actionEnd && a.actionFade == b.actionFade;
}

class SegmentNode final : public QSGClipNode {
public:
    SegmentNode()
        : geometry_(QSGGeometry::defaultAttributes_Point2D(), 4, 6, QSGGeometry::UnsignedShortType)
    {
        geometry_.setDrawingMode(QSGGeometry::DrawTriangles);
        auto* indices = geometry_.indexDataAsUShort();
        indices[0] = 0; indices[1] = 1; indices[2] = 2;
        indices[3] = 2; indices[4] = 1; indices[5] = 3;
        setGeometry(&geometry_);
        setIsRectangular(false);
        appendChildNode(&opacity);
        opacity.setFlag(QSGNode::OwnedByParent, false);
    }
    bool sync(const QRectF& rect)
    {
        if (clip_ == rect) return false;
        clip_ = rect;
        QSGGeometry::updateRectGeometry(&geometry_, rect);
        markDirty(QSGNode::DirtyGeometry);
        return true;
    }
    QSGOpacityNode opacity;
    QSGTextNode* text = nullptr;
    quint64 textRevision = 0;
    qsizetype variant = -1;
    QPointF textOrigin;
    QColor textColor;
private:
    QSGGeometry geometry_;
    QRectF clip_;
};

class SceneNode final : public QSGTransformNode {
public:
    SceneNode()
    {
        appendChildNode(&opacity);
        opacity.setFlag(QSGNode::OwnedByParent, false);
    }
    QSGOpacityNode opacity;
    QVector<SegmentNode*> segments;
};

} // namespace

struct TabLabelTextItem::Prepared {
    TabLabelPresentation label;
    TabTextRole role = TabTextRole::Title;
    QColor color;
    TabTitleFade fade;
    QVector<TextSegment> segments;
    std::shared_ptr<ShapedText> shaped;
    qsizetype variant = 0;
    quint64 resourceRevision = 0;
};

TabLabelTextItem::TabLabelTextItem(QQuickItem* parent)
    : QQuickItem(parent), prepared_(std::make_unique<Prepared>())
{
    setFlag(ItemHasContents);
}
TabLabelTextItem::~TabLabelTextItem() = default;

bool TabLabelTextItem::setLabel(const TabLabelPresentation& label, const TabStripLabelStyle& style,
                               TabTextRole role)
{
    if (label.parts.tab.isEmpty() || label.parts.title.isEmpty() || label.state.title.isEmpty()) {
        clearLabel();
        return true;
    }
    if (label.state.title.size() > maximumSourceUnits) {
        clearLabel();
        error_ = TabTextError::SourceBudgetExceeded;
        return false;
    }
    auto& next = *prepared_;
    const bool sourceChanged = !hasFrame_ || !sameSource(next.label, label, next.role, role,
                                                       next.shaped->hasLineSeparators);
    auto shaped = sourceChanged ? shapeText(label, role) : next.shaped;
    if (sourceChanged)
        textLayoutBuilds_.fetch_add(shaped->variants.size(), std::memory_order_relaxed);
    qsizetype variant = 0;
    while (variant + 1 < qsizetype(shaped->variants.size())
           && (shaped->variants[variant].width > label.parts.title.width()
               || shaped->variants[variant].height > label.parts.title.height())) ++variant;
    auto fade = role == TabTextRole::Title ? tabLabelTitleFade(label, style.metrics) : TabTitleFade{};
    // Reserve covering slices at zero opacity too; revealing an action must
    // only update opacity, never create another complete set of glyph nodes.
    if (role == TabTextRole::Title && !initialOnly(label, role))
        fade.covering = label.state.actionShown && !label.state.actionReserved && !label.parts.action.isEmpty();
    if ((fade.cut && fade.fadeLength > maximumFadeSteps)
        || (fade.covering && fade.actionFade > maximumFadeSteps)) {
        clearLabel();
        error_ = TabTextError::SliceBudgetExceeded;
        return false;
    }
    const bool clipChanged = !hasFrame_ || !sameFadeGeometry(next.fade, fade)
        || next.label.parts.title.translated(-next.label.parts.tab.topLeft())
            != label.parts.title.translated(-label.parts.tab.topLeft());
    auto segments = clipChanged ? titleSegments(label, fade) : next.segments;
    const auto& active = shaped->variants[variant];
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    TabTextError error = TabTextError::None;
    if (active.glyphCount * segments.size() > maximumGlyphInstances)
        error = TabTextError::GlyphBudgetExceeded;
    else if (active.glyphArea * ratio * ratio * segments.size() > maximumGlyphPixels)
        error = TabTextError::PixelBudgetExceeded;
    if (error != TabTextError::None) {
        clearLabel();
        error_ = error;
        return false;
    }
    const auto color = tabLabelTextColor(label, style.palette);
    const bool changed = sourceChanged || clipChanged || next.variant != variant || next.color != color
        || next.label.parts.tab.size() != label.parts.tab.size()
        || next.label.presence.opacity != label.presence.opacity
        || next.label.presence.scale != label.presence.scale || next.label.presence.offset != label.presence.offset
        || next.label.state.titleOpacity != label.state.titleOpacity
        || next.fade.actionOpacity != fade.actionOpacity;
    if (!changed) return true;
    if (sourceChanged) {
        next.resourceRevision = ++resourceRevision_;
    }
    next.label = label;
    next.role = role;
    next.color = color;
    next.fade = fade;
    next.segments = std::move(segments);
    next.shaped = std::move(shaped);
    next.variant = variant;
    hasFrame_ = true;
    error_ = TabTextError::None;
    setSize(label.parts.tab.size());
    update();
    return true;
}

void TabLabelTextItem::clearLabel()
{
    if (!hasFrame_ && error_ == TabTextError::None) return;
    hasFrame_ = false;
    error_ = TabTextError::None;
    *prepared_ = {};
    update();
}

TabTextError TabLabelTextItem::textError() const { return error_; }
TabLabelTextStats TabLabelTextItem::renderStats() const
{
    return {sceneGraphBuilds_.load(std::memory_order_relaxed), textLayoutBuilds_.load(std::memory_order_relaxed),
            clipUpdates_.load(std::memory_order_relaxed), nativeTextBuilds_.load(std::memory_order_relaxed)};
}
qsizetype TabLabelTextItem::segmentCount() const { return prepared_->segments.size(); }
qsizetype TabLabelTextItem::retainedGlyphInstances() const
{
    return hasFrame_ ? prepared_->shaped->variants[prepared_->variant].glyphCount * prepared_->segments.size() : 0;
}

void TabLabelTextItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) {
        if (hasFrame_) {
            if (change == ItemDevicePixelRatioHasChanged)
                prepared_->resourceRevision = ++resourceRevision_;
            // Recheck the retained resource budget at the target DPR. The
            // source shaping is independent of scene-graph resource ownership.
            const auto& active = prepared_->shaped->variants[prepared_->variant];
            const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
            if (active.glyphArea * ratio * ratio * prepared_->segments.size() > maximumGlyphPixels) {
                clearLabel();
                error_ = TabTextError::PixelBudgetExceeded;
            }
        }
        update();
    }
}

QSGNode* TabLabelTextItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!hasFrame_ || !window()) {
        delete previous;
        return nullptr;
    }
    auto* root = previous ? static_cast<SceneNode*>(previous) : new SceneNode;
    if (!previous) sceneGraphBuilds_.fetch_add(1, std::memory_order_relaxed);
    const auto& data = *prepared_;
    const auto& label = data.label;
    QMatrix4x4 presence;
    presence.translate(float(label.presence.offset.x()), float(label.presence.offset.y()));
    presence.scale(float(label.presence.scale));
    if (root->matrix() != presence) root->setMatrix(presence);
    root->opacity.setOpacity(label.presence.opacity * label.state.titleOpacity);
    const auto title = QRectF(label.parts.title.translated(-label.parts.tab.topLeft()));
    const auto& shaped = data.shaped->variants[data.variant];
    const bool centered = initialOnly(label, data.role);
    const qreal x = centered ? (title.width() - shaped.advance) / 2.0
        : data.role == TabTextRole::Title && label.direction == Qt::RightToLeft
            ? title.width() - shaped.advance : 0;
    // Match Qt's 26.6 truncation and the numbered-tab cap-height baseline.
    const qreal y = data.role == TabTextRole::Number
        ? (title.height() + shaped.capHeight) / 2.0 - shaped.ascent
        : (title.height() - shaped.height) / 2.0;
    const QPointF origin(title.left() + qreal(qint64(x * 64)) / 64,
                         title.top() + qreal(qint64(y * 64)) / 64);
    while (root->segments.size() > data.segments.size()) {
        auto* node = root->segments.takeLast();
        root->opacity.removeChildNode(node);
        delete node;
    }
    while (root->segments.size() < data.segments.size()) {
        auto* node = new SegmentNode;
        root->segments.append(node);
        root->opacity.appendChildNode(node);
    }
    for (qsizetype index = 0; index < data.segments.size(); ++index) {
        const auto& segment = data.segments[index];
        auto* node = root->segments[index];
        if (node->sync(segment.clip)) clipUpdates_.fetch_add(1, std::memory_order_relaxed);
        node->opacity.setOpacity(segment.cutOpacity * (1 - data.fade.actionOpacity * segment.coverFactor));
        if (!node->text) {
            node->text = window()->createTextNode();
            Q_ASSERT(node->text);
            node->text->setRenderType(QSGTextNode::NativeRendering);
            node->text->setFiltering(QSGTexture::Linear);
            node->opacity.appendChildNode(node->text);
        }
        if (node->textRevision != data.resourceRevision || node->variant != data.variant
            || node->textOrigin != origin || node->textColor != data.color) {
            node->text->clear();
            node->text->setColor(data.color);
            node->text->setViewport(QRectF(QPointF(), label.parts.tab.size()));
            node->text->addTextLayout(origin, shaped.layout.get());
            nativeTextBuilds_.fetch_add(1, std::memory_order_relaxed);
            node->textRevision = data.resourceRevision;
            node->variant = data.variant;
            node->textOrigin = origin;
            node->textColor = data.color;
        }
    }
    return root;
}

} // namespace window_system::quick
