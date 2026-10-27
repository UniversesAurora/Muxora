// SPDX-License-Identifier: MIT
// Surface reporting through a real, never-exposed strip. These checks use
// geometry and animation values, without native input or rendered pixels.
#include "docking/tab_bar.hpp"
#include "docking/tab_strip.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QImage>
#include <QPainter>
#include <QScopeGuard>
#include <QTest>
#include <algorithm>

using namespace window_system;

namespace {
void addTabs(TabBar& bar, int count)
{
    for (int index = 0; index < count; ++index) {
        const int added = bar.addTab({}, QStringLiteral("Workspace %1").arg(index + 1));
        bar.setTabData(added, QUuid::createUuid());
    }
}

QRectF arrange(TabBar& bar, QSize available)
{
    bar.setAvailableSize(available);
    const auto displayed = bar.displaySize(available);
    const auto reserved = bar.reservedSize(available);
    bar.resize(displayed);
    // Hidden widgets defer their resize event. Reapply the current policy
    // through its ordinary public entry to synchronize the in-memory layout.
    const auto policy = bar.appearance();
    bar.setAppearance(policy, bar.placement());
    QRectF body;
    switch (bar.placement()) {
    case TabBarPlacement::Top: body = {0, qreal(reserved.height()), qreal(available.width()), 400}; break;
    case TabBarPlacement::Bottom: body = {0, qreal(displayed.height() - reserved.height() - 400), qreal(available.width()), 400}; break;
    case TabBarPlacement::Left: body = {qreal(displayed.width() + bar.metrics().columnGap), 0, 400, qreal(available.height())}; break;
    case TabBarPlacement::Right: body = {qreal(-400 - bar.metrics().columnGap), 0, 400, qreal(available.height())}; break;
    }
    bar.setSurfaceContext(body, 1);
    return body;
}

void positions()
{
    QTest::addColumn<TabBarPlacement>("placement");
    QTest::newRow("top") << TabBarPlacement::Top;
    QTest::newRow("bottom") << TabBarPlacement::Bottom;
    QTest::newRow("left") << TabBarPlacement::Left;
    QTest::newRow("right") << TabBarPlacement::Right;
}

void compareProfiles(const ElevationProfile& actual, const ElevationProfile& expected)
{
    QCOMPARE(actual.key.offset, expected.key.offset);
    QCOMPARE(actual.key.blur, expected.key.blur);
    QCOMPARE(actual.key.opacity, expected.key.opacity);
    QCOMPARE(actual.ambient.offset, expected.ambient.offset);
    QCOMPARE(actual.ambient.blur, expected.ambient.blur);
    QCOMPARE(actual.ambient.opacity, expected.ambient.opacity);
    QCOMPARE(actual.edgeOpacity, expected.edgeOpacity);
}
} // namespace

class SurfaceBarTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { TabBar::setAnimationsEnabled(false); }

    void surfacePresenceFollowsItsLabelPose_data()
    {
        QTest::addColumn<TabBarPlacement>("placement");
        QTest::addColumn<bool>("entering");
        for (const auto placement : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right})
            for (const bool entering : {false, true})
                QTest::newRow(qPrintable(QStringLiteral("%1-enter-%2").arg(int(placement)).arg(entering))) << placement << entering;
    }
    void surfacePresenceFollowsItsLabelPose()
    {
        QFETCH(TabBarPlacement, placement);
        QFETCH(bool, entering);
        TabBar::setAnimationsEnabled(true);
        const auto restoreAnimations = qScopeGuard([] { TabBar::setAnimationsEnabled(false); });
        if (!TabBar::animationsAllowed()) QSKIP("System client-area animations are disabled");
        TabBar bar(defaultTheme(), nullptr);
        bar.setAppearance(TabAppearance{}, placement);
        addTabs(bar, 2); bar.setCurrentIndex(0);
        const auto body = arrange(bar, bar.vertical() ? QSize(180, 500) : QSize(640, 500));
        int backplateChanges = 0;
        bar.surfaceChanged = [&] { ++backplateChanges; };
        bar.animatePresence({bar.tabData(0).toUuid()}, entering);
        QCOMPARE(bar.labelPresence(0).opacity, entering ? qreal(0) : qreal(1));
        QCOMPARE(bar.surfaceAnchors().isEmpty(), entering);
        const int initialChanges = backplateChanges;
        QTRY_VERIFY_WITH_TIMEOUT(bar.labelPresence(0).opacity > 0 && bar.labelPresence(0).opacity < 1, 1000);
        const auto pose = bar.labelPresence(0);
        const auto anchors = bar.surfaceAnchors();
        QCOMPARE(anchors.size(), 1);
        QCOMPARE(anchors[0].presence.opacity, pose.opacity);
        QCOMPARE(anchors[0].presence.scale, pose.scale);
        QCOMPARE(anchors[0].presence.offset, pose.offset);
        QCOMPARE(anchors[0].geometry.body, body);
        QVERIFY(backplateChanges > initialChanges);
        QTRY_COMPARE_WITH_TIMEOUT(bar.labelPresence(0).opacity, entering ? qreal(1) : qreal(0), 1000);
        if (entering) QVERIFY(bar.surfaceAnchors()[0].presence.complete());
        else QVERIFY(bar.surfaceAnchors().isEmpty());
        QVERIFY(!bar.isVisible());
    }

    void presenceTransformsReuseCachedShadowImages()
    {
        SurfaceGeometryInput input;
        input.body = QRectF(180, 0, 400, 400);
        input.anchor = QRectF(0, 40, 180, 28); input.visibleAnchor = input.anchor;
        input.edge = SurfaceEdge::Left;
        const auto geometry = surfaceGeometry(input);
        ElevationRenderer renderer;
        QImage image(600, 420, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QPainter painter(&image);
        const auto draw = [&](qreal progress) {
            LabelPresenceEffect::paint(painter, labelPresencePose(input.anchor, progress), [&] {
                renderer.paintAnchor(painter, geometry, elevationProfile(ElevationLevel::BodyRest),
                    elevationProfile(ElevationLevel::AnchorRest), elevationStyleForTheme(defaultTheme()), true);
            });
        };
        draw(0.1);
        const auto warmed = renderer.cacheStats().generatedImages;
        for (int step = 2; step <= 10; ++step) draw(qreal(step) / 10);
        QCOMPARE(renderer.cacheStats().generatedImages, warmed);
    }

    void liftedLayerEscapesTheStripMask_data() { positions(); }
    void liftedLayerEscapesTheStripMask()
    {
        QFETCH(TabBarPlacement, placement);
        QWidget host; host.resize(800, 600);
        auto* group = new GroupTabs(defaultTheme(), &host);
        group->setGeometry(100, 100, 600, 400);
        const auto id = QUuid::createUuid();
        group->addTab(new QWidget, {}, "Workspace", id);
        TabAppearance policy; policy.centralPlacement = placement;
        group->setAppearance(policy);
        auto* bar = group->tabBar();
        const auto area = bar->tabRect(0);
        group->setDraggedTab(id);
        const auto point = area.center() - (bar->vertical() ? QPoint(0, 2 * area.height()) : QPoint(0, 0));
        group->setLiftedTab(bar->mapToGlobal(point), area.center() - area.topLeft());
        QPointer<QWidget> layer = host.findChild<QWidget*>("LiftedTabLayer", Qt::FindDirectChildrenOnly);
        QVERIFY(layer); QCOMPARE(layer->parentWidget(), &host);
        QCOMPARE(layer->geometry(), host.rect());
        QVERIFY(!layer->mask().isEmpty());
        QVERIFY(layer->mask() != QRegion(host.rect()));
        const auto margins = elevationMargins(elevationProfile(ElevationLevel::Lifted));
        const auto shadow = QRectF(bar->liftedRect()).marginsAdded(margins).translated(bar->mapTo(&host, QPoint()));
        QVERIFY(layer->rect().contains(shadow.toAlignedRect()));
        QVERIFY(layer->mask().contains(shadow.toAlignedRect()));
        QVERIFY(!host.isVisible());
        delete group;
        QVERIFY(layer.isNull());
    }

    void firstFastDragKeepsItsPressAnchor_data() { positions(); }
    void firstFastDragKeepsItsPressAnchor()
    {
        QFETCH(TabBarPlacement, placement);
        TabBar bar(defaultTheme(), nullptr);
        bar.setAppearance(TabAppearance{}, placement);
        addTabs(bar, 4); bar.setCurrentIndex(1);
        arrange(bar, bar.vertical() ? QSize(180, 500) : QSize(640, 500));
        const auto original = bar.tabRect(1);
        const auto press = original.topLeft() + QPoint(original.width() / 3, original.height() / 3);
        const auto delta = bar.vertical() ? QPoint(0, 3 * original.height())
            : QPoint(original.width() + QApplication::startDragDistance(), 0);
        const auto moved = press + delta;
        QVERIFY(!original.contains(moved));
        QVERIFY(bar.rect().contains(moved));
        bool started = false;
        QRect lifted;
        bar.drag = [&](int index, DragStart start) {
            // Exercise the same start-to-lift coordinate contract as the
            // controller, without exposing a window or capturing the mouse.
            QCOMPARE(start.pressGlobal, bar.mapToGlobal(press));
            QCOMPARE(start.currentGlobal, bar.mapToGlobal(moved));
            bar.setDraggedTab(bar.tabData(index).toUuid());
            bar.setLiftedWithAnchor(bar.mapFromGlobal(start.currentGlobal), start.anchor);
            lifted = bar.liftedRect();
            started = true;
        };
        QMouseEvent down(QEvent::MouseButtonPress, QPointF(press), QPointF(bar.mapToGlobal(press)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&bar, &down);
        QMouseEvent move(QEvent::MouseMove, QPointF(moved), QPointF(bar.mapToGlobal(moved)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&bar, &move);
        QVERIFY(started);
        QCOMPARE(lifted.topLeft(), original.topLeft() + delta);
        QVERIFY(lifted.contains(moved));
        QVERIFY(!bar.isVisible());
    }

    void dragAnchorsStayInsideEveryPresentation()
    {
        for (const auto point : {QPoint(0, 0), QPoint(230, 27), QPoint(229, 26), QPoint(113, 12)}) {
            const auto anchor = DragAnchor::capture(point, QRect(0, 0, 230, 28));
            for (const auto size : {QSize(26, 26), QSize(600, 28), QSize(28, 300), QSize(1, 1)})
                QVERIFY(QRect(QPoint(), size).contains(anchor.forSize(size)));
        }
        const auto start = DragStart::capture({200, 300}, {30, 10}, {0, 0, 230, 28});
        QVERIFY(!start.crossedThreshold({200, 300}, 10));
        QVERIFY(start.crossedThreshold({230, 340}, 10));
        QCOMPARE(start.movedTo({230, 340}).anchor.offset, start.anchor.offset);
    }

    void ordinaryPositionsReportTheBodyJoint_data() { positions(); }
    void ordinaryPositionsReportTheBodyJoint()
    {
        QFETCH(TabBarPlacement, placement);
        TabBar bar(defaultTheme(), nullptr);
        TabAppearance policy;
        bar.setAppearance(policy, placement);
        addTabs(bar, 4); bar.setCurrentIndex(1);
        const auto body = arrange(bar, bar.vertical() ? QSize(180, 500) : QSize(640, 500));
        auto anchors = bar.surfaceAnchors();
        QCOMPARE(anchors.size(), 1);
        QVERIFY(anchors.front().geometry.joined);
        QVERIFY(!anchors.front().geometry.covered);
        QCOMPARE(anchors.front().geometry.body, body);
        compareProfiles(anchors.front().elevation, elevationProfile(ElevationLevel::AnchorRaised));

        bar.setSurfaceContext(body, 0);
        anchors = bar.surfaceAnchors();
        QCOMPARE(anchors.size(), 1);
        QVERIFY(anchors.front().geometry.joined);
        compareProfiles(anchors.front().elevation, elevationProfile(ElevationLevel::AnchorRest));
        if (bar.vertical()) {
            bar.setCurrentIndex(0);
            QCOMPARE(qreal(bar.tabRect(0).top()), body.top());
            QCOMPARE(bar.surfaceAnchors().front().geometry.anchor.top(), body.top());
        }

        GroupTabs group(defaultTheme(), nullptr);
        group.resize(640, 500);
        group.addTab(new QWidget, {}, "Workspace", QUuid::createUuid());
        policy.centralPlacement = placement;
        group.setAppearance(policy);
        const auto first = group.tabBar()->mapToGlobal(group.tabBar()->tabRect(0).topLeft());
        const auto content = group.contentRectGlobal();
        if (group.tabBar()->vertical()) QCOMPARE(first.y(), content.top());
        else {
            QCOMPARE(first.x(), content.left());
            const auto barEnd = group.tabBar()->mapToGlobal(group.tabBar()->rect().bottomRight());
            QCOMPARE(barEnd.x(), content.right());
        }
        QVERIFY(!group.isVisible());
    }

    void borderlessContentRestoresItsNormalLayout()
    {
        GroupTabs group(defaultTheme(), nullptr);
        group.resize(640, 500);
        group.addTab(new QWidget, {}, "Workspace", QUuid::createUuid());
        group.setTabBarVisible(false);
        const auto ordinary = group.contentRectGlobal();
        QVERIFY(!group.surfaceBounds().isEmpty());
        group.setContentEdgeToEdge(true);
        QCOMPARE(group.contentRectGlobal(), QRect(group.mapToGlobal(QPoint()), group.size()));
        QVERIFY(group.surfaceBounds().isEmpty());
        group.setContentEdgeToEdge(false);
        QCOMPARE(group.contentRectGlobal(), ordinary);
        QVERIFY(!group.surfaceBounds().isEmpty());
        QVERIFY(!group.isVisible());
    }

    void horizontalOuterRowsAreCovered_data()
    {
        QTest::addColumn<bool>("bottom");
        QTest::newRow("top") << false; QTest::newRow("bottom") << true;
    }
    void horizontalOuterRowsAreCovered()
    {
        QFETCH(bool, bottom);
        TabBar bar(defaultTheme(), nullptr);
        TabAppearance policy; policy.multiline = true;
        bar.setAppearance(policy, bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top);
        addTabs(bar, 8);
        arrange(bar, {300, 700});
        const auto& tabs = bar.tabLayout().tabs;
        QVERIFY(!tabs.isEmpty());
        int outer = -1, inner = -1;
        for (int index = 0; index < tabs.size(); ++index) {
            if (bar.tabRect(index).isEmpty()) continue;
            if (outer < 0 || (bottom ? tabs[index].top() > tabs[outer].top() : tabs[index].top() < tabs[outer].top())) outer = index;
            if (inner < 0 || (bottom ? tabs[index].top() < tabs[inner].top() : tabs[index].top() > tabs[inner].top())) inner = index;
        }
        QVERIFY(outer >= 0 && inner >= 0);
        QVERIFY(tabs[outer].top() != tabs[inner].top());
        bar.setCurrentIndex(outer);
        auto anchors = bar.surfaceAnchors();
        QCOMPARE(anchors.size(), 1);
        QVERIFY(!anchors.front().geometry.joined);
        QVERIFY(anchors.front().geometry.covered);
        bar.setCurrentIndex(inner);
        anchors = bar.surfaceAnchors();
        QCOMPARE(anchors.size(), 1);
        QVERIFY(anchors.front().geometry.joined);
        QVERIFY(!anchors.front().geometry.covered);
    }

    void overlayRowsReportDetachedAnchors_data()
    {
        QTest::addColumn<bool>("bottom");
        QTest::newRow("top") << false; QTest::newRow("bottom") << true;
    }
    void overlayRowsReportDetachedAnchors()
    {
        QFETCH(bool, bottom);
        TabBar bar(defaultTheme(), nullptr);
        TabAppearance policy;
        policy.multiline = true; policy.showAllRows = false;
        policy.rowExpansion = TabRowExpansion::Overlay;
        bar.setAppearance(policy, bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top);
        addTabs(bar, 12); bar.setCurrentIndex(0);
        arrange(bar, {300, 700});
        const auto collapsed = bar.surfaceAnchors();
        QCOMPARE(collapsed.size(), 1);
        QVERIFY(collapsed.front().geometry.joined);
        bar.setExpanded(true, false, false);
        arrange(bar, {300, 700});
        QVERIFY(bar.overlayActive());
        const auto anchors = bar.surfaceAnchors();
        QCOMPARE(anchors.size(), 1);
        QVERIFY(!anchors.front().geometry.joined);
        QVERIFY(!anchors.front().geometry.covered);
    }

    void verticalStickyRowsShareOrdinaryAttachment_data()
    {
        QTest::addColumn<StickyTabSizing>("sizing");
        QTest::addColumn<bool>("right");
        for (const auto sizing : {StickyTabSizing::Normal, StickyTabSizing::Shrink, StickyTabSizing::Compact})
            for (const bool right : {false, true})
                QTest::newRow(qPrintable(QStringLiteral("sizing-%1-right-%2").arg(int(sizing)).arg(right))) << sizing << right;
    }
    void verticalStickyRowsShareOrdinaryAttachment()
    {
        QFETCH(StickyTabSizing, sizing); QFETCH(bool, right);
        TabBar bar(defaultTheme(), nullptr);
        TabAppearance policy; policy.stickySizing = sizing;
        bar.setAppearance(policy, right ? TabBarPlacement::Right : TabBarPlacement::Left);
        addTabs(bar, 5); bar.setStickyCount(3); bar.setCurrentIndex(1);
        arrange(bar, {180, 500});
        const auto anchors = bar.surfaceAnchors();
        if (sizing == StickyTabSizing::Compact) {
            QVERIFY(anchors.isEmpty());
            const auto tile = bar.tabRect(1);
            TabDragVisual incoming;
            incoming.size = tile.size(); incoming.anchor = tile.center() - tile.topLeft();
            incoming.active = QRect(QPoint(), incoming.size); incoming.identity = QUuid::createUuid();
            incoming.pieces.append(TabCarrierPiece{});
            bar.setDropPreview(1, true, {tile.width()});
            bar.setIncomingVisual(std::move(incoming), tile.center());
            const auto preview = bar.surfaceAnchors();
            QCOMPARE(preview.size(), 1);
            QVERIFY(preview.front().geometry.compact);
            QVERIFY(!preview.front().geometry.joined);
            QVERIFY(!preview.front().geometry.covered);
            QVERIFY(QRectF(bar.incomingRect()).contains(preview.front().geometry.anchorPath.boundingRect()));
            bar.setIncomingVisual({}); bar.setDropPreview(-1, false, {});
        }
        else {
            QCOMPARE(anchors.size(), 1);
            QVERIFY(anchors.front().geometry.joined);
        }
        bar.setIconMode(true);
        const auto body = arrange(bar, {180, 500});
        const auto iconAnchors = bar.surfaceAnchors();
        QCOMPARE(iconAnchors.size(), 1);
        QVERIFY(iconAnchors.front().geometry.joined);
        bar.setSurfaceContext(body, 1, 0.5);
        const auto peeking = bar.surfaceAnchors();
        QCOMPARE(peeking.size(), 1);
        QCOMPARE(peeking.front().geometry.joinProgress, 0.5);
        QVERIFY(!peeking.front().geometry.joined);
        QVERIFY(peeking.front().foreground);
        QVERIFY(peeking.front().geometry.bodyPath.isEmpty());
        const auto shadow = surfaceShadowClip(peeking.front().geometry, 7);
        QVERIFY(shadow.left() < peeking.front().geometry.anchor.left());
        QVERIFY(shadow.right() > peeking.front().geometry.anchor.right());
        QVERIFY(shadow.top() < peeking.front().geometry.anchor.top());
        QVERIFY(shadow.bottom() > peeking.front().geometry.anchor.bottom());
        QVERIFY(peeking.front().geometry.anchor != iconAnchors.front().geometry.anchor);
    }

    void localAndIncomingDragsReportTheSameSurface_data() { positions(); }
    void localAndIncomingDragsReportTheSameSurface()
    {
        QFETCH(TabBarPlacement, placement);
        TabBar bar(defaultTheme(), nullptr);
        bar.setAppearance(TabAppearance{}, placement);
        addTabs(bar, 4); bar.setCurrentIndex(1);
        arrange(bar, bar.vertical() ? QSize(180, 500) : QSize(640, 500));
        const auto area = bar.tabRect(1);
        QVERIFY(!area.isEmpty());
        const auto point = area.center();
        const QPoint grab(area.width() / 2, area.height() / 2);
        bar.setDraggedTab(bar.tabData(1).toUuid());
        bar.setDropPreview(1, false, {bar.tabExtent(1, false)});
        bar.setLifted(point, grab);
        const auto localDrop = bar.captureDropPositions();
        QCOMPARE(localDrop.value(bar.tabData(1).toUuid()),
            QRect(bar.mapToGlobal(bar.liftedRect().topLeft()), bar.liftedRect().size()));
        const auto local = bar.surfaceAnchors();
        QCOMPARE(local.size(), 1);
        QVERIFY(local.front().lifted);
        QVERIFY(local.front().geometry.joined);
        QVERIFY(!local.front().geometry.covered);
        compareProfiles(local.front().elevation, elevationProfile(ElevationLevel::Lifted));

        bar.setLifted({}); bar.setDraggedTab({});
        TabDragVisual incoming;
        incoming.size = area.size(); incoming.anchor = grab;
        incoming.active = QRect(QPoint{}, incoming.size);
        incoming.identity = QUuid::createUuid();
        incoming.pieces.append(TabCarrierPiece{});
        const auto incomingId = incoming.identity;
        // Release positions are supplied by the GUI controller, not a
        // callback retained by the target or a rendering frame.
        LabelPositions positions{{incomingId, QRect(QPoint(), incoming.size)}};
        bar.setIncomingVisual(std::move(incoming), point);
        const auto incomingDrop = bar.captureDropPositions(std::move(positions));
        QCOMPARE(incomingDrop.value(incomingId),
            QRect(bar.mapToGlobal(bar.incomingRect().topLeft()), bar.incomingRect().size()));
        const auto external = bar.surfaceAnchors();
        QCOMPARE(external.size(), 1);
        QVERIFY(external.front().lifted);
        QCOMPARE(external.front().geometry.joined, local.front().geometry.joined);
        QCOMPARE(external.front().geometry.covered, local.front().geometry.covered);
        QCOMPARE(external.front().geometry.edge, local.front().geometry.edge);
        compareProfiles(external.front().elevation, local.front().elevation);
    }

    void surfaceStateSurvivesReplacingTheOwningStrip()
    {
        TabBar before(defaultTheme(), nullptr);
        addTabs(before, 3); before.setCurrentIndex(1);
        const auto selected = before.tabData(1);
        const auto snapshot = before.captureSurfaceState();
        QCOMPARE(snapshot.selection.value(selected.toUuid()), 1.0);
        while (before.count()) before.removeTab(0);
        TabBar after(defaultTheme(), nullptr);
        addTabs(after, 2); after.setTabData(0, selected);
        after.setCurrentIndex(0);
        after.inheritSurface(snapshot, true);
        QCOMPARE(after.captureSurfaceState().selection.value(selected.toUuid()), 1.0);
        after.setCurrentIndex(1);
        const auto switched = after.captureSurfaceState();
        QCOMPARE(switched.selection.value(selected.toUuid()), 0.0);
        QCOMPARE(switched.selection.value(after.tabData(1).toUuid()), 1.0);
    }

    void coveringActionFadesOnlyTitleInk()
    {
        TabPainter style(TabMetrics{}, TabPalette::forTheme(defaultTheme()));
        TabParts parts;
        parts.tab = QRect(0, 0, 90, 28);
        parts.icon = QRect(10, 6, 16, 16);
        parts.title = QRect(28, 0, 58, 28);
        parts.action = QRect(32, 4, 20, 20);
        QPixmap icon(16, 16); icon.fill(Qt::magenta);
        TabTitleLayout title; title.prepare("Workspace", QApplication::font(), Qt::LeftToRight, true, nullptr);
        TabPaintState state;
        state.selected = true; state.groupFocused = true; state.icon = QIcon(icon);
        state.titleLayout = &title; state.title = "Workspace";
        state.actionShown = true; state.actionReserved = false;
        state.actionIcon = iconFor("close"); state.elevationManaged = true;
        QImage before(90, 28, QImage::Format_ARGB32_Premultiplied), after(before.size(), before.format());
        before.fill(Qt::transparent); after.fill(Qt::transparent);
        QPainter first(&before), second(&after);
        state.actionOpacity = 0; style.paintTab(first, parts, state);
        state.actionOpacity = 1; style.paintTab(second, parts, state);
        first.end(); second.end();
        QVERIFY(before.copy(parts.icon) == after.copy(parts.icon));
        QVERIFY(before.copy(parts.title) != after.copy(parts.title));
    }

    void endingAColumnDragKeepsOnlyTheHoveredPanelExpanded_data()
    {
        QTest::addColumn<bool>("right");
        QTest::addColumn<bool>("footer");
        for (bool right : {false, true}) for (bool footer : {false, true})
            QTest::newRow(qPrintable(QString("right-%1-footer-%2").arg(right).arg(footer))) << right << footer;
    }

    void endingAColumnDragKeepsOnlyTheHoveredPanelExpanded()
    {
        QFETCH(bool, right); QFETCH(bool, footer);
        GroupTabs group(defaultTheme(), nullptr);
        group.resize(620, 420);
        group.addTab(new QWidget, {}, "Workspace", QUuid::createUuid());
        TabAppearance policy;
        policy.centralPlacement = right ? TabBarPlacement::Right : TabBarPlacement::Left;
        group.setAppearance(policy); group.setVerticalState(200, true);
        const auto before = group.stripPanelRectGlobal();
        const auto available = group.tabBar()->availableSize();
        const int firstY = group.tabBar()->mapToGlobal(group.tabBar()->tabRect(0).topLeft()).y();
        group.restoreColumnPeek();
        QVERIFY(group.tabBar()->peeking());
        const auto panel = group.stripPanelRectGlobal();
        QCOMPARE(panel.top(), before.top()); QCOMPARE(panel.bottom(), before.bottom());
        QCOMPARE(group.tabBar()->availableSize(), available);
        QCOMPARE(group.tabBar()->mapToGlobal(group.tabBar()->tabRect(0).topLeft()).y(), firstY);
        QVERIFY(!panel.isEmpty());
        const auto inside = footer ? QPoint(panel.center().x(), panel.bottom() - 1) : panel.center();
        group.clearDragHover(inside);
        QVERIFY(group.tabBar()->peeking());
        group.clearDragHover(panel.bottomRight() + QPoint(100, 100));
        QVERIFY(!group.tabBar()->peeking());
        QVERIFY(!group.isVisible());
    }

    void surfaceMotionRetargetsFromTheDisplayedValue()
    {
        ProgressAnimation animation;
        animation.animateTo(1, Motion::SurfaceLift, true);
        animation.setCurrentTime(animation.duration() / 2);
        const qreal middle = animation.value();
        QVERIFY(middle > 0.5 && middle < 1);
        animation.animateTo(0, Motion::SurfaceLower, true);
        QCOMPARE(animation.value(), middle);
        QCOMPARE(animation.duration(), qRound(motionDuration(Motion::SurfaceLower) * middle));
        animation.setCurrentTime(animation.duration() / 2);
        QVERIFY(animation.value() > 0 && animation.value() < middle);
        animation.animateTo(1, Motion::SurfaceLift, false);
        QCOMPARE(animation.value(), 1);
        QCOMPARE(animation.target(), 1);
    }
};

QTEST_MAIN(SurfaceBarTest)
#include "surface_bar_test.moc"
