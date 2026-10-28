// SPDX-License-Identifier: MIT
// Label presentation ownership and the synchronous Widgets adapter contract.
// Dimensions: source lifetime, absent/owned style data, copy-on-write paths
// and presets, explicit text caches, painter font/direction state restoration.
// No exposed windows, native input, or fixed visual baselines.
#include "style/tab_painter.hpp"
#include "docking/tab_bar.hpp"

#include <QApplication>
#include <QStyle>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QScopeGuard>
#include <QTest>

using namespace window_system;

namespace {
TabParts labelParts()
{
    return {QRect(2, 2, 230, 36), {}, QRect(10, 12, 16, 16), QRect(34, 2, 170, 36), QRect(207, 10, 20, 20)};
}

TabPainter labelPainter()
{
    return TabPainter(TabMetrics{}, TabPalette::forTheme(defaultTheme()));
}
} // namespace

class TabLabelPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void foregroundFramesOwnLocalAndIncomingDragData()
    {
        const bool animations = TabBar::animationsAllowed(); TabBar::setAnimationsEnabled(false);
        const auto restore = qScopeGuard([=] { TabBar::setAnimationsEnabled(animations); });
        for (auto side : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right}) {
            auto bar = std::make_unique<TabBar>(defaultTheme(), nullptr);
            bar->setAppearance(TabAppearance{}, side);
            bar->resize(bar->vertical() ? QSize(220, 300) : QSize(700, 80)); bar->setAvailableSize(bar->size());
            for (int index = 0; index < 4; ++index) { bar->addTab({}, QStringLiteral("Workspace %1").arg(index)); bar->setTabData(index, QUuid::createUuid()); }
            bar->setCurrentIndex(1);
            const auto id = bar->tabData(1).toUuid(); const auto area = bar->tabRect(1);
            const auto point = area.center(), grab = area.center() - area.topLeft();
            bar->setDraggedTab(id); bar->setDropPreview(1, false, {bar->tabExtent(1, false)}); bar->setLifted(point, grab);
            const auto local = bar->captureForegroundFrame(bar->font(), bar->layoutDirection());
            QCOMPARE(local->carried.size(), 1); QCOMPARE(local->carried.first().id, id);
            QCOMPARE(local->carried.first().label.parts.tab, bar->liftedRect());
            QVERIFY(local->carried.first().label.state.lifted);
            QCOMPARE(local->carried.first().label.state.outerRight, side == TabBarPlacement::Right);
            const auto original = local->carried.first().label.parts;
            bar->setLifted({}); bar->setDraggedTab({}); bar->setDropPreview(-1, false, {});
            const auto bounds = bar->carrierBounds(); TabDragVisual visual;
            visual.identity = id; visual.size = bounds.size(); visual.active = area.translated(-bounds.topLeft());
            visual.anchor = point - bounds.topLeft(); visual.pieces.append({-bounds.topLeft(), bar->carrierFrame()});
            bar->setIncomingVisual(std::move(visual), point);
            const auto incoming = bar->captureForegroundFrame(bar->font(), bar->layoutDirection());
            QVERIFY(incoming->incoming); QCOMPARE(incoming->incoming->origin, bar->incomingRect().topLeft());
            bar.reset();
            QCOMPARE(local->carried.first().label.parts.tab, original.tab);
            QCOMPARE(local->carried.first().label.state.title, QStringLiteral("Workspace 1"));
            QCOMPARE(incoming->incoming->visual.pieces.first().frame->content->labels.at(1).id, id);
        }
    }

    void foregroundFolderFramesRetainTheHeaderIdentity()
    {
        const bool animations = TabBar::animationsAllowed(); TabBar::setAnimationsEnabled(false);
        const auto restore = qScopeGuard([=] { TabBar::setAnimationsEnabled(animations); });
        auto bar = std::make_unique<TabBar>(defaultTheme(), nullptr);
        bar->setAppearance(TabAppearance{}, TabBarPlacement::Left); bar->resize(220, 300); bar->setAvailableSize(bar->size());
        for (int index = 0; index < 4; ++index) { bar->addTab({}, QStringLiteral("Workspace %1").arg(index)); bar->setTabData(index, QUuid::createUuid()); }
        TabStripFolder folder{QUuid::createUuid(), QStringLiteral("Folder"), {}, false, 1, 3};
        bar->setFolders({folder}); const auto header = bar->folderHeaderRect(0);
        const auto base = bar->captureStripFrame(bar->font(), bar->layoutDirection());
        QCOMPARE(base->lanes.first().folders.first().id, folder.id);
        bar->setDraggedFolder(folder.id); bar->setLifted(header.center(), header.center() - header.topLeft());
        const auto carried = bar->captureForegroundFrame(bar->font(), bar->layoutDirection());
        QVERIFY(carried->folder); QCOMPARE(carried->carried.size(), 3); QVERIFY(carried->folderLine);
        bar.reset();
        QCOMPARE(carried->folder->id, folder.id); QCOMPARE(carried->folder->state.name, folder.name);
        QCOMPARE(carried->carried.first().label.state.title, QStringLiteral("Workspace 1"));
    }

    void stripFramesOwnTheirLabelsAndKeepFrozenLayersSeparate()
    {
        const bool animations = TabBar::animationsAllowed(); TabBar::setAnimationsEnabled(false);
        const auto restore = qScopeGuard([=] { TabBar::setAnimationsEnabled(animations); });
        auto bar = std::make_unique<TabBar>(defaultTheme(), nullptr);
        TabAppearance policy; policy.stickySizing = StickyTabSizing::Compact;
        policy.showNewTabButton = false; bar->setAppearance(policy, TabBarPlacement::Top);
        bar->resize(500, 40); bar->setAvailableSize(bar->size());
        for (int index = 0; index < 10; ++index) {
            bar->addTab({}, QStringLiteral("Workspace %1").arg(index)); bar->setTabData(index, QUuid::createUuid());
        }
        bar->setStickyCount(2); bar->setCurrentIndex(1);
        const auto original = bar->captureStripFrame(bar->font(), bar->layoutDirection());
        QCOMPARE(original->lanes.size(), 1);
        const auto& lane = original->lanes.first();
        QCOMPARE(lane.frozen.size(), 2); QCOMPARE(lane.frozen.last().sourceIndex, 1);
        QVERIFY(lane.frozen.last().label.state.selected);
        QVERIFY(lane.regularClip.left() > lane.viewport.left());
        for (const auto& label : lane.regular) {
            QVERIFY(label.sourceIndex >= 2); QVERIFY(!label.label.state.selected);
            QVERIFY(label.label.parts.tab.intersects(lane.regularClip));
        }
        const auto id = lane.frozen.last().id;
        bar->setTabText(1, QStringLiteral("Changed")); bar->setCurrentIndex(5);
        bar->ensureVisible(5);
        const auto changed = bar->captureStripFrame(bar->font(), bar->layoutDirection());
        QCOMPARE(changed->lanes.first().regular.last().sourceIndex, 5);
        QVERIFY(changed->lanes.first().regular.last().label.state.selected);
        bar->setScrollOffset(bar->maximumScrollOffset());
        const auto scrolled = bar->captureStripFrame(bar->font(), bar->layoutDirection());
        QVERIFY(!scrolled->lanes.first().fades.isEmpty());
        bar.reset();
        QCOMPARE(lane.frozen.last().id, id);
        QCOMPARE(lane.frozen.last().label.state.title, QStringLiteral("Workspace 1"));
        QCOMPARE(lane.frozen.last().content->title, QStringLiteral("Workspace 1"));
        QCOMPARE(changed->lanes.first().frozen.last().label.state.title, QStringLiteral("Changed"));
        QVERIFY(!original->style->closeIcon.isNull());
    }

    void stripFramesDescribeAllSidesWithoutWidgetPainting()
    {
        const bool animations = TabBar::animationsAllowed(); TabBar::setAnimationsEnabled(false);
        const auto restore = qScopeGuard([=] { TabBar::setAnimationsEnabled(animations); });
        for (auto side : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right}) {
            TabBar bar(defaultTheme(), nullptr); TabAppearance policy;
            policy.showNewTabButton = false; policy.multiline = true;
            bar.setAppearance(policy, side); bar.resize(bar.vertical() ? QSize(220, 240) : QSize(500, 140));
            bar.setAvailableSize(bar.size());
            for (int index = 0; index < 7; ++index) { bar.addTab({}, QStringLiteral("Workspace %1").arg(index)); bar.setTabData(index, QUuid::createUuid()); }
            bar.setCurrentIndex(3); bar.setUpdatesEnabled(false);
            const auto frame = bar.captureStripFrame(bar.font(), bar.layoutDirection());
            QCOMPARE(frame->vertical, bar.vertical()); QVERIFY(!frame->lanes.isEmpty());
            int active = 0;
            for (const auto& lane : frame->lanes) for (const auto& sample : lane.regular) {
                QCOMPARE(sample.id, bar.tabData(sample.sourceIndex).toUuid());
                QCOMPARE(sample.label.state.outerRight, side == TabBarPlacement::Right);
                QCOMPARE(sample.label.surface.has_value(), sample.label.state.selected);
                if (sample.label.state.selected) { ++active; QCOMPARE(sample.sourceIndex, 3); }
            }
            QCOMPARE(active, 1);
        }
    }

    void sharedContentChangesOnlyWithItsSourceAndOutlivesTheBar()
    {
        auto bar = std::make_unique<TabBar>(defaultTheme(), nullptr);
        TabAppearance policy; policy.showNumbers = true;
        TabColorPreset preset; preset.id = QStringLiteral("custom"); policy.colors = {preset};
        bar->setAppearance(policy, TabBarPlacement::Top);
        bar->addTab({}, QStringLiteral("First")); bar->addTab({}, QStringLiteral("Second"));
        const auto first = bar->labelContent(0), second = bar->labelContent(1);
        QCOMPARE(bar->labelContent(0), first);
        bar->setCurrentIndex(1);
        QCOMPARE(bar->labelContent(0), first);
        bar->setTabText(0, QStringLiteral("Edited"));
        const auto edited = bar->labelContent(0);
        QVERIFY(edited != first); QCOMPARE(first->title, QStringLiteral("First"));
        QCOMPARE(bar->labelContent(1), second);
        bar->setTabIcon(0, iconFor(QStringLiteral("files")));
        QVERIFY(!bar->labelContent(0)->icon.isNull()); QVERIFY(edited->icon.isNull());
        TabRecord record; record.colorId = preset.id; bar->setTabMetadata(0, record);
        const auto retained = bar->labelContent(0);
        QVERIFY(retained->preset); QCOMPARE(retained->preset->id, preset.id);
        QCOMPARE(bar->tabPreset(0), &*retained->preset);
        bar.reset();
        QCOMPARE(retained->title, QStringLiteral("Edited"));
        QCOMPARE(retained->number, QStringLiteral("1"));
        QVERIFY(!retained->style->closeIcon.isNull());
    }

    void contentTracksIndexFontDirectionAndPlacement()
    {
        TabBar bar(defaultTheme(), nullptr);
        TabAppearance policy; policy.showNumbers = true;
        bar.setAppearance(policy, TabBarPlacement::Top);
        for (int index = 0; index < 12; ++index) bar.addTab({}, QStringLiteral("Title %1").arg(index));
        const auto original = bar.labelContent(10);
        QCOMPARE(original->number, QStringLiteral("11"));
        bar.removeTab(0);
        const auto shifted = bar.labelContent(9);
        QCOMPARE(shifted->title, original->title); QCOMPARE(shifted->number, QStringLiteral("10"));
        QCOMPARE(shifted->numberExtent, bar.fontMetrics().horizontalAdvance(QStringLiteral("10")));
        QCOMPARE(original->number, QStringLiteral("11"));
        auto font = bar.font(); font.setPixelSize(21); bar.setFont(font);
        const auto resized = bar.labelContent(9);
        QVERIFY(resized->style != shifted->style); QCOMPARE(resized->style->font, font);
        QCOMPARE(resized->numberExtent, bar.fontMetrics().horizontalAdvance(QStringLiteral("10")));
        bar.setLayoutDirection(Qt::RightToLeft);
        QCOMPARE(bar.labelContent(9)->style->direction, Qt::RightToLeft);
        bar.setAppearance(policy, TabBarPlacement::Right);
        QCOMPARE(bar.labelContent(9)->style->stickyIcon.name(), QStringLiteral("sticky-vertical"));
        policy.showNumbers = false; bar.setAppearance(policy, TabBarPlacement::Right);
        QVERIFY(bar.labelContent(9)->number.isEmpty()); QCOMPARE(bar.labelContent(9)->numberExtent, 0);
    }

    void sharedGeometryPreservesTheWidgetMirrorAndExtent()
    {
        for (auto direction : {Qt::LeftToRight, Qt::RightToLeft, Qt::LayoutDirectionAuto})
            for (const QRect bounds : {QRect(0, 0, 240, 28), QRect(-80, 12, 230, 42)})
                for (const QRect label : {QRect(-10, 2, 80, 24), QRect(40, 4, 0, 20), QRect(120, 3, 90, 32)})
                    QCOMPARE(visualTabLabelRect(direction, bounds, label), QStyle::visualRect(direction, bounds, label));
        QCOMPARE(clampedExtent(-1), 0);
        QCOMPARE(clampedExtent(qint64(QWIDGETSIZE_MAX) + 123), QWIDGETSIZE_MAX);
    }

    void capturedLabel_outlivesItsSourceWidgetAndBorrowedData()
    {
        TabLabelPresentation captured;
        QPainterPath expectedPath;
        expectedPath.addRoundedRect(QRectF(2, 2, 230, 36), 6, 6);
        QFont expectedFont = QApplication::font(); expectedFont.setPixelSize(19);
        {
            TabBar source(defaultTheme(), nullptr);
            QPixmap glyph(16, 16); glyph.fill(Qt::magenta);
            source.addTab(QIcon(glyph), QStringLiteral("Owned label"));
            source.setFont(expectedFont); source.setLayoutDirection(Qt::RightToLeft);
            std::optional<SurfaceGeometry> geometry{SurfaceGeometry{}};
            geometry->anchorPath = expectedPath;
            std::optional<ElevationProfile> profile{elevationProfile(ElevationLevel::Lifted)};
            std::optional<TabColorPreset> preset{TabColorPreset{}};
            preset->id = QStringLiteral("captured");
            preset->groupActive.background = Qt::green;
            TabTitleLayout title;
            TabPaintState state;
            state.title = source.tabText(0); state.icon = source.tabIcon(0);
            state.number = QStringLiteral("7"); state.shape = TabShape::Row;
            state.selected = true; state.outerRight = true; state.actionOpacity = 0.4;
            state.surface = &*geometry; state.bodyElevation = &*profile;
            state.preset = &*preset; state.titleLayout = &title;
            captured = TabPainter::captureLabel(labelParts(), state, source.font(), source.layoutDirection());
            state.title = QStringLiteral("Changed source"); state.number.clear(); state.icon = {};
            state.selected = false; state.actionOpacity = 1;
            geometry.reset(); profile.reset(); preset.reset();
        }
        QCOMPARE(captured.state.title, QStringLiteral("Owned label"));
        QCOMPARE(captured.state.number, QStringLiteral("7"));
        QVERIFY(!captured.state.icon.isNull());
        QVERIFY(captured.state.selected); QVERIFY(captured.state.outerRight);
        QCOMPARE(captured.state.shape, TabShape::Row);
        QCOMPARE(captured.state.actionOpacity, qreal(0.4));
        QCOMPARE(captured.parts.tab, labelParts().tab);
        QCOMPARE(captured.font, expectedFont); QCOMPARE(captured.direction, Qt::RightToLeft);
        QVERIFY(captured.surface.has_value()); QCOMPARE(captured.surface->anchorPath, expectedPath);
        QVERIFY(captured.bodyElevation.has_value());
        QCOMPARE(captured.bodyElevation->key.blur, elevationProfile(ElevationLevel::Lifted).key.blur);
        QVERIFY(captured.preset.has_value()); QCOMPARE(captured.preset->id, QStringLiteral("captured"));
        QCOMPARE(captured.preset->groupActive.background, QColor(Qt::green));
    }

    void optionalStyle_isCopiedOnlyWhenPresent()
    {
        const auto captured = TabPainter::captureLabel(labelParts(), {}, QApplication::font(), Qt::LeftToRight);
        QVERIFY(!captured.surface); QVERIFY(!captured.bodyElevation); QVERIFY(!captured.preset);
        QCOMPARE(captured.state.shape, TabShape::Top);
        QCOMPARE(captured.state.titleOpacity, qreal(1));
        QVERIFY(captured.state.enabled);
    }

    void copiedPathsAndPresets_areIsolatedFromLaterChanges()
    {
        SurfaceGeometry geometry;
        geometry.anchorPath.addRoundedRect(QRectF(2, 2, 230, 36), 6, 6);
        geometry.mergedPath = geometry.anchorPath;
        TabColorPreset preset; preset.id = QStringLiteral("original");
        preset.groupActive.text = Qt::blue;
        TabPaintState state; state.surface = &geometry; state.preset = &preset;
        const auto captured = TabPainter::captureLabel(labelParts(), state, QApplication::font(), Qt::LeftToRight);
        const auto expectedPath = geometry.anchorPath;
        geometry.anchorPath.clear(); geometry.mergedPath.clear();
        preset.id = QStringLiteral("source changed"); preset.groupActive.text = Qt::red;
        QCOMPARE(captured.surface->anchorPath, expectedPath);
        QCOMPARE(captured.surface->mergedPath, expectedPath);
        QCOMPARE(captured.preset->id, QStringLiteral("original"));
        QCOMPARE(captured.preset->groupActive.text, QColor(Qt::blue));

        auto copy = captured;
        copy.surface->anchorPath.setElementPositionAt(0, -100, -100);
        copy.preset->id = QStringLiteral("copy changed");
        copy.preset->groupActive.text = Qt::yellow;
        QVERIFY(copy.surface->anchorPath != captured.surface->anchorPath);
        QCOMPARE(captured.surface->anchorPath, expectedPath);
        QCOMPARE(captured.preset->id, QStringLiteral("original"));
        QCOMPARE(captured.preset->groupActive.text, QColor(Qt::blue));
    }

    void painting_usesTheExplicitPersistentCacheAndRestoresPainterState_data()
    {
        QTest::addColumn<bool>("matchingTextState");
        QTest::newRow("matching-text-state") << true;
        QTest::newRow("detached-text-state") << false;
    }

    void painting_usesTheExplicitPersistentCacheAndRestoresPainterState()
    {
        QFETCH(bool, matchingTextState);
        auto style = labelPainter();
        QImage image(250, 48, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QFont font = QApplication::font(); font.setPixelSize(18);
        TabTitleLayout capturedCache;
        TabPaintState state; state.title = QStringLiteral("Cached label title");
        state.titleLayout = &capturedCache; state.elevationManaged = true;
        const auto captured = TabPainter::captureLabel(labelParts(), state, font, Qt::RightToLeft);
        TabTitleLayout targetCache;
        targetCache.prepare(captured.state.title, captured.font, captured.direction, true, &image);
        QPainter painter(&image);
        auto originalFont = matchingTextState ? font : QApplication::font();
        if (!matchingTextState) originalFont.setPixelSize(11);
        const auto originalDirection = matchingTextState ? Qt::RightToLeft : Qt::LeftToRight;
        painter.setFont(originalFont); painter.setLayoutDirection(originalDirection);
        painter.setOpacity(0.7); painter.setPen(Qt::darkGreen);
        const auto originalPen = painter.pen();
        const auto originalTransform = painter.transform();
        style.paintLabel(painter, captured, &targetCache);
        QVERIFY(targetCache.retainedGlyphCount() > 0);
        QVERIFY(targetCache.shapingCount() > 0);
        QCOMPARE(capturedCache.shapingCount(), quint64(0));
        QCOMPARE(painter.font(), originalFont); QCOMPARE(painter.layoutDirection(), originalDirection);
        QCOMPARE(painter.opacity(), qreal(0.7)); QCOMPARE(painter.pen(), originalPen);
        QCOMPARE(painter.transform(), originalTransform);
        const auto shaped = targetCache.shapingCount();
        style.paintLabel(painter, captured, &targetCache);
        QCOMPARE(targetCache.shapingCount(), shaped);
    }

    void paintingWithoutACache_doesNotRetainOrPrepareBorrowedText()
    {
        auto style = labelPainter();
        QImage image(250, 48, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        TabTitleLayout sourceCache;
        TabPaintState state; state.title = QStringLiteral("No implicit shaping");
        state.titleLayout = &sourceCache; state.elevationManaged = true;
        const auto captured = TabPainter::captureLabel(labelParts(), state, QApplication::font(), Qt::LeftToRight);
        QPainter painter(&image);
        style.paintLabel(painter, captured, nullptr);
        QCOMPARE(sourceCache.shapingCount(), quint64(0));
        QCOMPARE(sourceCache.retainedGlyphCount(), qsizetype(0));
    }
};

QTEST_MAIN(TabLabelPresentationTest)
#include "tab_label_presentation_test.moc"
