// SPDX-License-Identifier: MIT
#include "tab_frame_scenes.hpp"

namespace window_system::benchmarks {
namespace {
constexpr int rowHeight = 28;
constexpr int labelWidth = 150;
constexpr int labelPitch = 158;
constexpr int incomingLabelsPerPiece = 6;
constexpr int localLabelsPerPlane = 8;
constexpr QPoint incomingOrigin(40, 140);

void placeLabel(TabLabelPresentation& label, QPoint origin)
{
    label.parts.tab = QRect(origin, QSize(labelWidth, rowHeight));
    label.parts.title = QRect(origin + QPoint(32, 0), QSize(108, rowHeight));
}

SurfaceGeometry detachedGeometry(const QRectF& anchor)
{
    SurfaceGeometryInput input;
    input.body = QRectF(0, 80, 1600, 720);
    input.anchor = anchor;
    input.adjacent = false;
    return surfaceGeometry(input);
}

} // namespace

TabBenchmarkFrames::TabBenchmarkFrames(const QColor& background, Scene scene)
{
    auto style = std::make_shared<TabStripLabelStyle>();
    style->metrics.rowHeight = rowHeight;
    style->metrics.rowGap = 2;
    style->palette.strip = background;
    style->palette.active = Qt::white;
    style->palette.text = QColor("#251b31");
    style->palette.mutedText = QColor("#665d70");
    style->palette.separator = QColor("#b7a8c9");
    style->font = QFont(QStringLiteral("Segoe UI"), 10);
    style_ = style;
    for (int index = 0; index < 30; ++index) {
        TabLabelPresentation label;
        label.state.shape = TabShape::Row;
        label.state.title = QStringLiteral("Workspace %1").arg(index + 1);
        label.font = style_->font;
        label.naturalWidth = 105;
        labels_.append({QUuid::createUuid(), index, {}, std::move(label)});
    }
    if (scene == Scene::Strip) initializeStrip();
    else initializeForeground();
}

void TabBenchmarkFrames::initializeStrip()
{
    for (auto& frame : strips_) {
        frame = std::make_shared<TabStripFrame>();
        frame->style = style_;
        frame->font = style_->font;
        TabStripLaneFrame lane;
        lane.viewport = lane.regularClip = QRect(0, 0, 1600, 160);
        for (int index = 0; index < labels_.size(); ++index) {
            const int column = index % 10, row = index / 10;
            auto sample = labels_[index];
            sample.label.state.selected = index == 0;
            placeLabel(sample.label, QPoint(8 + column * labelPitch, 8 + row * 32));
            lane.regular.append(std::move(sample));
            frame->separators.append(QRect(150 + column * labelPitch, 16 + row * 32, 1, 12));
        }
        frame->lanes.append(std::move(lane));
    }
}

std::shared_ptr<const TabStripFrame> TabBenchmarkFrames::stripFrame(qreal progress)
{
    auto& frame = strips_[strips_[0].use_count() == 1 ? 0 : 1];
    Q_ASSERT(frame && frame.use_count() == 1);
    const int offset = qRound(progress * 24);
    auto& labels = frame->lanes[0].regular;
    for (int index = 0; index < labels.size(); ++index) {
        const int column = index % 10;
        labels[index].label.parts.tab.moveLeft(8 + column * labelPitch + offset);
        labels[index].label.parts.title.moveLeft(40 + column * labelPitch + offset);
        frame->separators[index].moveLeft(150 + column * labelPitch + offset);
    }
    return frame;
}

void TabBenchmarkFrames::initializeIncoming(ForegroundBuffer& buffer, const SurfaceGeometry& geometry)
{
    TabIncomingSample incoming;
    incoming.origin = incomingOrigin;
    incoming.geometry = geometry;
    incoming.visual.size = QSize(incomingLabelsPerPiece * labelPitch, 68);
    incoming.visual.anchor = QPoint(labelWidth / 2, rowHeight / 2);
    incoming.visual.active = QRect(QPoint(), QSize(labelWidth, rowHeight));
    incoming.visual.identity = labels_[0].id;
    for (int piece = 0; piece < incomingPieces; ++piece) {
        if (!carrierContent_[piece]) {
            auto content = std::make_shared<TabCarrierLabels>();
            content->metrics = style_->metrics;
            content->palette = style_->palette;
            for (int index = 0; index < incomingLabelsPerPiece; ++index) {
                const auto& source = labels_[piece * incomingLabelsPerPiece + index];
                auto label = source.label;
                label.state.selected = piece == 0 && index == 0;
                placeLabel(label, QPoint(index * labelPitch, 0));
                content->labels.append({source.id, std::move(label)});
            }
            carrierContent_[piece] = std::move(content);
        }
        auto& carrier = buffer.carriers[piece];
        carrier = std::make_shared<TabCarrierFrame>();
        carrier->content = carrierContent_[piece];
        for (const auto& source : carrier->content->labels)
            carrier->samples.append({source.label.parts, 1, 1});
        incoming.visual.pieces.append({QPoint(0, piece * 40), carrier});
    }
    buffer.frame->incoming = std::move(incoming);
}

void TabBenchmarkFrames::initializeLocalLabels(TabForegroundFrame& frame)
{
    for (int index = 0; index < localLabelsPerPlane; ++index) {
        auto carried = labels_[12 + index];
        carried.label.state.selected = index == 0;
        carried.label.state.elevationManaged = true;
        placeLabel(carried.label, QPoint(228 + index * labelPitch, 320));
        frame.carried.append(std::move(carried));
        auto settling = labels_[20 + index];
        settling.label.state.selected = index == 0;
        settling.label.state.elevationManaged = true;
        placeLabel(settling.label, QPoint(40 + index * labelPitch, 520));
        frame.settling.append(std::move(settling));
    }
    TabFolderPaintState folder;
    folder.name = QStringLiteral("Carried folder");
    folder.count = localLabelsPerPlane;
    folder.color = QColor("#8a6fbd");
    folder.wash = style_->palette.strip;
    folder.washText = style_->palette.text;
    frame.folder = TabStripFolderSample{QRect(40, 320, 180, rowHeight), folder, folderId_};
    frame.folderLine = TabStripFolderLine{QRect(44, 346, 1440, 2), folder.color};
}

void TabBenchmarkFrames::initializeForeground()
{
    const auto incomingGeometry = detachedGeometry(QRectF(0, 0, labelWidth, rowHeight));
    for (auto& buffer : foregrounds_) {
        buffer.frame = std::make_shared<TabForegroundFrame>();
        auto& frame = *buffer.frame;
        frame.style = style_;
        frame.font = style_->font;
        frame.elevation = style_->palette.elevation;
        initializeIncoming(buffer, incomingGeometry);
        initializeLocalLabels(frame);
        const auto flat = elevationProfile(ElevationLevel::Flat);
        const auto lifted = elevationProfile(ElevationLevel::Lifted);
        frame.shadows.append({translatedSurfaceGeometry(incomingGeometry, incomingOrigin), flat, lifted});
        frame.shadows.append({detachedGeometry(frame.carried[0].label.parts.tab), flat, lifted});
        frame.shadows.append({detachedGeometry(frame.settling[0].label.parts.tab), flat, lifted});
    }
}

std::shared_ptr<const TabForegroundFrame> TabBenchmarkFrames::foregroundFrame(qreal progress)
{
    auto& buffer = foregrounds_[foregrounds_[0].frame.use_count() == 1 ? 0 : 1];
    auto& frame = buffer.frame;
    Q_ASSERT(frame && frame.use_count() == 1);
    const QPoint offset(qRound(progress * 24), qRound(progress * 8));
    frame->incoming->origin = incomingOrigin + offset;
    frame->shadows[0].presence.offset = offset;
    for (auto& carrier : buffer.carriers)
        for (auto& sample : carrier->samples) sample.titleOpacity = .45 + .5 * progress;
    for (auto& sample : frame->carried) {
        sample.label.presence = labelPresencePose(sample.label.parts.tab, .55 + .4 * progress);
        sample.label.presence.offset += QPointF(offset.x(), 0);
    }
    for (auto& sample : frame->settling) {
        sample.label.presence = labelPresencePose(sample.label.parts.tab, .65 + .35 * progress);
        sample.label.presence.offset += QPointF(20 * (1 - progress), 0);
    }
    frame->shadows[1].presence = frame->carried[0].label.presence;
    frame->shadows[2].presence = frame->settling[0].label.presence;
    frame->folder->area.moveLeft(40 + offset.x());
    frame->folderLine->area.moveLeft(44 + offset.x());
    return frame;
}

} // namespace window_system::benchmarks
