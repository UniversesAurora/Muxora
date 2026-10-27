// SPDX-License-Identifier: MIT
// Carrier descriptors own their data; the Widgets adapter reuses styles and
// bounded text caches across movement and frame samples. All drawing targets
// are in-memory images, without exposed windows or fixed visual baselines.
// Dimensions: source lifetime, shared content/changed samples, font/content
// replacement, multiple pieces, cache removal, explicit clear, painter state.
#include "docking/tab_drag_visual.hpp"
#include "style/tab_carrier_painter.hpp"

#include <QApplication>
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
        label.font = QApplication::font(); label.font.setPixelSize(15);
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
        QFont originalFont = QApplication::font(); originalFont.setPixelSize(11);
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
