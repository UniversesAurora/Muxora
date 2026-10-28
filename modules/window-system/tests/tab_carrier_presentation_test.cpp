// SPDX-License-Identifier: MIT
// Carrier descriptors own their data; the raster adapter reuses styles and
// bounded text caches across movement and frame samples. All drawing targets
// are in-memory images, without exposed windows or fixed visual baselines.
// Dimensions: source lifetime, shared content/changed samples, font/content
// replacement, multiple pieces, cache removal, explicit clear, painter state.
#include "docking/tab_drag_visual.hpp"
#include "docking/tab_incoming_state.hpp"
#include "style/tab_carrier_painter.hpp"
#include "style/tab_painter.hpp"
#include "style/tab_departure.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTest>

using namespace window_system;

namespace {
TabParts parts(int y = 0)
{
    return {QRect(0, y, 240, 32), {}, {}, QRect(12, y, 212, 32), {}};
}

std::shared_ptr<TabCarrierLabels> labels(int count = 1)
{
    auto content = std::make_shared<TabCarrierLabels>();
    content->palette = TabPalette::forTheme(defaultTheme());
    for (int index = 0; index < count; ++index) {
        TabLabelPresentation label;
        label.parts = parts(index * 34);
        label.state.title = QStringLiteral("Carrier title %1").arg(index + 1);
        label.state.shape = TabShape::Row;
        label.state.selected = index == 0;
        label.font = QGuiApplication::font(); label.font.setPixelSize(15);
        content->labels.append({QUuid::createUuid(), std::move(label)});
    }
    return content;
}

std::shared_ptr<TabCarrierFrame> frame(std::shared_ptr<const TabCarrierLabels> content)
{
    auto sample = std::make_shared<TabCarrierFrame>();
    sample->content = std::move(content);
    for (const auto& label : sample->content->labels)
        sample->samples.append({label.label.parts, 1, 1});
    return sample;
}

TabDragVisual visual(std::shared_ptr<const TabCarrierFrame> sample)
{
    TabDragVisual result;
    result.size = {240, int(sample->samples.size()) * 34};
    result.pieces.append({QPoint(4, 4), std::move(sample)});
    return result;
}

void paint(TabCarrierPainter& renderer, const TabDragVisual& descriptor, const SurfaceGeometry* geometry = nullptr)
{
    QImage image(600, 200, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.paint(painter, descriptor, geometry);
}
} // namespace

class TabCarrierPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void incomingStateOwnsTheCarrierAndPredictsFromTheLatestPointer()
    {
        TabIncomingState state;
        TabLayout layout; layout.gap.append(QRect(20, 0, 120, 32));
        const TabDragArea area{{600, 100}, {0, 3}, 32, 4};
        const auto row = TabDragConstraint::HorizontalRow;
        auto content = labels(); std::weak_ptr<const TabCarrierLabels> owner = content;
        auto descriptor = visual(frame(content)); descriptor.size = {160, 32};
        descriptor.active = QRect(20, 0, 120, 32);
        descriptor.sourceAnchor = DragAnchor{{90, 7}, {120, 32}};
        descriptor.identity = content->labels.first().id;
        const auto id = descriptor.identity;
        QCOMPARE(state.replace(std::move(descriptor), {150, 40}, layout, area, row), TabIncomingState::Change::Enter);
        content.reset(); QVERIFY(!owner.expired());
        QCOMPARE(state.visual().identity, id);
        const auto displayed = state.rect(layout, area, row);
        QCOMPARE(displayed, QRect(40, 3, 160, 32));
        QCOMPARE(state.reorderOffset(), std::optional(displayed.center() - (layout.gap.first().center() + area.origin)));
        QCOMPARE(state.rectAt({450, 40}, layout, area, row).left(), 340);
        QCOMPARE(state.rect(layout, area, row), displayed);
        auto retained = state.visual();
        QCOMPARE(state.replace(retained, {450, 40}, layout, area, row), TabIncomingState::Change::Update);
        QCOMPARE(state.rect(layout, area, row).left(), 340);
        state.resetReorderOffset(); QVERIFY(!state.reorderOffset());
        QCOMPARE(state.replace({}, {}, layout, area, row), TabIncomingState::Change::Leave);
        QVERIFY(state.visual().empty()); QVERIFY(state.rect(layout, area, row).isEmpty());
        QVERIFY(!state.reorderOffset());
        QCOMPARE(state.replace({}, {10, 20}, layout, area, row), TabIncomingState::Change::None);
        QVERIFY(!owner.expired()); retained = {}; QVERIFY(owner.expired());
    }

    void incomingStateReprojectsTheOriginalAnchorAfterResizing()
    {
        TabIncomingState state; const TabLayout layout;
        const TabDragArea area{{600, 100}, {}, 32, 4};
        auto descriptor = visual(frame(labels())); descriptor.size = {100, 32};
        descriptor.active = QRect(20, 0, 60, 32);
        descriptor.sourceAnchor = DragAnchor{{90, 7}, {120, 32}};
        const auto row = TabDragConstraint::HorizontalRow;
        QCOMPARE(state.replace(std::move(descriptor), {150, 40}, layout, area, row), TabIncomingState::Change::Enter);
        QCOMPARE(state.rect(layout, area, row).left(), 85);
        const auto tile = state.rectAt({150, 40}, layout, area, TabDragConstraint::CompactTile);
        QCOMPARE(tile.topLeft(), QPoint(101, 25));
        QVERIFY(!state.reorderOffset());
    }

    void departuresOwnDataAndKeepRendererCachesOutsideTheFrame()
    {
        const auto id = QUuid::createUuid();
        TabVisualState transitions;
        QVector<TabDepartureSample> samples;
        const QPoint origin(300, 200);
        {
            const auto source = labels()->labels.first().label;
            auto retired = std::make_shared<TabRetiredLabel>();
            retired->label = source; retired->visibility = 0.8;
            TabLabelVisual visual{source.parts.tab, TabShape::Row, false, true, false, source.parts, retired};
            visual.global.translate(origin);
            for (auto* part : {&visual.parts.tab, &visual.parts.icon, &visual.parts.number, &visual.parts.title, &visual.parts.action})
                part->translate(origin);
            transitions.insert(id, visual);
        }
        sampleTabDepartures(transitions, origin, 0.5, samples);
        QCOMPARE(samples.size(), 1); QCOMPARE(samples.first().id, id);
        QCOMPARE(samples.first().label.parts.tab, parts().tab);
        QCOMPARE(samples.first().label.presence.opacity, 0.4);
        transitions.clear();
        QCOMPARE(samples.first().label.state.title, QStringLiteral("Carrier title 1"));
        QImage image(300, 80, QImage::Format_RGB32); image.fill(Qt::white);
        QPainter painter(&image);
        TabPainter style(TabMetrics{}, TabPalette::forTheme(defaultTheme()));
        TabDeparturePainter renderer;
        renderer.paint(painter, style, samples);
        QCOMPARE(renderer.retainedLabelCount(), 1);
        const auto shaped = renderer.shapingCount(); QVERIFY(shaped > 0);
        samples.first().label.presence = labelPresencePose(parts().tab, 0.3);
        renderer.paint(painter, style, samples);
        QCOMPARE(renderer.shapingCount(), shaped);
        samples.clear(); renderer.paint(painter, style, samples);
        QCOMPARE(renderer.retainedLabelCount(), 0);
    }


    void retainedDescriptor_outlivesTheSourceAndClearReleasesIt()
    {
        TabDragVisual descriptor;
        std::weak_ptr<const TabCarrierLabels> owner;
        {
            const auto source = labels();
            owner = source;
            descriptor = visual(frame(source));
        }
        QVERIFY(!owner.expired());
        TabCarrierPainter renderer;
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().pieces, qsizetype(1));
        QCOMPARE(renderer.cacheStats().labels, qsizetype(1));
        QVERIFY(renderer.cacheStats().shapingCount > 0);
        descriptor = {};
        QVERIFY(!owner.expired());
        renderer.clear();
        QVERIFY(owner.expired());
        QCOMPARE(renderer.cacheStats().pieces, qsizetype(0));
        QCOMPARE(renderer.cacheStats().labels, qsizetype(0));
        QCOMPARE(renderer.cacheStats().shapingCount, quint64(0));
    }

    void translationAndOpacitySamples_reuseTheSameTextLayout()
    {
        const auto content = labels();
        const auto first = frame(content);
        auto descriptor = visual(first);
        TabCarrierPainter renderer;
        paint(renderer, descriptor);
        const auto shaped = renderer.cacheStats().shapingCount;
        QVERIFY(shaped > 0);
        descriptor.pieces[0].offset += QPoint(45, 20);
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().shapingCount, shaped);

        auto next = std::make_shared<TabCarrierFrame>(*first);
        next->samples[0].parts = parts(12);
        next->samples[0].titleOpacity = 0.5;
        next->samples[0].actionOpacity = 0.25;
        descriptor.pieces[0].frame = next;
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().shapingCount, shaped);
        QCOMPARE(renderer.cacheStats().labels, qsizetype(1));
        QCOMPARE(renderer.cacheStats().pieces, qsizetype(1));
        QCOMPARE(next->content.get(), first->content.get());
        QCOMPARE(first->samples[0].parts.tab.top(), 0);
        QCOMPARE(first->samples[0].titleOpacity, qreal(1));
        QCOMPARE(content->labels[0].label.state.titleOpacity, qreal(1));
    }

    void replacedContent_invalidatesFontAndTitleButRetainsStableIdentity()
    {
        const auto content = labels();
        auto descriptor = visual(frame(content));
        TabCarrierPainter renderer;
        paint(renderer, descriptor);
        const auto initial = renderer.cacheStats().shapingCount;
        auto fontChange = std::make_shared<TabCarrierLabels>(*content);
        fontChange->labels[0].label.font.setPixelSize(19);
        descriptor.pieces[0].frame = frame(fontChange);
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().labels, qsizetype(1));
        QVERIFY(renderer.cacheStats().shapingCount > initial);
        const auto afterFont = renderer.cacheStats().shapingCount;

        auto titleChange = std::make_shared<TabCarrierLabels>(*fontChange);
        titleChange->labels[0].label.state.title = QStringLiteral("Replacement carrier title");
        descriptor.pieces[0].frame = frame(titleChange);
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().labels, qsizetype(1));
        QVERIFY(renderer.cacheStats().shapingCount > afterFont);
        const auto afterTitle = renderer.cacheStats().shapingCount;

        auto themeChange = std::make_shared<TabCarrierLabels>(*titleChange);
        themeChange->palette.active = Qt::yellow;
        themeChange->metrics.radius += 1;
        descriptor.pieces[0].frame = frame(themeChange);
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().shapingCount, afterTitle);
        QCOMPARE(content->labels[0].label.font.pixelSize(), 15);
        QCOMPARE(content->labels[0].label.state.title, QStringLiteral("Carrier title 1"));
        QCOMPARE(themeChange->labels[0].id, content->labels[0].id);
    }

    void multiplePieces_keepTheirStylesAndReleaseRemovedTextCaches()
    {
        const auto first = labels();
        auto second = labels();
        second->palette.active = Qt::cyan;
        second->labels[0].label.font.setPixelSize(17);
        auto descriptor = visual(frame(first));
        descriptor.pieces.append({QPoint(260, 4), frame(second)});
        TabCarrierPainter renderer;
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().pieces, qsizetype(2));
        QCOMPARE(renderer.cacheStats().labels, qsizetype(2));
        const auto shaped = renderer.cacheStats().shapingCount;
        std::swap(descriptor.pieces[0], descriptor.pieces[1]);
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().shapingCount, shaped);
        descriptor.pieces.removeLast();
        paint(renderer, descriptor);
        QCOMPARE(renderer.cacheStats().pieces, qsizetype(1));
        QCOMPARE(renderer.cacheStats().labels, qsizetype(1));
        paint(renderer, {});
        QCOMPARE(renderer.cacheStats().pieces, qsizetype(0));
        QCOMPARE(renderer.cacheStats().labels, qsizetype(0));
    }

    void geometryOverride_doesNotMutateTheFrameOrPainterState()
    {
        auto content = labels();
        content->labels[0].label.direction = Qt::RightToLeft;
        content->labels[0].label.state.outerRight = true;
        const auto descriptor = visual(frame(content));
        SurfaceGeometry geometry;
        geometry.anchor = QRectF(0, 0, 240, 32);
        geometry.anchorPath.addRoundedRect(geometry.anchor, 6, 6);
        geometry.mergedPath = geometry.anchorPath;
        QImage image(600, 200, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QPainter painter(&image);
        QFont originalFont = QGuiApplication::font(); originalFont.setPixelSize(11);
        painter.setFont(originalFont); painter.setLayoutDirection(Qt::LeftToRight);
        painter.translate(5, 8); painter.setOpacity(0.75);
        const auto originalTransform = painter.transform();
        TabCarrierPainter renderer;
        renderer.paint(painter, descriptor, &geometry);
        QCOMPARE(painter.font(), originalFont);
        QCOMPARE(painter.layoutDirection(), Qt::LeftToRight);
        QCOMPARE(painter.transform(), originalTransform);
        QCOMPARE(painter.opacity(), qreal(0.75));
        QVERIFY(!content->labels[0].label.surface);
        QVERIFY(!content->labels[0].label.state.lifted);
        QVERIFY(content->labels[0].label.state.outerRight);
        QCOMPARE(content->labels[0].label.direction, Qt::RightToLeft);
        QCOMPARE(geometry.anchor.topLeft(), QPointF(0, 0));
    }
};

QTEST_MAIN(TabCarrierPresentationTest)
#include "tab_carrier_presentation_test.moc"
