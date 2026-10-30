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

// Titles come from outside (a remote shell sets them). Shaping stops after
// this many UTF-16 units; the rest of such a title is never on screen.
constexpr qsizetype maximumSourceUnits = 16384;
// QSGTextNode's viewport only culls lines vertically, and every fade slice
// carries its own glyph nodes. The visible cut keeps this far from reach;
// only pathological text (thousands of zero-width marks) can exceed it.
constexpr qsizetype maximumGlyphInstances = 128 * 1024;
// A title this wide or wider is drawn from a visible cut, not whole.
constexpr qreal minimumCutCoverage = 2048;
// Extra graphemes on each side of a cut keep shaping context (kerning,
// joining) at its edges identical to the complete title.
constexpr int cutContext = 8;

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
};

qsizetype glyphCount(const QTextLayout& layout)
{
    qsizetype count = 0;
    for (const auto& run : layout.glyphRuns()) count += run.glyphIndexes().size();
    return count;
}

// The single-line part of `source` between two x positions, shaped alone
// and placed where that part is in the complete layout.
struct TextCut {
    std::shared_ptr<QTextLayout> layout;
    qreal offset = 0;
    qreal begin = 0;
    qreal end = 0;
    qsizetype glyphCount = 0;
};

TextCut cutText(const QTextLayout& source, qreal begin, qreal end)
{
    const auto line = source.lineAt(0);
    int first = line.xToCursor(begin), last = line.xToCursor(end);
    if (first > last) std::swap(first, last);
    for (int step = 0; step < cutContext; ++step) {
        first = source.previousCursorPosition(first);
        last = source.nextCursorPosition(last);
    }
    QList<QTextLayout::FormatRange> formats;
    for (auto format : source.formats()) {
        const int from = std::max(format.start, first), to = std::min(format.start + format.length, last);
        if (from >= to) continue;
        format.start = from - first; format.length = to - from;
        formats.append(format);
    }
    TextCut cut;
    cut.layout = std::make_shared<QTextLayout>(source.text().mid(first, last - first), source.font());
    cut.layout->setTextOption(source.textOption());
    cut.layout->setFormats(formats);
    cut.layout->setCacheEnabled(true);
    cut.layout->beginLayout();
    auto cutLine = cut.layout->createLine();
    cutLine.setLineWidth(0x01000000);
    cutLine.setPosition(line.position());
    cut.layout->endLayout();
    const qreal a = line.cursorToX(first), b = line.cursorToX(last);
    cut.offset = std::min(a, b);
    cut.begin = cut.offset;
    cut.end = std::max(a, b);
    cut.glyphCount = glyphCount(*cut.layout);
    return cut;
}

struct ShapedText {
    std::vector<TextVariant> variants;
    bool hasLineSeparators = false;
};

std::shared_ptr<ShapedText> shapeText(const TabLabelPresentation& label, TabTextRole role)
{
    auto result = std::make_shared<ShapedText>();
    QString source = initialOnly(label, role)
        ? label.state.title.left(1).toUpper() : label.state.title.left(maximumSourceUnits);
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
        shaped.glyphCount = glyphCount(*shaped.layout);
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
    // Empty while the whole variant is drawn.
    TextCut cut;
    quint64 resourceRevision = 0;
};

namespace {
// Where the layout's x = 0 is drawn, in item coordinates.
QPointF textOrigin(const TabLabelPresentation& label, TabTextRole role, const TextVariant& shaped)
{
    const auto title = QRectF(label.parts.title.translated(-label.parts.tab.topLeft()));
    const bool centered = initialOnly(label, role);
    const qreal x = centered ? (title.width() - shaped.advance) / 2.0
        : role == TabTextRole::Title && label.direction == Qt::RightToLeft
            ? title.width() - shaped.advance : 0;
    // Match Qt's 26.6 truncation and the numbered-tab cap-height baseline.
    const qreal y = role == TabTextRole::Number
        ? (title.height() + shaped.capHeight) / 2.0 - shaped.ascent
        : (title.height() - shaped.height) / 2.0;
    return {title.left() + qreal(qint64(x * 64)) / 64, title.top() + qreal(qint64(y * 64)) / 64};
}
} // namespace

TabLabelTextItem::TabLabelTextItem(QQuickItem* parent)
    : QQuickItem(parent), prepared_(std::make_unique<Prepared>())
{
    setFlag(ItemHasContents);
}
TabLabelTextItem::~TabLabelTextItem() = default;

void TabLabelTextItem::setLabel(const TabLabelPresentation& label, const TabStripLabelStyle& style,
                               TabTextRole role)
{
    if (label.parts.tab.isEmpty() || label.parts.title.isEmpty() || label.state.title.isEmpty()) {
        clearLabel();
        return;
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
    const bool clipChanged = !hasFrame_ || !sameFadeGeometry(next.fade, fade)
        || next.label.parts.title.translated(-next.label.parts.tab.topLeft())
            != label.parts.title.translated(-label.parts.tab.topLeft());
    auto segments = clipChanged ? titleSegments(label, fade) : next.segments;
    const auto color = tabLabelTextColor(label, style.palette);
    const bool changed = sourceChanged || clipChanged || next.variant != variant || next.color != color
        || next.label.parts.tab.size() != label.parts.tab.size()
        || next.label.presence.opacity != label.presence.opacity
        || next.label.presence.scale != label.presence.scale || next.label.presence.offset != label.presence.offset
        || next.label.state.titleOpacity != label.state.titleOpacity
        || next.fade.actionOpacity != fade.actionOpacity;
    if (!changed) return;
    if (sourceChanged || next.variant != variant) {
        next.resourceRevision = ++resourceRevision_;
        next.cut = {};
    }
    next.label = label;
    next.role = role;
    next.color = color;
    next.fade = fade;
    next.segments = std::move(segments);
    next.shaped = std::move(shaped);
    next.variant = variant;
    hasFrame_ = true;
    setSize(label.parts.tab.size());
    selectVisibleText();
    // Positions settle during this frame; check the cut again before syncing.
    polish();
}

void TabLabelTextItem::selectVisibleText()
{
    auto& data = *prepared_;
    const auto& shaped = data.shaped->variants[data.variant];
    auto visible = QRectF(data.label.parts.title.translated(-data.label.parts.tab.topLeft()));
    qreal margin = minimumCutCoverage / 2;
    if (const auto* target = window()) {
        visible &= mapRectFromScene(QRectF(QPointF(), target->size()));
        margin = std::max(margin, qreal(target->width()) / 2);
    }
    const qreal originX = textOrigin(data.label, data.role, shaped).x();
    const qreal begin = std::max(qreal(0), visible.left() - originX);
    const qreal end = std::min(shaped.advance, visible.right() - originX);
    if (shaped.layout->lineCount() != 1 || shaped.advance < std::max(minimumCutCoverage, 4 * margin)) data.cut = {};
    else if (!data.cut.layout || (end > begin && (begin < data.cut.begin || end > data.cut.end))) {
        data.cut = cutText(*shaped.layout, std::max(qreal(0), begin - margin), std::min(shaped.advance, end + margin));
        data.resourceRevision = ++resourceRevision_;
    }
    const auto glyphs = data.cut.layout ? data.cut.glyphCount : shaped.glyphCount;
    error_ = glyphs * data.segments.size() > maximumGlyphInstances ? TabTextError::GlyphBudgetExceeded : TabTextError::None;
    update();
}

void TabLabelTextItem::updatePolish()
{
    if (hasFrame_) selectVisibleText();
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
    if (!hasFrame_ || error_ != TabTextError::None) return 0;
    const auto& data = *prepared_;
    const auto glyphs = data.cut.layout ? data.cut.glyphCount : data.shaped->variants[data.variant].glyphCount;
    return glyphs * data.segments.size();
}

void TabLabelTextItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) {
        if (hasFrame_ && change == ItemDevicePixelRatioHasChanged)
            prepared_->resourceRevision = ++resourceRevision_;
        // Another window has another width and position: recheck the cut.
        if (hasFrame_) polish();
        update();
    }
}

QSGNode* TabLabelTextItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!hasFrame_ || !window() || error_ != TabTextError::None) {
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
    const auto& shaped = data.shaped->variants[data.variant];
    const auto origin = textOrigin(label, data.role, shaped) + QPointF(data.cut.offset, 0);
    auto* layout = data.cut.layout ? data.cut.layout.get() : shaped.layout.get();
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
            node->text->addTextLayout(origin, layout);
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
