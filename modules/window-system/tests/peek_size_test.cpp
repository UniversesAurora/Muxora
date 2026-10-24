// SPDX-License-Identifier: MIT
#include "translation_checks.hpp"
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "commands/command_menus.hpp"
#include "docking/dock_surface.hpp"
#include "docking/edge_strip.hpp"
#include "docking/tab_strip.hpp"
#include <window_system/window_manager.hpp>

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <limits>
#include <memory>

using namespace window_system;

namespace {

class ResizeCallbackLabel final : public QLabel {
public:
    ResizeCallbackLabel() : QLabel("Resize callback") {}
    std::function<void()> resized;
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        const auto callback = resized;
        if (callback) callback();
    }
};

void edgeRows()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left); QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top); QTest::newRow("bottom") << int(Edge::Bottom);
}

bool horizontal(Edge edge) { return edge == Edge::Left || edge == Edge::Right; }
int extent(QWidget* widget, Edge edge) { return horizontal(edge) ? widget->width() : widget->height(); }
int requestedExtent(QSize size, Edge edge) { return horizontal(edge) ? size.width() : size.height(); }

void sizeWindow(QWidget* window)
{
    const auto available = window->screen()->availableGeometry();
    const QSize size(qMin(1100, available.width()), qMin(740, available.height()));
    QRect geometry(QPoint(), size); geometry.moveCenter(available.center()); window->setGeometry(geometry);
}

DockSurface* surface(WindowManager& manager, WindowId window)
{
    return static_cast<WorkspaceWindow*>(manager.windowWidget(window))->surface();
}

QWidget* peek(WindowManager& manager, WindowId window)
{
    return manager.windowWidget(window)->findChild<QWidget*>("PeekPanel");
}

GroupTabs* pinnedGroup(WindowManager& manager, WindowId window, GroupId group)
{
    for (auto* candidate : manager.windowWidget(window)->findChildren<QFrame*>("TabGroup"))
        if (candidate->isVisible() && candidate->property("groupId").toUuid() == group)
            return dynamic_cast<GroupTabs*>(candidate);
    return nullptr;
}

QRect peekAvailable(DockSurface* surface)
{
    QRect area = surface->rect();
    const auto bars = surface->edgeBarRects();
    for (auto iterator = bars.cbegin(); iterator != bars.cend(); ++iterator) {
        const auto rectangle = QRect(surface->mapFromGlobal(iterator->topLeft()), iterator->size());
        switch (iterator.key()) {
        case Edge::Left: area.setLeft(rectangle.right() + 1); break;
        case Edge::Right: area.setRight(rectangle.left() - 1); break;
        case Edge::Top: area.setTop(rectangle.bottom() + 1); break;
        case Edge::Bottom: area.setBottom(rectangle.top() - 1); break;
        }
    }
    return area;
}

void sendDrag(QWidget* widget, QPoint start)
{
    const auto destination = start + QPoint(QApplication::startDragDistance() + 1, 0);
    QMouseEvent press(QEvent::MouseButtonPress, start, widget->mapToGlobal(start),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QMouseEvent move(QEvent::MouseMove, destination, widget->mapToGlobal(destination),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, destination, widget->mapToGlobal(destination),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &release);
}

void sendContextMenu(QWidget* widget, QPoint point)
{
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, widget->mapToGlobal(point));
    QApplication::sendEvent(widget, &event);
}

} // namespace

class PeekSizeTest final : public QObject {
    Q_OBJECT
    translation_test::ChineseUiCatalog chineseUi_;
private slots:
    void initTestCase() { QVERIFY(chineseUi_.install()); qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void perTabPreferencesUpdateTheLivePanelWithoutRebuilding_data() { edgeRows(); }
    void perTabPreferencesUpdateTheLivePanelWithoutRebuilding();
    void resizeCallbacksCanReplaceModelRecords_data() { edgeRows(); }
    void resizeCallbacksCanReplaceModelRecords();
    void innerEdgeDraggingSavesOnlyTheActiveAxis_data() { edgeRows(); }
    void innerEdgeDraggingSavesOnlyTheActiveAxis();
    void pinnedPartitionsUseIndependentExtents_data() { edgeRows(); }
    void pinnedPartitionsUseIndependentExtents();
    void viewportClampingPreservesTheRequestedSize_data() { edgeRows(); }
    void viewportClampingPreservesTheRequestedSize();
    void preferencesMoveAndRestoreWithTheTab();
    void invalidApiCallsPreserveTheLivePanelAndSnapshot();
    void invalidCommandArgumentsAreAtomic_data();
    void invalidCommandArgumentsAreAtomic();
    void commandsUseTheClickedTabAndStayOutOfOtherContexts();
    void shortcutOpensAKeyboardEditableSizeDialog();
    void dialogCancellationAndTargetLossAreSafe_data();
    void dialogCancellationAndTargetLossAreSafe();
    void aSynchronousSaverCanDeleteTheManager();
    void resizingContentCanDeleteTheManager();
    void queuedInputIsHarmlessUntilDeferredDeletion();
    void defaultPanelsStretchOneAxisAndUseUniformCompactMargins_data() { edgeRows(); }
    void defaultPanelsStretchOneAxisAndUseUniformCompactMargins();
    void bothDimensionsAndCornerResizingWorkOnEverySide_data() { edgeRows(); }
    void bothDimensionsAndCornerResizingWorkOnEverySide();
    void oppositeSidesShareSizesWithoutChangingTheOtherOrientation();
    void autoAxisCommandsPreserveViewportStretch();
    void directionalPoliciesRejectInvalidSizesWithoutReplacingContent();
    void shortenedPanelsFollowTheirTriggerAndClampToTheViewport_data() { edgeRows(); }
    void shortenedPanelsFollowTheirTriggerAndClampToTheViewport();
};

void PeekSizeTest::shortenedPanelsFollowTheirTriggerAndClampToTheViewport()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    const bool vertical = direction == Edge::Left || direction == Edge::Right;
    WindowManager manager; const auto id = manager.createWindow();
    manager.addTab(id, new QLabel("Central"), {"Central", "label", {}});
    QVector<TabId> tabs;
    for (int index = 0; index < 12; ++index) {
        const auto tab = manager.addTab(id, new QLabel(QString::number(index)),
            {QString("Sidebar section %1").arg(index), "label", {}});
        QVERIFY(manager.moveTabToEdge(tab, direction)); tabs.append(tab);
    }
    auto* window = manager.windowWidget(id); sizeWindow(window); manager.showWindow(id);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto tab = tabs[2]; const auto pane = manager.model().tab(tab)->activePane;
    auto* content = manager.paneWidget(pane);
    EdgeStrip* strip = nullptr;
    for (auto* frame : window->findChildren<QFrame*>("EdgeBar"))
        if (frame->isVisible() && frame->property("edge").toInt() == edge) strip = dynamic_cast<EdgeStrip*>(frame);
    QVERIFY(strip);
    QAbstractButton* trigger = nullptr;
    for (auto* button : strip->findChildren<QAbstractButton*>("EdgeButton"))
        if (button->property("tabId").toUuid() == tab) trigger = button;
    QVERIFY(trigger); QVERIFY(manager.activateTab(tab));
    auto* dock = surface(manager, id); QPointer<QWidget> panel = peek(manager, id); QVERIFY(panel);
    const auto expectedOrigin = [&] {
        const auto area = peekAvailable(dock);
        const auto position = trigger->mapTo(dock, QPoint());
        return vertical ? qBound(area.top(), position.y(), area.bottom() - panel->height() + 1)
                        : qBound(area.left(), position.x(), area.right() - panel->width() + 1);
    };
    QVERIFY(manager.setPeekSizePolicy(tab, direction, {300, 180}));
    QTRY_COMPARE(vertical ? panel->y() : panel->x(), expectedOrigin());
    QVERIFY((vertical ? panel->y() : panel->x()) > (vertical ? peekAvailable(dock).top() : peekAvailable(dock).left()));
    QVERIFY(peekAvailable(dock).contains(panel->geometry()));

    const auto offset = strip->scrollOffset();
    strip->scrollByWheel({0, -36}); QTRY_VERIFY(strip->scrollOffset() > offset);
    QTRY_COMPARE(vertical ? panel->y() : panel->x(), expectedOrigin());
    const auto area = peekAvailable(dock);
    QVERIFY(manager.setPeekSizePolicy(tab, direction, {area.width(), area.height()}));
    QTRY_COMPARE(panel->geometry(), area);
    QVERIFY(manager.setPeekSizePolicy(tab, direction, {300, 180}));
    QTRY_COMPARE(vertical ? panel->y() : panel->x(), expectedOrigin());
    QCOMPARE(peek(manager, id), panel.data()); QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(content->isVisible()); QVERIFY(manager.model().validate());
}

void PeekSizeTest::perTabPreferencesUpdateTheLivePanelWithoutRebuilding()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    WindowManager manager; const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    auto* content = new QPlainTextEdit("Kept contents");
    const auto first = manager.addTab(window, content, {"First", "editor", {}});
    const auto pane = manager.model().tab(first)->activePane;
    const auto second = manager.addTab(window, new QLabel("Second"), {"Second", "label", {}});
    QVERIFY(manager.moveTabToEdge(first, direction)); QVERIFY(manager.moveTabToEdge(second, direction));
    const auto group = manager.model().groupForTab(first);
    // Deliberately use one auto-hide group: sizes belong to the tabs, not this group.
    QVERIFY(manager.moveTab(second, group)); QCOMPARE(manager.model().groupForTab(second), group);
    QCOMPARE(manager.peekSize(first, direction), QSize(250, 170)); QCOMPARE(manager.peekSize(second, direction), QSize(250, 170));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); QVERIFY(manager.activateTab(first));
    QPointer<QWidget> panel = peek(manager, window); QVERIFY(panel); QVERIFY(content->isVisible());
    const auto pinnedExtent = manager.model().group(group)->edgeExtent;
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    content->setFocus(); QTRY_VERIFY(content->hasFocus());
    changes.clear();
    const auto firstSize = horizontal(direction) ? PeekSizePolicy{350, std::nullopt} : PeekSizePolicy{std::nullopt, 210};
    const auto secondSize = horizontal(direction) ? PeekSizePolicy{305, std::nullopt} : PeekSizePolicy{std::nullopt, 190};
    const auto firstQuery = horizontal(direction) ? QSize(350, 170) : QSize(250, 210);
    const auto secondQuery = horizontal(direction) ? QSize(305, 170) : QSize(250, 190);
    QVERIFY(manager.setPeekSizePolicy(first, direction, firstSize));
    QCOMPARE(peek(manager, window), panel.data()); QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(content->toPlainText(), QStringLiteral("Kept contents")); QVERIFY(content->hasFocus());
    QTRY_COMPARE(extent(panel, direction), requestedExtent(QSize(350, 210), direction));
    QCOMPARE(manager.model().group(group)->edgeExtent, pinnedExtent); QCOMPARE(changes.size(), 1);
    QVERIFY(manager.setPeekSizePolicy(second, direction, secondSize));
    QCOMPARE(peek(manager, window), panel.data()); QCOMPARE(manager.peekSize(first, direction), firstQuery);
    surface(manager, window)->closePeek(); QVERIFY(!panel); QVERIFY(manager.activateTab(second));
    QTRY_COMPARE(extent(peek(manager, window), direction), requestedExtent(QSize(305, 190), direction));
    QVERIFY(manager.activateTab(first));
    QTRY_COMPARE(extent(peek(manager, window), direction), requestedExtent(QSize(350, 210), direction));
    panel = peek(manager, window); QVERIFY(manager.resetPeekSize(first, direction));
    QCOMPARE(peek(manager, window), panel.data()); QCOMPARE(manager.peekSize(first, direction), QSize(250, 170));
    QCOMPARE(manager.peekSizePolicy(first, direction), PeekSizePolicy{}); QCOMPARE(manager.peekSize(second, direction), secondQuery);
    QTRY_COMPARE(extent(panel, direction), requestedExtent(QSize(250, 170), direction));
}

void PeekSizeTest::resizeCallbacksCanReplaceModelRecords()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    WindowManager manager; const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    auto* content = new ResizeCallbackLabel;
    const auto tab = manager.addTab(window, content, {"Reentrant resize", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(manager.moveTabToEdge(tab, direction));
    QVERIFY(manager.setPeekSizePolicy(tab, direction,
        horizontal(direction) ? PeekSizePolicy{300, std::nullopt} : PeekSizePolicy{std::nullopt, 190}));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.activateTab(tab));
    QPointer<QWidget> panel = peek(manager, window); QVERIFY(panel);
    auto* handle = panel->findChild<QWidget*>("PeekResizeHandle"); QVERIFY(handle);
    bool callbackRan = false;
    bool activated = false;
    content->resized = [&] {
        if (callbackRan) return;
        callbackRan = true;
        // placePeek is still inside setGeometry. Activating this same pane is
        // a transaction too: it replaces groups_ even without moving content.
        activated = manager.focusPane(tab, pane);
    };
    const auto revision = manager.model().revision();
    QVERIFY(manager.setPeekSizePolicy(tab, direction,
        horizontal(direction) ? PeekSizePolicy{345, std::nullopt} : PeekSizePolicy{std::nullopt, 235}));
    QVERIFY(callbackRan); QVERIFY(activated);
    QVERIFY(manager.model().revision() >= revision + 2);
    QCOMPARE(peek(manager, window), panel.data()); QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.peekSize(tab, direction), horizontal(direction) ? QSize(345, 170) : QSize(250, 235)); QVERIFY(content->isVisible());
    QTRY_COMPARE(extent(panel, direction), requestedExtent(QSize(345, 235), direction));
    QCOMPARE(handle->property("edge").toInt(), edge);
    QVERIFY(panel->rect().contains(handle->geometry()));
    switch (direction) {
    case Edge::Left: QCOMPARE(handle->geometry(), QRect(panel->width() - 5, 1, 4, panel->height() - 2)); break;
    case Edge::Right: QCOMPARE(handle->geometry(), QRect(1, 1, 4, panel->height() - 2)); break;
    case Edge::Top: QCOMPARE(handle->geometry(), QRect(1, panel->height() - 5, panel->width() - 2, 4)); break;
    case Edge::Bottom: QCOMPARE(handle->geometry(), QRect(1, 1, panel->width() - 2, 4)); break;
    }
    QVERIFY(manager.model().validate());
    content->resized = {};
}

void PeekSizeTest::innerEdgeDraggingSavesOnlyTheActiveAxis()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    WindowManager manager; const auto window = manager.createWindow();
    auto* content = new QPlainTextEdit("Identity survives resizing");
    const auto tab = manager.addTab(window, content, {"Side", "editor", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, direction));
    QVERIFY(manager.setPeekSizePolicy(tab, direction,
        horizontal(direction) ? PeekSizePolicy{300, std::nullopt} : PeekSizePolicy{std::nullopt, 190}));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); QVERIFY(manager.activateTab(tab));
    QPointer<QWidget> panel = peek(manager, window); QVERIFY(panel);
    auto* handle = panel->findChild<QWidget*>("PeekResizeHandle"); QVERIFY(handle); QVERIFY(handle->isVisible());
    QCOMPARE(handle->cursor().shape(), horizontal(direction) ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    QVERIFY(panel->rect().contains(handle->geometry()));
    switch (direction) {
    case Edge::Left: QCOMPARE(handle->geometry().right(), panel->width() - 2); break;
    case Edge::Right: QCOMPARE(handle->x(), 1); break;
    case Edge::Top: QCOMPARE(handle->geometry().bottom(), panel->height() - 2); break;
    case Edge::Bottom: QCOMPARE(handle->y(), 1); break;
    }
    const auto start = handle->rect().center();
    const int sign = direction == Edge::Left || direction == Edge::Top ? 1 : -1;
    const auto delta = horizontal(direction) ? QPoint(sign * 36, 0) : QPoint(0, sign * 28);
    const auto expected = horizontal(direction) ? QSize(336, 170) : QSize(250, 218);
    const auto group = manager.model().groupForTab(tab); const auto pinnedExtent = manager.model().group(group)->edgeExtent;
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(handle, start + delta);
    QTRY_COMPARE(manager.peekSize(tab, direction), expected);
    const auto resizedPolicy = manager.peekSizePolicy(tab, direction);
    if (horizontal(direction)) { QCOMPARE(resizedPolicy.width, std::optional<int>(336)); QVERIFY(!resizedPolicy.height); }
    else { QVERIFY(!resizedPolicy.width); QCOMPARE(resizedPolicy.height, std::optional<int>(218)); }
    QCOMPARE(peek(manager, window), panel.data()); QCOMPARE(manager.paneWidget(pane), content);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
    QTRY_COMPARE(extent(panel, direction), requestedExtent(expected, direction));
    QCOMPARE(manager.model().group(group)->edgeExtent, pinnedExtent);
    surface(manager, window)->closePeek(); QVERIFY(manager.activateTab(tab));
    QTRY_COMPARE(extent(peek(manager, window), direction), requestedExtent(expected, direction));
    QCOMPARE(manager.paneWidget(pane), content); QCOMPARE(content->toPlainText(), QStringLiteral("Identity survives resizing"));
}

void PeekSizeTest::pinnedPartitionsUseIndependentExtents()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    WindowManager manager; const auto window = manager.createWindow();
    auto* content = new QLabel("Side"); const auto tab = manager.addTab(window, content, {"Side", "label", {}});
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, direction, true)); const auto group = manager.model().groupForTab(tab);
    QVERIFY(manager.execute("layout.setEdgeExtent", {{"group", group}, {"extent", 190}}, window));
    // This internal command commits an already moved splitter to the model;
    // it does not resize the old PinnedRegion's preferredExtent. Establish a
    // real 190px presentation using the same unpin/repin precondition as
    // EdgeTabInteractionTest::pinnedExtentSurvivesRefreshAndUnpinRepin.
    QVERIFY(manager.pinTab(tab, false)); QVERIFY(manager.pinTab(tab, true));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QTRY_VERIFY(pinnedGroup(manager, window, group));
    QTRY_COMPARE(extent(pinnedGroup(manager, window, group), direction), 190);
    QPointer<GroupTabs> pinned = pinnedGroup(manager, window, group);
    QVERIFY(manager.setPeekSizePolicy(tab, direction,
        horizontal(direction) ? PeekSizePolicy{360, std::nullopt} : PeekSizePolicy{std::nullopt, 225}));
    QCOMPARE(pinnedGroup(manager, window, group), pinned.data());
    QCOMPARE(extent(pinned, direction), 190); QCOMPARE(manager.model().group(group)->edgeExtent, 190);
    QVERIFY(manager.pinTab(tab, false)); QVERIFY(manager.activateTab(tab));
    QTRY_COMPARE(extent(peek(manager, window), direction), requestedExtent(QSize(360, 225), direction));
    QVERIFY(manager.setPeekSizePolicy(tab, direction,
        horizontal(direction) ? PeekSizePolicy{330, std::nullopt} : PeekSizePolicy{std::nullopt, 205}));
    QVERIFY(manager.pinTab(tab, true));
    QTRY_VERIFY(pinnedGroup(manager, window, group)); QTRY_COMPARE(extent(pinnedGroup(manager, window, group), direction), 190);
    QCOMPARE(manager.model().group(group)->edgeExtent, 190);
    QCOMPARE(manager.peekSize(tab, direction), horizontal(direction) ? QSize(330, 170) : QSize(250, 205));
    QVERIFY(manager.resetPeekSize(tab, direction)); QTRY_COMPARE(extent(pinnedGroup(manager, window, group), direction), 190);
    QVERIFY(manager.pinTab(tab, false)); QVERIFY(manager.activateTab(tab));
    QTRY_COMPARE(extent(peek(manager, window), direction), requestedExtent(QSize(250, 170), direction));
    QCOMPARE(manager.paneWidget(manager.model().tab(tab)->activePane), content);
}

void PeekSizeTest::viewportClampingPreservesTheRequestedSize()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Side"), {"Side", "label", {}});
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, direction));
    QVERIFY(manager.setPeekSizePolicy(tab, direction,
        horizontal(direction) ? PeekSizePolicy{2000, std::nullopt} : PeekSizePolicy{std::nullopt, 2000}));
    const auto requestedSize = horizontal(direction) ? QSize(2000, 170) : QSize(250, 2000);
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); QVERIFY(manager.activateTab(tab));
    QPointer<QWidget> panel = peek(manager, window); QVERIFY(panel);
    auto* dock = surface(manager, window);
    QTRY_COMPARE(panel->geometry(), peekAvailable(dock));
    QCOMPARE(manager.peekSize(tab, direction), requestedSize);
    auto* widget = manager.windowWidget(window); const auto before = widget->size();
    widget->resize(qMax(480, before.width() - 120), qMax(320, before.height() - 70));
    QTRY_COMPARE(panel->geometry(), peekAvailable(dock)); QCOMPARE(manager.peekSize(tab, direction), requestedSize);
    widget->resize(before); QTRY_COMPARE(panel->geometry(), peekAvailable(dock));
    QCOMPARE(manager.peekSize(tab, direction), requestedSize);
    // Begin from the visible edge, not the off-screen 2000px request.
    auto* handle = panel->findChild<QWidget*>("PeekResizeHandle"); QVERIFY(handle);
    const int visible = extent(panel, direction); const auto start = handle->rect().center();
    const int sign = direction == Edge::Left || direction == Edge::Top ? -1 : 1;
    const auto delta = horizontal(direction) ? QPoint(sign * 30, 0) : QPoint(0, sign * 30);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(handle, start + delta);
    QTRY_COMPARE(requestedExtent(manager.peekSize(tab, direction), direction), visible - 30);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
    const auto policy = manager.peekSizePolicy(tab, direction);
    if (horizontal(direction)) QVERIFY(!policy.height); else QVERIFY(!policy.width);
    QCOMPARE(horizontal(direction) ? manager.peekSize(tab, direction).height() : manager.peekSize(tab, direction).width(),
        horizontal(direction) ? 170 : 250);
    QTRY_COMPARE(extent(panel, direction), visible - 30);
}

void PeekSizeTest::preferencesMoveAndRestoreWithTheTab()
{
    WindowManager manager; const auto original = manager.createWindow();
    auto* content = new QPlainTextEdit("Moved tab");
    const auto tab = manager.addTab(original, content, {"Side", "editor", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    manager.addTab(original, new QLabel("Kept"), {"Kept", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge::Left));
    QVERIFY(manager.setPeekSizePolicy(tab, Edge::Left, {340, std::nullopt}));
    QVERIFY(manager.setPeekSizePolicy(tab, Edge::Top, {std::nullopt, 205}));
    const auto floating = manager.detachTab(tab); QVERIFY(!floating.isNull());
    QCOMPARE(manager.peekSize(tab, Edge::Left), QSize(340, 170)); QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(manager.moveTabToEdge(tab, Edge::Bottom)); QCOMPARE(manager.peekSize(tab, Edge::Bottom), QSize(250, 205));
    QCOMPARE(manager.peekSizePolicy(tab, Edge::Left), (PeekSizePolicy{340, std::nullopt}));
    const auto saved = manager.saveLayout();
    WindowManager restored; int creations = 0;
    restored.setContentFactory([&](const TabOptions& options, PaneId) { ++creations; return new QLabel(options.title); });
    QVERIFY(restored.restoreLayout(saved)); QCOMPARE(creations, 2);
    QCOMPARE(restored.peekSize(tab, Edge::Bottom), QSize(250, 205));
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Right), (PeekSizePolicy{340, std::nullopt}));
    QCOMPARE(restored.model().group(restored.model().groupForTab(tab))->edge, Edge::Bottom);
    sizeWindow(restored.windowWidget(floating)); restored.showWindow(floating);
    QVERIFY(QTest::qWaitForWindowExposed(restored.windowWidget(floating))); QVERIFY(restored.activateTab(tab));
    QTRY_COMPARE(peek(restored, floating)->height(), 205);
    QVERIFY(restored.resetPeekSize(tab, Edge::Bottom));
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Bottom), PeekSizePolicy{});
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Left), (PeekSizePolicy{340, std::nullopt}));
    const auto model = restored.saveLayout().value("model").toObject();
    for (const auto& value : model.value("tabs").toArray()) {
        const auto record = value.toObject();
        if (TabId(record.value("id").toString()) == tab) {
            QCOMPARE(record.value("horizontalPeekSize").toObject(), QJsonObject{});
            QCOMPARE(record.value("verticalPeekSize").toObject(), (QJsonObject{{"width", 340}}));
        }
    }
    const auto before = restored.saveLayout(); auto* restoredContent = restored.paneWidget(pane);
    for (const auto* field : {"verticalPeekSize", "horizontalPeekSize"}) {
        auto malformed = saved; auto malformedModel = malformed.value("model").toObject();
        auto records = malformedModel.value("tabs").toArray();
        auto record = records.first().toObject(); record.insert(field, QJsonObject{{"width", 320.5}, {"height", 180}});
        records[0] = record; malformedModel.insert("tabs", records); malformed.insert("model", malformedModel);
        QVERIFY(!restored.restoreLayout(malformed)); QCOMPARE(creations, 2); QCOMPARE(restored.saveLayout(), before);
        QCOMPARE(restored.paneWidget(pane), restoredContent);
    }
}

void PeekSizeTest::invalidApiCallsPreserveTheLivePanelAndSnapshot()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Side"), {"Side", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge::Left));
    QVERIFY(manager.setPeekSizePolicy(tab, Edge::Left, {340, std::nullopt}));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); QVERIFY(manager.activateTab(tab));
    QPointer<QWidget> panel = peek(manager, window); QVERIFY(panel);
    const auto before = manager.saveLayout(); const auto geometry = panel->geometry();
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    for (const auto size : {QSize(139, 170), QSize(250, 99), QSize(-1, -1), QSize(QWIDGETSIZE_MAX + 1, 170)})
        QVERIFY(!manager.setPeekSize(tab, Edge::Left, size));
    const auto unknown = QUuid::createUuid();
    QVERIFY(!manager.setPeekSize(unknown, Edge::Left, QSize(300, 200))); QVERIFY(!manager.resetPeekSize(unknown, Edge::Left));
    QCOMPARE(manager.peekSize(unknown, Edge::Left), QSize()); QCOMPARE(changes.size(), 0);
    QCOMPARE(manager.saveLayout(), before); QCOMPARE(panel->geometry(), geometry); QCOMPARE(peek(manager, window), panel.data());
    QVERIFY(manager.setPeekSizePolicy(tab, Edge::Left, {340, std::nullopt})); QCOMPARE(changes.size(), 0);
}

void PeekSizeTest::invalidCommandArgumentsAreAtomic_data()
{
    QTest::addColumn<QVariantMap>("args");
    QTest::newRow("missing-width") << QVariantMap{{"height", 180}};
    QTest::newRow("missing-height") << QVariantMap{{"width", 320}};
    QTest::newRow("fraction") << QVariantMap{{"width", 320.5}, {"height", 180}};
    QTest::newRow("boolean") << QVariantMap{{"width", true}, {"height", 180}};
    QTest::newRow("numeric-text") << QVariantMap{{"width", "320"}, {"height", 180}};
    QTest::newRow("null") << QVariantMap{{"width", QVariant()}, {"height", 180}};
    QTest::newRow("nonfinite") << QVariantMap{{"width", std::numeric_limits<double>::infinity()}, {"height", 180}};
    QTest::newRow("small-width") << QVariantMap{{"width", 139}, {"height", 180}};
    QTest::newRow("small-height") << QVariantMap{{"width", 320}, {"height", 99}};
    QTest::newRow("too-large") << QVariantMap{{"width", QWIDGETSIZE_MAX + 1}, {"height", 180}};
}

void PeekSizeTest::invalidCommandArgumentsAreAtomic()
{
    QFETCH(QVariantMap, args);
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Side"), {"Side", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge::Right)); QVERIFY(manager.activateTab(tab));
    const auto before = manager.saveLayout(); QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    QVERIFY(!manager.execute("tab.peek.setSize", args, window)); QCOMPARE(manager.saveLayout(), before);
    QCOMPARE(changes.size(), 0); QCOMPARE(manager.peekSize(tab, Edge::Right), QSize(250, 170));
}

void PeekSizeTest::commandsUseTheClickedTabAndStayOutOfOtherContexts()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto first = manager.addTab(window, new QLabel("First"), {"First", "label", {}});
    const auto second = manager.addTab(window, new QLabel("Second"), {"Second", "label", {}});
    const auto central = manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.moveTabToEdge(first, Edge::Left)); QVERIFY(manager.moveTabToEdge(second, Edge::Left));
    QVERIFY(manager.activateTab(first));
    auto captured = manager.context(window); captured.group = manager.model().groupForTab(second); captured.tab = second;
    QMenu menu; populateContextMenu(&menu, CommandMenuSurface::Tab, &manager, captured);
    auto* set = menu.findChild<QAction*>("tab.peek.setSize"); auto* reset = menu.findChild<QAction*>("tab.peek.resetSize");
    QVERIFY(set && set->isVisible() && set->isEnabled()); QVERIFY(reset && reset->isVisible());
    captured.args = {{"width", 335}, {"height", 195}};
    QVERIFY(manager.commands().execute("tab.peek.setSize", captured));
    QCOMPARE(manager.peekSize(second, Edge::Left), QSize(335, 195)); QCOMPARE(manager.peekSize(first, Edge::Left), QSize(250, 170));
    QVERIFY(manager.commands().execute(reset->data().toString(), captured));
    QCOMPARE(manager.peekSize(second, Edge::Left), QSize(250, 170)); QCOMPARE(manager.peekSizePolicy(second, Edge::Left), PeekSizePolicy{});
    QVERIFY(manager.pinTab(second, true));
    captured.group = manager.model().groupForTab(second);
    refreshCommandAction(&manager, set, captured); QVERIFY(!set->isVisible());
    QVERIFY(!manager.commands().execute("tab.peek.setSize", captured));
    captured.tab = central; captured.group = manager.model().groupForTab(central);
    refreshCommandAction(&manager, set, captured); QVERIFY(!set->isVisible());
    QVERIFY(!manager.commands().execute("tab.peek.resetSize", captured));
}

void PeekSizeTest::shortcutOpensAKeyboardEditableSizeDialog()
{
    WindowManager manager; const auto window = manager.createWindow();
    auto* content = new QPlainTextEdit("Keyboard target");
    const auto tab = manager.addTab(window, content, {"Side", "editor", {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge::Left));
    QVERIFY(manager.commands().setShortcut("tab.peek.setSize", QKeySequence("Ctrl+Alt+P")));
    auto* const host = manager.windowWidget(window);
    sizeWindow(host); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(host)); QVERIFY(manager.activateTab(tab));
    QVERIFY(QTest::qWaitForWindowActive(host));
    bool interacted = false, timedOut = false; QTimer navigation, watchdog;
    navigation.setInterval(20); watchdog.setSingleShot(true);
    connect(&navigation, &QTimer::timeout, this, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "PeekSizeDialog") return;
        auto* width = dialog->findChild<QSpinBox*>("PeekWidthInput");
        auto* height = dialog->findChild<QSpinBox*>("PeekHeightInput");
        auto* buttons = dialog->findChild<QDialogButtonBox*>("PeekSizeButtons");
        QVERIFY(width && height && buttons);
        // Modal registration precedes native activation. Keep the original
        // watchdog running while waiting for actual initial keyboard focus.
        if (!dialog->isActiveWindow() || !width->hasFocus()) return;
        navigation.stop(); interacted = true;
        QVERIFY(dialog->isActiveWindow()); QVERIFY(width->hasFocus());
        QCOMPARE(width->value(), 0); QCOMPARE(height->value(), 0);
        QCOMPARE(width->specialValueText(), QStringLiteral("默认宽度"));
        QCOMPARE(height->specialValueText(), QStringLiteral("填满高度"));
        QTest::keyClick(width, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(width, "369");
        QTest::keyClick(width, Qt::Key_Tab); QVERIFY(height->hasFocus());
        QVERIFY(dialog->isActiveWindow());
        QTest::keyClick(height, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(height, "217");
        buttons->button(QDialogButtonBox::Ok)->setFocus();
        QVERIFY(dialog->isActiveWindow());
        QTest::keyClick(buttons->button(QDialogButtonBox::Ok), Qt::Key_Return);
    });
    connect(&watchdog, &QTimer::timeout, this, [&] {
        timedOut = true;
        const auto* modal = QApplication::activeModalWidget();
        const auto* focus = QApplication::focusWidget();
        const auto* active = QApplication::activeWindow();
        qWarning() << "Peek-size navigation timed out; dialogActive=" << (modal && modal->isActiveWindow())
                   << "focus=" << (focus ? focus->objectName() : QStringLiteral("<none>"))
                   << "activeWindow=" << (active ? active->objectName() : QStringLiteral("<none>"));
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->objectName() == "PeekSizeDialog") dialog->reject();
    });
    navigation.start(); watchdog.start(2000);
    QTest::keyClick(content, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
    navigation.stop(); watchdog.stop(); QVERIFY(interacted); QVERIFY(!timedOut);
    QCOMPARE(manager.peekSize(tab, Edge::Left), QSize(369, 217)); QTRY_COMPARE(peek(manager, window)->width(), 369);
    QVERIFY(content->isVisible());
}

void PeekSizeTest::dialogCancellationAndTargetLossAreSafe_data()
{
    QTest::addColumn<QString>("action");
    for (const auto* action : {"cancel", "remove", "pin", "destroy"}) QTest::newRow(action) << QString::fromLatin1(action);
}

void PeekSizeTest::dialogCancellationAndTargetLossAreSafe()
{
    QFETCH(QString, action);
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    const auto tab = manager->addTab(window, new QLabel("Side"), {"Side", "label", {}});
    QVERIFY(manager->moveTabToEdge(tab, Edge::Top));
    QVERIFY(manager->setPeekSizePolicy(tab, Edge::Top, {std::nullopt, 185}));
    sizeWindow(manager->windowWidget(window)); manager->showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager->windowWidget(window))); QVERIFY(manager->activateTab(tab));
    bool interacted = false, timedOut = false; QTimer navigation, watchdog;
    navigation.setInterval(20); watchdog.setSingleShot(true);
    connect(&navigation, &QTimer::timeout, this, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "PeekSizeDialog") return;
        navigation.stop(); interacted = true;
        if (action == "cancel") QTest::keyClick(dialog, Qt::Key_Escape);
        else if (action == "remove") QVERIFY(manager->closeTab(tab));
        else if (action == "pin") QVERIFY(manager->pinTab(tab, true));
        else manager.reset();
    });
    connect(&watchdog, &QTimer::timeout, this, [&] {
        timedOut = true;
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->objectName() == "PeekSizeDialog") dialog->reject();
    });
    navigation.start(); watchdog.start(2000);
    const bool executed = manager->execute("tab.peek.setSize", {}, window);
    navigation.stop(); watchdog.stop(); QVERIFY(interacted); QVERIFY(!timedOut);
    QCOMPARE(executed, action == "cancel");
    if (action == "destroy") QVERIFY(!manager);
    else if (action == "remove") QVERIFY(!manager->model().tab(tab));
    else QCOMPARE(manager->peekSize(tab, Edge::Top), QSize(250, 185));
}

void PeekSizeTest::aSynchronousSaverCanDeleteTheManager()
{
    auto* manager = new WindowManager; QPointer<WindowManager> retained(manager);
    const auto window = manager->createWindow();
    const auto tab = manager->addTab(window, new QLabel("Side"), {"Side", "label", {}});
    QVERIFY(manager->moveTabToEdge(tab, Edge::Left));
    QJsonObject saved;
    connect(manager, &WindowManager::layoutChanged, this, [&] { saved = manager->saveLayout(); delete manager; });
    QVERIFY(manager->setPeekSizePolicy(tab, Edge::Left, {330, std::nullopt})); QVERIFY(!retained);
    LayoutModel model; QVERIFY(model.fromJson(saved.value("model").toObject()));
    QCOMPARE(model.tab(tab)->verticalPeekSize, (std::optional<PeekSizePolicy>{PeekSizePolicy{330, std::nullopt}}));
    QVERIFY(!model.tab(tab)->horizontalPeekSize);
}

void PeekSizeTest::resizingContentCanDeleteTheManager()
{
    auto* manager = new WindowManager; QPointer<WindowManager> retained(manager);
    const auto window = manager->createWindow(); auto* content = new ResizeCallbackLabel;
    const auto tab = manager->addTab(window, content, {"Side", "label", {}});
    QVERIFY(manager->moveTabToEdge(tab, Edge::Left));
    sizeWindow(manager->windowWidget(window)); manager->showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager->windowWidget(window))); QVERIFY(manager->activateTab(tab));
    QCoreApplication::processEvents(); QSize savedSize; int callbacks = 0;
    const QPointer<QWidget> contentObserver(content), windowObserver(manager->windowWidget(window));
    content->resized = [&] {
        ++callbacks; savedSize = manager->peekSize(tab, Edge::Left); content->resized = {};
        delete manager;
    };
    manager->setPeekSizePolicy(tab, Edge::Left, {330, std::nullopt});
    QTRY_VERIFY(!retained); QCOMPARE(callbacks, 1); QCOMPARE(savedSize, QSize(330, 170));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!contentObserver); QVERIFY(!windowObserver);
}

void PeekSizeTest::queuedInputIsHarmlessUntilDeferredDeletion()
{
    auto* manager = new WindowManager; QPointer<WindowManager> retained(manager);
    const auto window = manager->createWindow(); auto* content = new ResizeCallbackLabel;
    const auto tab = manager->addTab(window, content, {"Side", "label", {}});
    const auto centralFirst = manager->addTab(window, new QLabel("Central first"), {"Central first", "label", {}});
    manager->addTab(window, new QLabel("Central second"), {"Central second", "label", {}});
    const auto pinned = manager->addTab(window, new QLabel("Pinned"), {"Pinned", "label", {}});
    QVERIFY(manager->moveTabToEdge(pinned, Edge::Right, true));
    QVERIFY(manager->moveTabToEdge(tab, Edge::Left));
    sizeWindow(manager->windowWidget(window)); manager->showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager->windowWidget(window))); QVERIFY(manager->activateTab(tab));
    QCoreApplication::processEvents(); QSize savedSize; int callbacks = 0;
    const QPointer<QWidget> contentObserver(content), windowObserver(manager->windowWidget(window));
    const QPointer<GroupTabs> centralObserver(pinnedGroup(*manager, window, manager->model().groupForTab(centralFirst)));
    const QPointer<GroupTabs> pinnedObserver(pinnedGroup(*manager, window, manager->model().groupForTab(pinned)));
    const QPointer<QWidget> headerObserver(peek(*manager, window)->findChild<QWidget*>("PeekTitleBar"));
    QPointer<QAbstractButton> edgeObserver;
    for (auto* button : windowObserver->findChildren<QAbstractButton*>("EdgeButton"))
        if (button->isVisible() && button->property("tabId").toUuid() == tab) edgeObserver = button;
    QPointer<QToolButton> maximizeObserver;
    for (auto* button : windowObserver->findChildren<QToolButton*>("CaptionButton"))
        if (button->property("role").toString() == "maximize") maximizeObserver = button;
    QVERIFY(centralObserver && pinnedObserver && headerObserver && edgeObserver && maximizeObserver);
    content->resized = [&] {
        ++callbacks; savedSize = manager->peekSize(tab, Edge::Left); content->resized = {};
        delete manager;
    };
    manager->setPeekSizePolicy(tab, Edge::Left, {330, std::nullopt});
    QTRY_VERIFY(!retained); QCOMPARE(callbacks, 1); QCOMPARE(savedSize, QSize(330, 170));
    // The owner is gone while Qt's layout still owns these widgets. Deliver
    // queued input before DeferredDelete: widget-held callbacks are not Qt
    // connections whose receiver lifetime automatically disconnects them.
    QVERIFY(contentObserver && windowObserver);
    bool queuedInputDelivered = false;
    QVERIFY(QMetaObject::invokeMethod(windowObserver, [&] {
        QVERIFY(contentObserver && windowObserver && centralObserver && pinnedObserver);
        const auto geometry = windowObserver->geometry();
        maximizeObserver->click(); QVERIFY(!windowObserver->isMaximized());
        QCOMPARE(windowObserver->geometry(), geometry);
        centralObserver->setCurrentIndex((centralObserver->currentIndex() + 1) % centralObserver->count());
        centralObserver->closeRequested(0);
        for (const auto& name : {"UnpinCurrentButton", "UnpinAllButton"}) {
            auto* button = pinnedObserver->findChild<QToolButton*>(name); QVERIFY(button); button->click();
        }
        auto* bar = centralObserver->tabBar();
        sendDrag(bar, bar->tabRect(0).center()); sendContextMenu(bar, bar->tabRect(0).center());
        sendDrag(edgeObserver, edgeObserver->rect().center()); sendContextMenu(edgeObserver, edgeObserver->rect().center());
        sendDrag(headerObserver, headerObserver->rect().center());
        QVERIFY(!QApplication::activePopupWidget());
        QCOMPARE(centralObserver->count(), 2); QCOMPARE(pinnedObserver->count(), 1);
        auto* liveWindow = static_cast<WorkspaceWindow*>(windowObserver.data());
        auto* toolbarMenu = liveWindow->strips()->createContextMenu(liveWindow);
        QVERIFY(toolbarMenu); QVERIFY(toolbarMenu->findChild<QAction*>("LockStripsAction"));
        for (auto* action : toolbarMenu->findChildren<QAction*>())
            QVERIFY(action->property("windowSystemCommand").toString().isEmpty());
        delete toolbarMenu;
        windowObserver->resize(windowObserver->size() + QSize(1, 1));
        QVERIFY(contentObserver && windowObserver); QCOMPARE(callbacks, 1);
        queuedInputDelivered = true;
    }, Qt::QueuedConnection));
    QCoreApplication::sendPostedEvents(windowObserver, QEvent::MetaCall);
    QVERIFY(queuedInputDelivered); QCOMPARE(savedSize, QSize(330, 170));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!contentObserver); QVERIFY(!windowObserver); QVERIFY(!centralObserver); QVERIFY(!pinnedObserver);
}

void PeekSizeTest::defaultPanelsStretchOneAxisAndUseUniformCompactMargins()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Side"), {"Side", "label", {}});
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, direction));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); QVERIFY(manager.activateTab(tab));
    auto* panel = peek(manager, window); QVERIFY(panel);
    const auto area = peekAvailable(surface(manager, window));
    const QSize expected = horizontal(direction) ? QSize(250, area.height()) : QSize(area.width(), 170);
    QTRY_COMPARE(panel->size(), expected);
    QCOMPARE(panel->layout()->contentsMargins(), QMargins(1, 1, 1, 1));
    auto* primary = panel->findChild<QWidget*>("PeekResizeHandle");
    auto* secondary = panel->findChild<QWidget*>(horizontal(direction) ? "PeekHeightResizeHandle" : "PeekWidthResizeHandle");
    auto* corner = panel->findChild<QWidget*>("PeekCornerResizeHandle");
    QVERIFY(primary && secondary && corner);
    for (auto* handle : {primary, secondary, corner}) {
        QVERIFY(handle->isVisible()); QVERIFY(panel->rect().contains(handle->geometry()));
        QCOMPARE(qMin(handle->width(), handle->height()), 4);
    }
    QCOMPARE(secondary->cursor().shape(), horizontal(direction) ? Qt::SizeVerCursor : Qt::SizeHorCursor);
    QVERIFY(corner->cursor().shape() == Qt::SizeFDiagCursor || corner->cursor().shape() == Qt::SizeBDiagCursor);
    const auto before = panel->size();
    const auto start = secondary->rect().center();
    const auto delta = horizontal(direction) ? QPoint(0, -40) : QPoint(-40, 0);
    QTest::mousePress(secondary, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(secondary, start + delta);
    const QSize resized = horizontal(direction) ? QSize(before.width(), before.height() - 40)
                                                : QSize(before.width() - 40, before.height());
    QTRY_COMPARE(panel->size(), resized);
    QTest::mouseRelease(secondary, Qt::LeftButton, Qt::NoModifier, secondary->rect().center());
    const auto policy = manager.peekSizePolicy(tab, direction);
    if (horizontal(direction)) { QVERIFY(!policy.width); QCOMPARE(policy.height, std::optional<int>(resized.height())); }
    else { QCOMPARE(policy.width, std::optional<int>(resized.width())); QVERIFY(!policy.height); }
    surface(manager, window)->closePeek(); QVERIFY(manager.activateTab(tab));
    QTRY_COMPARE(peek(manager, window)->size(), resized);
}

void PeekSizeTest::bothDimensionsAndCornerResizingWorkOnEverySide()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    WindowManager manager; const auto window = manager.createWindow();
    auto* content = new QPlainTextEdit("Both axes keep the same editor");
    const auto tab = manager.addTab(window, content, {"Side", "editor", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, direction));
    QVERIFY(manager.setPeekSize(tab, direction, QSize(340, 230)));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); QVERIFY(manager.activateTab(tab));
    QPointer<QWidget> panel = peek(manager, window); QVERIFY(panel); QTRY_COMPARE(panel->size(), QSize(340, 230));
    const auto pinnedExtent = manager.model().group(manager.model().groupForTab(tab))->edgeExtent;
    auto* corner = panel->findChild<QWidget*>("PeekCornerResizeHandle"); QVERIFY(corner);
    const auto start = corner->rect().center();
    const QPoint delta(direction == Edge::Right ? -24 : 24, direction == Edge::Bottom ? -18 : 18);
    QTest::mousePress(corner, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(corner, start + delta);
    QTRY_COMPARE(panel->size(), QSize(364, 248));
    QTest::mouseRelease(corner, Qt::LeftButton, Qt::NoModifier, corner->rect().center());
    QCOMPARE(peek(manager, window), panel.data()); QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.peekSize(tab, direction), QSize(364, 248));
    QCOMPARE(manager.model().group(manager.model().groupForTab(tab))->edgeExtent, pinnedExtent);
    const auto saved = manager.saveLayout();
    WindowManager restored; restored.setContentFactory([](const TabOptions& options, PaneId) { return new QLabel(options.title); });
    QVERIFY(restored.restoreLayout(saved)); QCOMPARE(restored.peekSize(tab, direction), QSize(364, 248));
    QCOMPARE(restored.peekSizePolicy(tab, direction), (PeekSizePolicy{364, 248}));
    sizeWindow(restored.windowWidget(window)); restored.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(restored.windowWidget(window))); QVERIFY(restored.activateTab(tab));
    QTRY_COMPARE(peek(restored, window)->size(), QSize(364, 248));
}

void PeekSizeTest::oppositeSidesShareSizesWithoutChangingTheOtherOrientation()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Side"), {"Side", "label", {}});
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.setPeekSize(tab, Edge::Left, QSize(340, 245)));
    QVERIFY(manager.setPeekSize(tab, Edge::Top, QSize(730, 185)));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    for (const auto direction : {Edge::Left, Edge::Top, Edge::Right, Edge::Bottom, Edge::Left}) {
        QVERIFY(manager.moveTabToEdge(tab, direction)); QVERIFY(manager.activateTab(tab));
        const QSize expected = horizontal(direction) ? QSize(340, 245) : QSize(730, 185);
        QTRY_COMPARE(peek(manager, window)->size(), expected);
        QCOMPARE(manager.peekSize(tab, direction), expected);
    }
    QVERIFY(manager.resetPeekSize(tab, Edge::Right));
    QCOMPARE(manager.peekSize(tab, Edge::Left), QSize(250, 170));
    QCOMPARE(manager.peekSize(tab, Edge::Bottom), QSize(730, 185));
    const auto area = peekAvailable(surface(manager, window));
    QTRY_COMPARE(peek(manager, window)->size(), QSize(250, area.height()));
    const auto saved = manager.saveLayout();
    WindowManager restored; restored.setContentFactory([](const TabOptions& options, PaneId) { return new QLabel(options.title); });
    QVERIFY(restored.restoreLayout(saved));
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Right), PeekSizePolicy{});
    QCOMPARE(restored.peekSize(tab, Edge::Top), QSize(730, 185));
    QVERIFY(restored.resetPeekSize(tab, Edge::Top));
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Left), PeekSizePolicy{});
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Bottom), PeekSizePolicy{});
}

void PeekSizeTest::autoAxisCommandsPreserveViewportStretch()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Side"), {"Side", "label", {}});
    manager.addTab(window, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge::Left));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); QVERIFY(manager.activateTab(tab));
    QVERIFY(manager.execute("tab.peek.setSize", {{"width", 375}, {"height", 0}}, window));
    QCOMPARE(manager.peekSizePolicy(tab, Edge::Right), (PeekSizePolicy{375, std::nullopt}));
    QTRY_COMPARE(peek(manager, window)->size(), QSize(375, peekAvailable(surface(manager, window)).height()));
    QVERIFY(manager.moveTabToEdge(tab, Edge::Bottom)); QVERIFY(manager.activateTab(tab));
    QVERIFY(manager.execute("tab.peek.setSize", {{"width", 0}, {"height", 195}}, window));
    QCOMPARE(manager.peekSizePolicy(tab, Edge::Top), (PeekSizePolicy{std::nullopt, 195}));
    QCOMPARE(manager.peekSizePolicy(tab, Edge::Left), (PeekSizePolicy{375, std::nullopt}));
    QTRY_COMPARE(peek(manager, window)->size(), QSize(peekAvailable(surface(manager, window)).width(), 195));
    QVERIFY(manager.execute("tab.peek.resetSize", {}, window));
    QCOMPARE(manager.peekSizePolicy(tab, Edge::Bottom), PeekSizePolicy{});
    QCOMPARE(manager.peekSizePolicy(tab, Edge::Left), (PeekSizePolicy{375, std::nullopt}));
}

void PeekSizeTest::directionalPoliciesRejectInvalidSizesWithoutReplacingContent()
{
    WindowManager manager; const auto window = manager.createWindow();
    auto* content = new QLabel("Retained"); const auto tab = manager.addTab(window, content, {"Side", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(manager.moveTabToEdge(tab, Edge::Left)); QVERIFY(manager.setPeekSize(tab, Edge::Left, QSize(345, 235)));
    sizeWindow(manager.windowWidget(window)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); QVERIFY(manager.activateTab(tab));
    auto* panel = peek(manager, window); QVERIFY(panel); const auto geometry = panel->geometry();
    const auto saved = manager.saveLayout(); QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    QVERIFY(!manager.setPeekSize(tab, Edge::Right, QSize(139, 170)));
    QVERIFY(!manager.setPeekSizePolicy(tab, Edge::Left, {250, 99}));
    QVERIFY(!manager.setPeekSizePolicy(tab, Edge(-1), {}));
    QVERIFY(!manager.resetPeekSize(QUuid::createUuid(), Edge::Left));
    QCOMPARE(manager.saveLayout(), saved); QCOMPARE(changes.size(), 0);
    QCOMPARE(peek(manager, window), panel); QCOMPARE(panel->geometry(), geometry); QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(manager.setPeekSizePolicy(tab, Edge::Left, {345, 235})); QCOMPARE(changes.size(), 0);
}

QTEST_MAIN(PeekSizeTest)
#include "peek_size_test.moc"
