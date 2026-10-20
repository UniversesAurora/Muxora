// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include "style/tooltip_layout.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QHelpEvent>
#include <QToolTip>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
#include <QMenuBar>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QTest>
#include <QToolButton>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <windowsx.h>
#endif

using namespace window_system;

namespace {

WindowId createFixture(WindowManager& manager, bool floating, const WindowOptions& options = {})
{
    const auto root = manager.createWindow(WindowKind::Workbench, options);
    return floating ? manager.createWindow(WindowKind::Floating, options, root) : root;
}

QToolButton* captionButton(QWidget* window, const QString& role)
{
    for (auto* button : window->findChildren<QToolButton*>("CaptionButton"))
        if (button->property("role").toString() == role) return button;
    return nullptr;
}

void addWindowKinds()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false;
    QTest::newRow("floating") << true;
}

#ifdef Q_OS_WIN
LPARAM nativePoint(QWidget* window, QWidget* child, QPoint local)
{
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto client = child->mapTo(window, local);
    const qreal ratio = window->devicePixelRatioF();
    POINT point{qRound(client.x() * ratio), qRound(client.y() * ratio)};
    if (!ClientToScreen(handle, &point)) return 0;
    return MAKELPARAM(point.x, point.y);
}

class CaptionHoverProbe final : public QObject {
public:
    explicit CaptionHoverProbe(QWidget* button) : QObject(button) { button->installEventFilter(this); }
    int enters = 0;
    int leaves = 0;
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Enter) ++enters;
        if (event->type() == QEvent::Leave) ++leaves;
        return false;
    }
};

QVector<QPair<LPARAM, LRESULT>> resizeHits(const RECT& bounds)
{
    const int midX = (bounds.left + bounds.right) / 2;
    const int midY = (bounds.top + bounds.bottom) / 2;
    return {{MAKELPARAM(bounds.left + 1, midY), HTLEFT},
            {MAKELPARAM(bounds.right - 2, midY), HTRIGHT},
            {MAKELPARAM(midX, bounds.top + 1), HTTOP},
            {MAKELPARAM(midX, bounds.bottom - 2), HTBOTTOM},
            {MAKELPARAM(bounds.left + 1, bounds.top + 1), HTTOPLEFT},
            {MAKELPARAM(bounds.right - 2, bounds.top + 1), HTTOPRIGHT},
            {MAKELPARAM(bounds.left + 1, bounds.bottom - 2), HTBOTTOMLEFT},
            {MAKELPARAM(bounds.right - 2, bounds.bottom - 2), HTBOTTOMRIGHT}};
}
#endif

QImage referenceCaption(QLabel* caption, const QString& text, const QImage& actual)
{
    // Use Qt's ordinary label as the independent drawing reference. It receives
    // the expected visible text, while the real caption must retain full text.
    QLabel reference(text);
    reference.setTextFormat(Qt::PlainText);
    reference.setAlignment(Qt::AlignCenter);
    reference.setFont(caption->font());
    reference.setContentsMargins(caption->contentsMargins());
    auto palette = caption->palette();
    palette.setColor(QPalette::Window, actual.pixelColor(0, 0));
    reference.setPalette(palette);
    reference.setAutoFillBackground(true);
    reference.resize(caption->size());
    // Match QWidget::grab's native raster surface. Rendering directly to an
    // alpha QImage disables Windows ClearType and changes glyph edge pixels.
    QPixmap expected(actual.size());
    expected.setDevicePixelRatio(actual.devicePixelRatio());
    expected.fill(palette.color(QPalette::Window));
    reference.render(&expected);
    return expected.toImage().convertToFormat(actual.format());
}

void doubleClickCaption(QLabel* caption)
{
    const auto position = caption->rect().center();
    const auto global = caption->mapToGlobal(position);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, position, global,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(caption, &doubleClick);
    QMouseEvent release(QEvent::MouseButtonRelease, position, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(caption, &release);
}

constexpr int stripSpacingForTest = 4; // CommandStripHost spacing between strips of a row.

bool reserveTitleWidth(WorkspaceWindow* window, int contentWidth)
{
    // The title is now required to remain elastic. Reserve the rest of the
    // row with its left neighbour (the menu; caption controls have a fixed
    // width), exercising the actual elastic layout.
    auto* caption = window->findChild<QLabel*>("CaptionTitle");
    const auto menuId = window->strips()->stripIds().contains("menu") ? QStringLiteral("menu") : QStringLiteral("floating-menu");
    auto* menu = window->strips()->stripWidget(menuId);
    return caption && menu && window->strips()->setStripWidth(menuId,
        menu->width() + caption->width() - contentWidth);
}

} // namespace

class CaptionStripTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void controlsCanGrowAndMoveButNeverCompressOrBecomeElastic_data() { addWindowKinds(); }
    void controlsCanGrowAndMoveButNeverCompressOrBecomeElastic();
    void captionButtonsRetainWindowAndCloseGuardBehavior_data() { addWindowKinds(); }
    void captionButtonsRetainWindowAndCloseGuardBehavior();
    void movedMaximizeButtonRetainsNativeHitTesting_data() { addWindowKinds(); }
    void movedMaximizeButtonRetainsNativeHitTesting();
    void lightweightCustomizationPreservesTheCompleteChromeCache();
    void legacyLayoutsIntroduceTheProtectedCaptionStrip();
    void titleCentersAndElidesWithoutLosingItsFullCaption_data() { addWindowKinds(); }
    void titleCentersAndElidesWithoutLosingItsFullCaption();
    void titleMinimumIncludesTheUnlockedHandleAndCannotBeOverriddenByLayout_data() { addWindowKinds(); }
    void titleMinimumIncludesTheUnlockedHandleAndCannotBeOverriddenByLayout();
    void centeredTitleKeepsDoubleClickAndContentIdentity_data() { addWindowKinds(); }
    void centeredTitleKeepsDoubleClickAndContentIdentity();
    void floatingMenuRestoresBesideTheTitleWithoutMovingTheWorkbenchMenu();
#ifdef Q_OS_WIN
    void maximizeEntireRectangleRetainsNativeSnapHitAndQtHover_data();
    void maximizeEntireRectangleRetainsNativeSnapHitAndQtHover();
    void captionHitPriorityPreservesResizeEdgesAndCorners_data() { addWindowKinds(); }
    void captionHitPriorityPreservesResizeEdgesAndCorners();
    void disabledHiddenCompactAndFullscreenCaptionsRejectNativeSnap_data() { addWindowKinds(); }
    void disabledHiddenCompactAndFullscreenCaptionsRejectNativeSnap();
    void maximizedCaptionControlsOwnTheActualScreenEdge_data();
    void maximizedCaptionControlsOwnTheActualScreenEdge();
    void captionButtonOrderFollowsTheBehavior_data() { addWindowKinds(); }
    void captionButtonOrderFollowsTheBehavior();
    void restoredCaptionControlsTouchTheWindowBorder_data() { addWindowKinds(); }
    void restoredCaptionControlsTouchTheWindowBorder();
    void captionControlsNeverTakeExtraWidth_data() { addWindowKinds(); }
    void captionControlsNeverTakeExtraWidth();
    void captionOrderChangesKeepAllButtonsWhileMaximized_data() { addWindowKinds(); }
    void captionOrderChangesKeepAllButtonsWhileMaximized();
    void reusedToolTipsKeepTheThemedSize();
    void longToolTipsWrapAtAReadableWidth();
#ifdef Q_OS_WIN
    void maximizedCaptionButtonsShowThemedToolTips();
#endif
    void expandedCaptionControlsReachEveryScreenCorner_data();
    void expandedCaptionControlsReachEveryScreenCorner();
    void fixedResizeAxesRemainRespected_data() { addWindowKinds(); }
    void fixedResizeAxesRemainRespected();
#endif
};

void CaptionStripTest::controlsCanGrowAndMoveButNeverCompressOrBecomeElastic()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    auto* strips = window->strips(); QVERIFY(strips);
    auto* controls = strips->stripWidget("caption-controls"); QVERIFY(controls);
    auto* viewport = controls->findChild<QWidget*>("StripViewport"); QVERIFY(viewport);
    QVERIFY(!strips->stripConstraints("caption-controls").compressible);
    QVERIFY(!strips->stripConstraints("caption-controls").elasticAllowed);
    QVERIFY(!strips->isStripOptionsEnabled("caption-controls"));
    QVERIFY(!strips->createOverflowMenu("caption-controls", window));
    QVERIFY(!strips->setStripSizing("caption-controls", CommandStripHost::StripSizing::Elastic));
    QVERIFY(!manager.execute("window.toolbar.setSizing", {{"strip", "caption-controls"}, {"sizing", "elastic"}}, id));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const int naturalWidth = controls->width();
    QCOMPARE(naturalWidth, 126);
    // A fixed width: no narrower, no wider, and no saved width.
    QVERIFY(strips->stripConstraints("caption-controls").fixedWidth);
    QVERIFY(!strips->setStripWidth("caption-controls", naturalWidth - 1));
    QVERIFY(!strips->setStripWidth("caption-controls", naturalWidth + 80));
    QCOMPARE(controls->width(), naturalWidth);
    QCOMPARE(window->saveStripLayout().value("strips").toObject().value("caption-controls").toObject().value("width").toInt(), -1);
    QVERIFY(strips->overflowItems("caption-controls").isEmpty());
    for (const auto& role : {QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")}) {
        auto* button = captionButton(window, role); QVERIFY(button);
        QCOMPARE(button->size(), QSize(42, 32));
        QCOMPARE(button->mapTo(controls, QPoint()).y(), 0);
        QVERIFY(viewport->rect().contains(QRect(button->mapTo(viewport, QPoint()), button->size())));
    }
    auto* close = captionButton(window, "close");
    QCOMPARE(close->mapTo(viewport, QPoint()).x() + close->width(), viewport->width());
    QCOMPARE(captionButton(window, "minimize")->mapTo(controls, QPoint()).y(), 0);
    QVERIFY(manager.execute("window.toggleStripsLock", {}, id));
    QVERIFY(!strips->isLocked());
    QVERIFY(strips->moveStrip("caption-controls", 1, 0));
    QCOMPARE(strips->stripOrder(1).first(), QStringLiteral("caption-controls"));
    QVERIFY(manager.execute("window.toggleStripsLock", {}, id));
    QVERIFY(strips->isLocked());
    window->resize(window->minimumSize()); QCoreApplication::processEvents();
    QVERIFY(controls->width() >= naturalWidth);
    for (auto* button : window->findChildren<QToolButton*>("CaptionButton")) {
        QVERIFY(button->isVisible());
        QVERIFY(viewport->rect().contains(QRect(button->mapTo(viewport, QPoint()), button->size())));
    }
}

void CaptionStripTest::captionButtonsRetainWindowAndCloseGuardBehavior()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    const auto tab = manager.addTab(id, new QLabel("Content"), {"Content", "test.label", {}});
    QVERIFY(!tab.isNull());
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    QVERIFY(window->strips()->moveStrip("caption-controls", 1, 0));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* minimize = captionButton(window, "minimize"); QVERIFY(minimize);
    auto* maximize = captionButton(window, "maximize"); QVERIFY(maximize);
    auto* close = captionButton(window, "close"); QVERIFY(close);
    minimize->click(); QTRY_VERIFY(window->isMinimized());
    window->showNormal(); QTRY_VERIFY(!window->isMinimized());
    maximize->click(); QTRY_VERIFY(window->isMaximized());
    QCOMPARE(maximize->property("role").toString(), QStringLiteral("restore"));
    QCOMPARE(maximize->accessibleName(), QStringLiteral("还原"));
    maximize->click(); QTRY_VERIFY(!window->isMaximized());
    QCOMPARE(maximize->property("role").toString(), QStringLiteral("maximize"));
    QCOMPARE(maximize->accessibleName(), QStringLiteral("最大化"));
    QVector<TabId> requested;
    manager.setCloseGuard([&requested](const QVector<TabId>& tabs) { requested = tabs; return false; });
    close->click();
    QCOMPARE(requested, QVector<TabId>{tab});
    QVERIFY(manager.model().window(id)); QVERIFY(manager.model().tab(tab)); QVERIFY(window->isVisible());
    manager.setCloseGuard([](const QVector<TabId>&) { return true; });
    close->click();
    QVERIFY(!manager.model().window(id)); QVERIFY(!manager.model().tab(tab));
}

void CaptionStripTest::movedMaximizeButtonRetainsNativeHitTesting()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    QVERIFY(window->strips()->moveStrip("caption-controls", 1, 0));
    window->setWindowFlag(Qt::WindowStaysOnTopHint);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* button = captionButton(window, "maximize"); QVERIFY(button); QVERIFY(button->isVisible());
    auto* strips = window->strips();
    QCOMPARE(strips->stripArea("caption-controls"), CommandStripHost::Area::Top);
    QVERIFY(strips->stripOrder(CommandStripHost::Area::Top, 1).contains("caption-controls"));
    auto* previousRow = strips->stripWidget("title")->parentWidget();
    auto* controlsRow = strips->stripWidget("caption-controls")->parentWidget();
    QVERIFY(previousRow != controlsRow && previousRow->isVisible() && previousRow->height() > 0);
    const int nextRowTop = previousRow->mapTo(window, QPoint()).y() + previousRow->height();
    QCOMPARE(controlsRow->mapTo(window, QPoint()).y(), nextRowTop);
    QVERIFY(button->mapTo(window, QPoint()).y() >= nextRowTop);
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(window->winId());
    const auto client = button->mapTo(window, button->rect().center());
    const qreal ratio = window->devicePixelRatioF();
    POINT point{qRound(client.x() * ratio), qRound(client.y() * ratio)};
    QVERIFY(ClientToScreen(handle, &point));
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)), LRESULT(HTMAXBUTTON));
    const auto location = MAKELPARAM(point.x, point.y);
    SendMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, location);
    QVERIFY(button->isDown());
    SendMessageW(handle, WM_NCLBUTTONUP, HTMAXBUTTON, location);
    QTRY_VERIFY(window->isMaximized());
    QVERIFY(!button->isDown());
    window->showNormal();
#else
    button->click(); QTRY_VERIFY(window->isMaximized());
    button->click(); QTRY_VERIFY(!window->isMaximized());
#endif
}

void CaptionStripTest::lightweightCustomizationPreservesTheCompleteChromeCache()
{
    WindowManager manager;
    const auto id = manager.createWindow();
    const auto owner = manager.createWindow();
    auto* content = new QLabel("Persistent content");
    const auto tab = manager.addTab(id, content, {"Content", "test.label", {}}); QVERIFY(!tab.isNull());
    const auto pane = manager.model().tab(tab)->activePane;
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    auto* strips = window->strips(); QVERIFY(strips);
    QVERIFY(strips->setStripVisible("workspace", false));
    QVERIFY(strips->setStripItemVisible("workspace", "pane.split.right", false));
    const auto complete = window->saveStripLayout();
    QVERIFY(manager.demoteWindow(id, owner));
    strips = window->strips(); QVERIFY(strips);
    QCOMPARE(strips->stripIds(), QStringList({"floating-menu", "title", "caption-controls"}));
    QCOMPARE(window->saveInactiveStripLayouts().value("workbench").toObject(), complete);
    QVERIFY(!window->saveStripLayout().value("strips").toObject().contains("menu"));
    QVERIFY(!window->saveStripLayout().value("strips").toObject().contains("workspace"));
    QVERIFY(window->findChild<QMenuBar*>("MenuStrip"));
    QVERIFY(!window->findChild<QWidget*>("StatusBar"));
    QVERIFY(strips->setStripWidth("floating-menu", 206));
    QVERIFY(strips->moveStrip("caption-controls", 1, 0));
    const auto changed = window->saveStripLayout();
    QCOMPARE(window->saveInactiveStripLayouts().value("workbench").toObject(), complete);
    QVERIFY(manager.promoteWindow(id));
    strips = window->strips(); QVERIFY(strips);
    QCOMPARE(strips->saveLayout(), complete);
    QVERIFY(!strips->isStripVisible("workspace"));
    QVERIFY(!strips->isStripItemVisible("workspace", "pane.split.right"));
    QCOMPARE(strips->stripOrder(0), (QStringList{"menu", "title", "caption-controls"}));
    QCOMPARE(window->saveInactiveStripLayouts().value("floating").toObject(), changed);
    QCOMPARE(manager.paneWidget(pane), content);
    const auto snapshot = manager.saveLayout();
    int restoredContents = 0;
    manager.setContentFactory([&restoredContents](const TabOptions& options, PaneId) {
        ++restoredContents; return new QLabel(options.title);
    });
    QVERIFY(manager.restoreLayout(snapshot));
    auto* restored = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(restored);
    QCOMPARE(restored->saveStripLayout(), complete);
    QCOMPARE(restored->saveInactiveStripLayouts().value("floating").toObject(), changed);
    QVERIFY(manager.demoteWindow(id, owner));
    QCOMPARE(restored->saveStripLayout(), changed);
    QCOMPARE(restored->strips()->stripOrder(1), (QStringList{"caption-controls"}));
    QCOMPARE(restoredContents, 1);
    auto* restoredContent = qobject_cast<QLabel*>(manager.paneWidget(pane)); QVERIFY(restoredContent);
    QCOMPARE(restoredContent->text(), QStringLiteral("Content"));
}

void CaptionStripTest::legacyLayoutsIntroduceTheProtectedCaptionStrip()
{
    WindowManager manager;
    const auto id = manager.createWindow();
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    auto legacy = window->saveStripLayout();
    auto rows = legacy.value("rows").toArray();
    for (int row = 0; row < rows.size(); ++row) {
        auto entries = rows[row].toArray();
        for (qsizetype index = entries.size(); index-- > 0;)
            if (entries[index].toString() == QStringLiteral("caption-controls")) entries.removeAt(index);
        rows[row] = entries;
    }
    auto values = legacy.value("strips").toObject(); values.remove("caption-controls");
    auto menu = values.value("menu").toObject(); menu.insert("visible", false); values.insert("menu", menu);
    legacy.insert("rows", rows); legacy.insert("strips", values);
    window->restoreStripLayout(legacy);
    auto* strips = window->strips(); QVERIFY(strips);
    QCOMPARE(strips->stripOrder(0).last(), QStringLiteral("caption-controls"));
    QVERIFY(!strips->isStripVisible("menu"));
    QCOMPARE(strips->stripSizing("caption-controls"), CommandStripHost::StripSizing::Content);
    QVERIFY(!strips->stripConstraints("caption-controls").compressible);
    QVERIFY(!strips->stripConstraints("caption-controls").elasticAllowed);
    auto modified = window->saveStripLayout();
    auto savedValues = modified.value("strips").toObject();
    auto controls = savedValues.value("caption-controls").toObject();
    controls.insert("sizing", "elastic"); savedValues.insert("caption-controls", controls);
    modified.insert("strips", savedValues);
    window->restoreStripLayout(modified);
    QCOMPARE(strips->stripSizing("caption-controls"), CommandStripHost::StripSizing::Content);
    QVERIFY(!strips->isStripOptionsEnabled("caption-controls"));
}

void CaptionStripTest::floatingMenuRestoresBesideTheTitleWithoutMovingTheWorkbenchMenu()
{
    WindowManager manager; const auto id = manager.createWindow(); const auto owner = manager.createWindow();
    auto* content = new QLabel("Retained body");
    const auto tab = manager.addTab(id, content, {"local menu", {}, {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(window->strips()->moveStrip("menu", CommandStripHost::Area::Bottom, 0, 0));
    QVERIFY(window->strips()->moveStrip("title", CommandStripHost::Area::Bottom, 1, 0));
    QVERIFY(window->strips()->setStripItemVisible("menu", "视图", false));
    const auto complete = window->saveStripLayout();
    QVERIFY(manager.demoteWindow(id, owner));
    QVERIFY(window->strips()->moveStrip("floating-menu", CommandStripHost::Area::Bottom, 0, 0));
    QVERIFY(window->strips()->moveStrip("title", CommandStripHost::Area::Bottom, 1, 0));
    const auto floating = window->saveStripLayout();
    QVERIFY(manager.promoteWindow(id));
    for (int cycle = 0; cycle < 3; ++cycle) {
        QVERIFY(manager.demoteWindow(id, owner));
        auto* strips = window->strips();
        QVERIFY(strips->stripWidget("floating-menu")); QVERIFY(!strips->stripWidget("menu"));
        QCOMPARE(strips->stripArea("floating-menu"), CommandStripHost::Area::Bottom);
        QVERIFY(strips->stripWidget("floating-menu")->parentWidget() != strips->stripWidget("title")->parentWidget());
        QCOMPARE(strips->stripOrder(CommandStripHost::Area::Bottom, 0), (QStringList{"floating-menu"}));
        QCOMPARE(strips->stripOrder(CommandStripHost::Area::Bottom, 1), (QStringList{"title"}));
        QCOMPARE(strips->rowCount(CommandStripHost::Area::Top), 1);
        QCOMPARE(strips->rowCount(CommandStripHost::Area::Bottom), 2);
        QCOMPARE(window->saveStripLayout(), floating);
        QCOMPARE(window->saveInactiveStripLayouts().value("workbench").toObject(), complete);
        QCOMPARE(window->findChildren<QMenuBar*>("MenuStrip").size(), 1);
        QCOMPARE(manager.paneWidget(pane), content);
        QVERIFY(manager.promoteWindow(id));
        strips = window->strips();
        QVERIFY(strips->stripWidget("menu")); QVERIFY(!strips->stripWidget("floating-menu"));
        QCOMPARE(strips->stripArea("menu"), CommandStripHost::Area::Bottom);
        QCOMPARE(strips->stripOrder(CommandStripHost::Area::Bottom, 0), (QStringList{"menu"}));
        QCOMPARE(strips->stripOrder(CommandStripHost::Area::Bottom, 1), (QStringList{"title"}));
        QVERIFY(!strips->isStripItemVisible("menu", "视图"));
        QCOMPARE(strips->saveLayout(), complete);
        QCOMPARE(window->findChildren<QMenuBar*>("MenuStrip").size(), 1);
        QCOMPARE(manager.paneWidget(pane), content);
    }
}

void CaptionStripTest::titleCentersAndElidesWithoutLosingItsFullCaption()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto title = QStringLiteral("Muxora — 远程工作空间 / Production servers / Complete caption");
    const auto id = createFixture(manager, floating, {title, QSize(900, 500)});
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    auto* strips = window->strips(); QVERIFY(strips);
    auto* caption = window->findChild<QLabel*>("CaptionTitle"); QVERIFY(caption);
    auto* strip = strips->stripWidget("title"); QVERIFY(strip);
    auto* viewport = strip->findChild<QWidget*>("StripViewport"); QVERIFY(viewport);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(!strips->setStripSizing("title", CommandStripHost::StripSizing::Content));
    QVERIFY(reserveTitleWidth(window, 96));
    QCOMPARE(caption->alignment(), Qt::Alignment(Qt::AlignCenter));
    QCOMPARE(strip->width(), 96);
    QCOMPARE(caption->width(), viewport->width());
    const auto full = floating ? window->windowTitle() : title;
    QCOMPARE(caption->text(), full);
    QCOMPARE(caption->accessibleName(), full);
    QVERIFY(caption->toolTip().isEmpty());
    const auto expectedText = QFontMetrics(caption->font()).elidedText(full, Qt::ElideRight, caption->contentsRect().width());
    QVERIFY(expectedText != full);
    QVERIFY(expectedText.endsWith(QChar(0x2026)));
    const auto compressed = caption->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(compressed, referenceCaption(caption, expectedText, compressed));
    QVERIFY(compressed != referenceCaption(caption, full, compressed));
    const int completeWidth = QFontMetrics(caption->font()).horizontalAdvance(full) + 40;
    window->resize(completeWidth + 600, window->height());
    QCoreApplication::processEvents();
    QVERIFY(reserveTitleWidth(window, completeWidth));
    QCOMPARE(caption->width(), completeWidth);
    const auto expanded = caption->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(expanded, referenceCaption(caption, full, expanded));
    QCOMPARE(caption->text(), full);
}

void CaptionStripTest::titleMinimumIncludesTheUnlockedHandleAndCannotBeOverriddenByLayout()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    auto* strips = window->strips(); QVERIFY(strips);
    QCOMPARE(strips->stripConstraints("title").minimumContentWidth, 96);
    QVERIFY(!strips->setStripWidth("title", 95));
    QVERIFY(!strips->setStripSizing("title", CommandStripHost::StripSizing::Content));
    QVERIFY(strips->setStripWidth("title", 96));
    QVERIFY(manager.execute("window.toggleStripsLock", {}, id));
    auto* strip = strips->stripWidget("title"); QVERIFY(strip);
    auto* viewport = strip->findChild<QWidget*>("StripViewport"); QVERIFY(viewport);
    auto* handle = strip->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(reserveTitleWidth(window, 96));
    QVERIFY(handle->isVisible());
    QCOMPARE(viewport->width(), 96);
    QVERIFY(!strips->setStripWidth("title", 96 + handle->width() - 1));
    QVERIFY(strips->setStripWidth("title", 96 + handle->width()));
    auto layout = window->saveStripLayout();
    auto settings = layout.value("strips").toObject();
    auto savedTitle = settings.value("title").toObject();
    savedTitle.insert("width", 36); settings.insert("title", savedTitle); layout.insert("strips", settings);
    window->restoreStripLayout(layout);
    QCOMPARE(strips->stripConstraints("title").minimumContentWidth, 96);
    QVERIFY(viewport->width() >= 96);
    window->resize(window->minimumSize()); QCoreApplication::processEvents();
    QVERIFY(viewport->width() >= 96);
}

void CaptionStripTest::centeredTitleKeepsDoubleClickAndContentIdentity()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    auto* content = new QLabel("Persistent content");
    const auto tab = manager.addTab(id, content, {"Content", "test.label", {}}); QVERIFY(!tab.isNull());
    const auto pane = manager.model().tab(tab)->activePane;
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    auto* caption = window->findChild<QLabel*>("CaptionTitle"); QVERIFY(caption);
    QVERIFY(caption->property("windowDragArea").toBool());
    QVERIFY(window->strips()->windowDragAreas().contains(caption));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    doubleClickCaption(caption); QTRY_VERIFY(window->isMaximized());
    doubleClickCaption(caption); QTRY_VERIFY(!window->isMaximized());
    QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.model().group(manager.model().groupForTab(tab))->windowId, id);
}

#ifdef Q_OS_WIN
void CaptionStripTest::maximizeEntireRectangleRetainsNativeSnapHitAndQtHover_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<bool>("maximized"); QTest::addColumn<int>("vertical");
    QTest::addColumn<int>("order");
    const char* orders[] = {"windows", "reversed", "macos"};
    for (const bool floating : {false, true}) for (const bool maximized : {false, true})
        for (const int vertical : {0, 1, 2}) for (const int order : {0, 1, 2})
            QTest::newRow(qPrintable(QString("%1-%2-%3-%4").arg(floating ? "floating" : "workbench")
                .arg(maximized ? "maximized" : "restored").arg(vertical).arg(orders[order])))
                << floating << maximized << vertical << order;
}

void CaptionStripTest::maximizeEntireRectangleRetainsNativeSnapHitAndQtHover()
{
    QFETCH(bool, floating); QFETCH(bool, maximized); QFETCH(int, vertical); QFETCH(int, order);
    WindowManager manager; const auto id = createFixture(manager, floating);
    // The Win11 Snap flyout follows the maximize button wherever the chosen
    // order puts it: last but one, middle, or last.
    auto behavior = manager.behavior(); behavior.captionButtonOrder = CaptionButtonOrder(order); manager.setBehavior(behavior);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    if (maximized) { window->showMaximized(); QTRY_VERIFY(window->isMaximized()); }
    auto* button = captionButton(window, maximized ? "restore" : "maximize"); QVERIFY(button);
    QVERIFY(button->isVisible() && button->isEnabled()); QCOMPARE(button->size(), QSize(42, 32));
    // The default first row is essential: a moved second-row fixture cannot
    // reproduce the restored window's caption/resize-rim overlap.
    QCOMPARE(window->strips()->stripArea("caption-controls"), CommandStripHost::Area::Top);
    QVERIFY(window->strips()->stripOrder(CommandStripHost::Area::Top, 0).contains("caption-controls"));
    QVERIFY(button->mapTo(window, QPoint()).y() < 5);
    const int y = vertical == 0 ? 0 : vertical == 1 ? button->height() / 2 : button->height() - 1;
    const auto earlyLocation = nativePoint(window, button, QPoint(button->width() / 2, y)); QVERIFY(earlyLocation);
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    QTest::mouseMove(window, window->rect().center());
    SendMessageW(handle, WM_NCMOUSELEAVE, 0, 0); QVERIFY(!button->underMouse());
    const auto quiet = button->grab().toImage();
    const auto location = nativePoint(window, button, QPoint(button->width() / 2, y)); QVERIFY(location);
    if (earlyLocation != location) qInfo() << "Caption layout settled after queued input:" << earlyLocation << location;
    const auto beforeGeometry = window->geometry(); const auto beforeHandle = window->effectiveWinId();
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, location), LRESULT(HTMAXBUTTON));
    CaptionHoverProbe hover(button);
    SendMessageW(handle, WM_NCMOUSEMOVE, HTMAXBUTTON, location);
    QVERIFY(button->underMouse()); QCOMPARE(hover.enters, 1);
    QVERIFY(button->grab().toImage() != quiet);
    SendMessageW(handle, WM_NCMOUSELEAVE, 0, 0);
    QVERIFY(!button->underMouse()); QCOMPARE(hover.leaves, 1);
    QCOMPARE(button->grab().toImage(), quiet);
    QCOMPARE(window->geometry(), beforeGeometry); QCOMPARE(window->effectiveWinId(), beforeHandle);
    QCOMPARE(window->isMaximized(), maximized);
}

void CaptionStripTest::captionHitPriorityPreservesResizeEdgesAndCorners()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    RECT bounds{}; QVERIFY(GetWindowRect(handle, &bounds));
    for (const auto& hit : resizeHits(bounds)) QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, hit.first), hit.second);
    window->showMaximized(); QTRY_VERIFY(window->isMaximized()); QVERIFY(GetWindowRect(handle, &bounds));
    for (const auto& hit : resizeHits(bounds)) QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, hit.first), LRESULT(HTCLIENT));
    window->showNormal(); QTRY_VERIFY(!window->isMaximized()); QVERIFY(GetWindowRect(handle, &bounds));
    for (const auto& hit : resizeHits(bounds)) QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, hit.first), hit.second);
}

void CaptionStripTest::disabledHiddenCompactAndFullscreenCaptionsRejectNativeSnap()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createFixture(manager, floating);
    const auto tab = manager.addTab(id, new QLabel("Retained content"), {"Retained", {}, {}}); QVERIFY(!tab.isNull());
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* button = captionButton(window, "maximize"); QVERIFY(button);
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    button->setEnabled(false);
    auto location = nativePoint(window, button, button->rect().center()); QVERIFY(location);
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, location), LRESULT(HTCLIENT));
    SendMessageW(handle, WM_NCMOUSEMOVE, HTMAXBUTTON, location); QVERIFY(!button->underMouse()); QVERIFY(!button->isDown());
    button->setEnabled(true); button->hide();
    location = nativePoint(window, button, button->rect().center()); QVERIFY(location);
    QVERIFY(SendMessageW(handle, WM_NCHITTEST, 0, location) != HTMAXBUTTON);
    SendMessageW(handle, WM_NCMOUSEMOVE, HTMAXBUTTON, location); QVERIFY(!button->underMouse());
    button->show();
    QVERIFY(manager.setViewMode(id, ViewMode::WindowFullscreen)); QTRY_VERIFY(window->isFullScreen());
    QVERIFY(button->isVisible());
    location = nativePoint(window, button, button->rect().center()); QVERIFY(location);
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, location), LRESULT(HTCLIENT));
    SendMessageW(handle, WM_NCMOUSEMOVE, HTMAXBUTTON, location); QVERIFY(!button->underMouse());
    RECT bounds{}; QVERIFY(GetWindowRect(handle, &bounds));
    for (const auto& hit : resizeHits(bounds)) QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, hit.first), LRESULT(HTCLIENT));
    QVERIFY(manager.setViewMode(id, ViewMode::Normal)); QTRY_VERIFY(!window->isFullScreen());
    window->setDragPresentation(WorkspaceWindow::DragPresentation::Tab);
    location = nativePoint(window, button, button->rect().center()); QVERIFY(location);
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, location), LRESULT(HTCLIENT));
    SendMessageW(handle, WM_NCMOUSEMOVE, HTMAXBUTTON, location); QVERIFY(!button->underMouse());
    window->setDragPresentation(WorkspaceWindow::DragPresentation::Window);
    location = nativePoint(window, button, button->rect().center()); QVERIFY(location);
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, location), LRESULT(HTMAXBUTTON));
}

void CaptionStripTest::maximizedCaptionControlsOwnTheActualScreenEdge_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<QString>("role");
    QTest::addColumn<bool>("startsAtWorkArea");
    for (const bool startsAtWorkArea : {false, true}) for (const bool floating : {false, true})
        for (const auto& role : {QStringLiteral("minimize"), QStringLiteral("restore"), QStringLiteral("close")})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2%3").arg(floating ? "floating" : "workbench", role,
                startsAtWorkArea ? QStringLiteral("-already-fills-work-area") : QString{})))
                << floating << role << startsAtWorkArea;
}

void CaptionStripTest::maximizedCaptionControlsOwnTheActualScreenEdge()
{
    QFETCH(bool, floating); QFETCH(QString, role); QFETCH(bool, startsAtWorkArea);
    WindowManager manager; const auto id = createFixture(manager, floating);
    const auto tab = manager.addTab(id, new QLabel("Retained body"), {"Retained", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    const QPointer<QWidget> body(manager.paneWidget(pane));
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    if (startsAtWorkArea) window->setGeometry(window->screen()->availableGeometry());
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    auto* button = captionButton(window, role); QVERIFY(button && button->isVisible() && button->isEnabled());
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    MONITORINFO monitor{sizeof(MONITORINFO)};
    QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
    // Sample the actual last physical pixel, not a logical point inset from
    // the button. The top row is the screen's work-area edge for all controls;
    // close additionally owns its upper-right corner.
    const int indexFromRight = role == "close" ? 0 : role == "restore" ? 1 : 2;
    const int nativeWidth = qRound(42 * window->devicePixelRatioF());
    const POINT edge{monitor.rcWork.right - 1 - indexFromRight * nativeWidth, monitor.rcWork.top};
    const auto nativeClientFillsWorkArea = [handle, monitor] {
        POINT origin{}; RECT client{};
        return ClientToScreen(handle, &origin) && GetClientRect(handle, &client)
            && origin.x == monitor.rcWork.left && origin.y == monitor.rcWork.top
            && client.right - client.left == monitor.rcWork.right - monitor.rcWork.left
            && client.bottom - client.top == monitor.rcWork.bottom - monitor.rcWork.top;
    };
    // Qt updates its state before the asynchronous native maximize/layout
    // finishes. Wait for the independently observed client rectangle, then
    // convert the same absolute screen-edge point on every geometry sample.
    QTRY_VERIFY(nativeClientFillsWorkArea());
    QString edgeDiagnostic;
    const auto containsScreenEdge = [window, button, handle, edge, nativeWidth, &edgeDiagnostic] {
        POINT client = edge;
        if (!ScreenToClient(handle, &client)) return false;
        const qreal ratio = window->devicePixelRatioF();
        const QPoint logical(qFloor(client.x / ratio), qFloor(client.y / ratio));
        const auto area = QRect(button->mapTo(window, QPoint()), button->size());
        edgeDiagnostic = QStringLiteral("edge=(%1,%2) nativeWidth=%3 DPR=%4 client=(%5,%6) logical=(%7,%8) button=(%9,%10,%11,%12) window=(%13,%14)")
            .arg(edge.x).arg(edge.y).arg(nativeWidth).arg(ratio).arg(client.x).arg(client.y)
            .arg(logical.x()).arg(logical.y()).arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height())
            .arg(window->width()).arg(window->height());
        return button->rect().contains(button->mapFrom(window, logical));
    };
    QTRY_VERIFY2(containsScreenEdge(), qPrintable(edgeDiagnostic));
    QCOMPARE(button->mapTo(window, QPoint()).y(), 0);
    auto* close = captionButton(window, "close"); QVERIFY(close);
    QCOMPARE(close->mapTo(window, QPoint(close->width(), 0)).x(), window->width());
    const LPARAM position = MAKELPARAM(edge.x, edge.y);
    const LRESULT expectedHit = role == "close" ? HTCLOSE : role == "minimize" ? HTMINBUTTON : HTMAXBUTTON;
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, position), expectedHit);
    SendMessageW(handle, WM_NCMOUSELEAVE, 0, 0);
    CaptionHoverProbe hover(button);
    SendMessageW(handle, WM_NCMOUSEMOVE, WPARAM(expectedHit), position);
    QVERIFY(button->underMouse()); QCOMPARE(hover.enters, 1);
    SendMessageW(handle, WM_NCMOUSELEAVE, 0, 0);
    QVERIFY(!button->underMouse()); QCOMPARE(hover.leaves, 1);
    int clicks = 0; QVector<TabId> closeRequests;
    QObject::connect(button, &QToolButton::clicked, window, [&clicks] { ++clicks; });
    manager.setCloseGuard([&closeRequests](const QVector<TabId>& tabs) { closeRequests = tabs; return false; });
    SendMessageW(handle, WM_NCLBUTTONDOWN, WPARAM(expectedHit), position);
    QVERIFY(button->isDown()); QCOMPARE(GetCapture(), handle);
    SendMessageW(handle, WM_NCLBUTTONUP, WPARAM(expectedHit), position);
    QCOMPARE(clicks, 1); QVERIFY(!button->isDown()); QVERIFY(GetCapture() != handle);
    if (role == "minimize") QTRY_VERIFY(window->isMinimized());
    else if (role == "restore") QTRY_VERIFY(!window->isMaximized());
    else { QCOMPARE(closeRequests, QVector<TabId>{tab}); QVERIFY(window->isMaximized() && window->isVisible()); }
    QCOMPARE(window->effectiveWinId(), WId(handle)); QCOMPARE(manager.paneWidget(pane), body.data());
    QVERIFY(manager.model().validate().ok);
}

void CaptionStripTest::captionButtonOrderFollowsTheBehavior()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* controls = window->strips()->stripWidget("caption-controls"); QVERIFY(controls);
    const auto x = [window](const QString& role) { return captionButton(window, role)->mapTo(window, QPoint()).x(); };
    const auto settle = [] { QCoreApplication::processEvents(); };
    QCOMPARE(manager.behavior().captionButtonOrder, CaptionButtonOrder::Windows);
    QVERIFY(x("minimize") < x("maximize") && x("maximize") < x("close"));
    // The strip is exactly as wide as its three buttons in every order:
    // the outermost button is the strip's edge on both sides.
    const int natural = controls->width();
    QCOMPARE(natural, 3 * 42);
    auto* close = captionButton(window, "close");
    const auto fills = [controls, window](const QStringList& roles) {
        const auto* first = captionButton(window, roles.first());
        const auto* last = captionButton(window, roles.last());
        return first->mapTo(controls, QPoint()).x() == 0
            && last->mapTo(controls, QPoint(last->width(), 0)).x() == controls->width();
    };
    QVERIFY(fills({"minimize", "maximize", "close"}));
    const struct { CaptionButtonOrder order; const char* key; QStringList roles; } orders[] = {
        {CaptionButtonOrder::Reversed, "reversed", {"close", "maximize", "minimize"}},
        {CaptionButtonOrder::MacOS, "macos", {"close", "minimize", "maximize"}},
        {CaptionButtonOrder::Windows, "windows", {"minimize", "maximize", "close"}}};
    for (const auto& expected : orders) {
        auto behavior = manager.behavior(); behavior.captionButtonOrder = expected.order; manager.setBehavior(behavior); settle();
        QVERIFY2(x(expected.roles[0]) < x(expected.roles[1]) && x(expected.roles[1]) < x(expected.roles[2]), expected.key);
        QCOMPARE(controls->width(), natural);
        QVERIFY2(fills(expected.roles), expected.key);
        if (expected.order == CaptionButtonOrder::Windows)
            QCOMPARE(close->mapTo(controls, QPoint(close->width(), 0)).x(), controls->width());
        else QCOMPARE(close->mapTo(controls, QPoint()).x(), 0);
        // Persisted with the other window behavior and restored exactly.
        const auto snapshot = manager.saveLayout();
        QCOMPARE(snapshot.value("behavior").toObject().value("captionButtonOrder").toString(), QString::fromLatin1(expected.key));
        WindowManager restored; QVERIFY(restored.restoreLayout(snapshot).ok);
        QCOMPARE(restored.behavior().captionButtonOrder, expected.order);
    }
    // Unknown names and non-string values are rejected, not silently defaulted.
    for (const QJsonValue& value : {QJsonValue("mac"), QJsonValue(true), QJsonValue(2)}) {
        auto invalid = manager.saveLayout(); auto settings = invalid.value("behavior").toObject();
        settings.insert("captionButtonOrder", value); invalid.insert("behavior", settings);
        WindowManager rejected; QVERIFY(!rejected.restoreLayout(invalid).ok);
    }
    // Layouts saved before the setting existed keep the Windows order.
    auto legacy = manager.saveLayout(); auto settings = legacy.value("behavior").toObject();
    settings.remove("captionButtonOrder"); legacy.insert("behavior", settings);
    WindowManager restored; QVERIFY(restored.restoreLayout(legacy).ok);
    QCOMPARE(restored.behavior().captionButtonOrder, CaptionButtonOrder::Windows);
}

void CaptionStripTest::restoredCaptionControlsTouchTheWindowBorder()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(!window->isMaximized() && !window->isFullScreen());
    auto* close = captionButton(window, "close"); QVERIFY(close && close->isVisible());
    // Like native caption buttons, only the restored window's one-pixel
    // border separates the outermost button from the window edge.
    QTRY_COMPARE(close->mapTo(window, QPoint(close->width(), 0)).x(), window->width() - 1);
    QCOMPARE(close->mapTo(window, QPoint()).y(), 1);
    // Ordinary toolbars touch the border as well: the menu is the first
    // strip of the default row.
    auto* strips = window->strips();
    const auto menuId = floating ? QStringLiteral("floating-menu") : QStringLiteral("menu");
    auto* menu = strips->stripWidget(menuId); QVERIFY(menu);
    QCOMPARE(strips->stripOrder(0).first(), menuId);
    QCOMPARE(menu->mapTo(window, QPoint()).x(), 1);
    auto behavior = manager.behavior(); behavior.captionButtonOrder = CaptionButtonOrder::MacOS; manager.setBehavior(behavior);
    QVERIFY(strips->moveStrip("caption-controls", CommandStripHost::Area::Top, 0, 0));
    QTRY_COMPARE(close->mapTo(window, QPoint()).x(), 1);
    QCOMPARE(close->mapTo(window, QPoint()).y(), 1);
    // The menu follows directly after the three buttons.
    auto* controls = strips->stripWidget("caption-controls"); QVERIFY(controls);
    QCOMPARE(controls->width(), 3 * 42);
    QVERIFY(menu->mapTo(window, QPoint()).x() > close->mapTo(window, QPoint()).x());
    QVERIFY(manager.model().validate().ok);
}

void CaptionStripTest::captionControlsNeverTakeExtraWidth()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* strips = window->strips();
    auto* controls = strips->stripWidget("caption-controls"); QVERIFY(controls);
    const int natural = controls->width();
    QCOMPARE(natural, 3 * 42);
    // Removing the strip after the controls normally hands its width to the
    // previous strip, so the others keep their place. Fixed-width controls
    // do not absorb it: no blank space opens beside the buttons.
    auto* action = new QAction("Custom command", strips);
    strips->addActionStrip("custom", "Custom strip", {action}, 0);
    QVERIFY(strips->moveStrip("caption-controls", CommandStripHost::Area::Top, 0, 0));
    QVERIFY(strips->moveStrip("custom", CommandStripHost::Area::Top, 0, 1));
    const auto menuId = floating ? QStringLiteral("floating-menu") : QStringLiteral("menu");
    QCOMPARE(strips->stripOrder(0).mid(0, 3), (QStringList{"caption-controls", "custom", menuId}));
    QVERIFY(strips->moveStrip("custom", CommandStripHost::Area::Bottom, 0, 0));
    QCoreApplication::processEvents();
    QCOMPARE(controls->width(), natural);
    // The next strip follows after the ordinary strip spacing only.
    auto* menu = strips->stripWidget(menuId);
    QCOMPARE(menu->mapTo(window, QPoint()).x() - controls->mapTo(window, QPoint(controls->width(), 0)).x(), stripSpacingForTest);
    QCOMPARE(window->saveStripLayout().value("strips").toObject().value("caption-controls").toObject().value("width").toInt(), -1);
    // Layouts saved by earlier versions may carry a widened width: ignore it.
    auto layout = window->saveStripLayout();
    auto settings = layout.value("strips").toObject();
    auto entry = settings.value("caption-controls").toObject();
    entry.insert("width", 349); settings.insert("caption-controls", entry); layout.insert("strips", settings);
    window->restoreStripLayout(layout); QCoreApplication::processEvents();
    controls = strips->stripWidget("caption-controls"); QVERIFY(controls);
    QCOMPARE(controls->width(), natural);
    QCOMPARE(window->saveStripLayout().value("strips").toObject().value("caption-controls").toObject().value("width").toInt(), -1);
    QVERIFY(manager.model().validate().ok);
}

void CaptionStripTest::captionOrderChangesKeepAllButtonsWhileMaximized()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    auto* controls = window->strips()->stripWidget("caption-controls"); QVERIFY(controls);
    // Maximized, the middle button carries the "restore" role. Every order
    // change must keep all three buttons laid out side by side.
    const auto check = [&](const QStringList& roles) {
        QCoreApplication::processEvents();
        QList<QRect> rects;
        for (const auto& role : roles) {
            auto* button = captionButton(window, role);
            if (!button || !button->isVisible()) return false;
            rects.append(QRect(button->mapTo(controls, QPoint()), button->size()));
        }
        for (int i = 0; i + 1 < rects.size(); ++i)
            if (rects[i].right() + 1 != rects[i + 1].left() || rects[i].top() != rects[i + 1].top()) return false;
        return rects.first().left() == 0 && rects.last().right() + 1 == controls->width() && controls->width() == 3 * 42;
    };
    QVERIFY(check({"minimize", "restore", "close"}));
    const struct { CaptionButtonOrder order; QStringList roles; } orders[] = {
        {CaptionButtonOrder::Reversed, {"close", "restore", "minimize"}},
        {CaptionButtonOrder::MacOS, {"close", "minimize", "restore"}},
        {CaptionButtonOrder::Windows, {"minimize", "restore", "close"}},
        {CaptionButtonOrder::MacOS, {"close", "minimize", "restore"}}};
    for (const auto& expected : orders) {
        auto behavior = manager.behavior(); behavior.captionButtonOrder = expected.order; manager.setBehavior(behavior);
        QVERIFY2(check(expected.roles), qPrintable(expected.roles.join(',')));
    }
    // Restoring the window keeps the chosen order with the maximize role.
    window->showNormal(); QTRY_VERIFY(!window->isMaximized());
    QVERIFY(check({"close", "minimize", "maximize"}));
    QCOMPARE(controls->findChildren<QToolButton*>("CaptionButton").size(), 3);
}

namespace {
QWidget* visibleToolTip()
{
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->inherits("QTipLabel") && widget->isVisible()) return widget;
    return nullptr;
}

// Windows' tooltip fade shows the tip through a separate effect window; in a
// test that window's activation closes the tip before it appears. Tip sizes
// and styling do not depend on the effect, so tests show tips directly.
[[nodiscard]] auto withoutToolTipEffects()
{
    const bool fade = QApplication::isEffectEnabled(Qt::UI_FadeTooltip);
    const bool animate = QApplication::isEffectEnabled(Qt::UI_AnimateTooltip);
    QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
    return qScopeGuard([fade, animate] {
        QApplication::setEffectEnabled(Qt::UI_FadeTooltip, fade);
        QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, animate);
    });
}
} // namespace

void CaptionStripTest::longToolTipsWrapAtAReadableWidth()
{
    const auto effects = withoutToolTipEffects();
    WindowManager manager; const auto id = createFixture(manager, false);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    window->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window));
    const auto show = [](QWidget* button) {
        QHelpEvent help(QEvent::ToolTip, button->rect().center(), button->mapToGlobal(button->rect().center()));
        QApplication::sendEvent(button, &help);
    };
    const auto shown = [&show](QWidget* button) -> QWidget* {
        QToolTip::hideText();
        if (!QTest::qWaitFor([] { return !visibleToolTip(); })) return nullptr;
        show(button);
        QTest::qWaitFor([] { return visibleToolTip() != nullptr; });
        return visibleToolTip();
    };
    auto* shortButton = captionButton(window, "close");
    auto* longButton = captionButton(window, "minimize");
    const QString sentence = QStringLiteral("这是一段很长的提示文字，用来检查提示气泡不会被拉成细长的一整行；"
        "This long tooltip must wrap into a balanced card instead of one thin line. ");
    auto* tip = shown(shortButton); QVERIFY(tip);
    const int lineHeight = tip->height();
    QVERIFY(tip->width() <= singleLineToolTipWidth);
    // A long text wraps into a wide, balanced card: wider than tall and not
    // stretched beyond the aspect ratio.
    longButton->setToolTip(sentence);
    tip = shown(longButton); QVERIFY(tip);
    const QSize longSize = tip->size();
    QVERIFY2(longSize.height() >= 2 * lineHeight - 4, qPrintable(QStringLiteral("%1x%2").arg(longSize.width()).arg(longSize.height())));
    QVERIFY(longSize.width() > longSize.height());
    QVERIFY(longSize.width() <= (maximumToolTipAspectRatio + 1) * longSize.height());
    QVERIFY(tip->height() >= tip->heightForWidth(tip->width())); // Every line fits.
    QVERIFY(tip->screen()->availableGeometry().contains(tip->geometry()));
    // Six times the text grows the card wider instead of into a tall strip.
    longButton->setToolTip(sentence.repeated(6));
    tip = shown(longButton); QVERIFY(tip);
    const QSize longerSize = tip->size();
    QVERIFY2(longerSize.width() > longSize.width(), qPrintable(QStringLiteral("%1 vs %2").arg(longerSize.width()).arg(longSize.width())));
    QVERIFY(longerSize.width() > longerSize.height());
    QVERIFY(longerSize.width() <= (maximumToolTipAspectRatio + 1) * longerSize.height());
    QVERIFY(tip->height() >= tip->heightForWidth(tip->width()));
    QVERIFY(tip->screen()->availableGeometry().contains(tip->geometry()));
    // Reusing the visible tip for the long text wraps it the same way.
    show(shortButton); QCoreApplication::processEvents();
    show(longButton); QCoreApplication::processEvents();
    tip = visibleToolTip(); QVERIFY(tip);
    QCOMPARE(tip->size(), longerSize);
    // Back to a short tip: one line again.
    show(shortButton); QCoreApplication::processEvents();
    tip = visibleToolTip(); QVERIFY(tip);
    QCOMPARE(QToolTip::text(), shortButton->toolTip());
    QCOMPARE(tip->height(), lineHeight);
    QToolTip::hideText();
}

void CaptionStripTest::reusedToolTipsKeepTheThemedSize()
{
    // A host's own application style sheet survives the window theme.
    const QString hostRule = QStringLiteral("QLabel#HostRule { color: red; }");
    qApp->setStyleSheet(hostRule);
    const auto restoreSheet = qScopeGuard([] { qApp->setStyleSheet(QString()); });
    const auto effects = withoutToolTipEffects();
    WindowManager manager; const auto id = createFixture(manager, false);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    // Qt closes a tip as soon as its window is not the active one.
    window->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window));
    manager.setTheme(manager.theme()); // A second application must not duplicate the block.
    const auto sheet = qApp->styleSheet();
    QVERIFY(sheet.startsWith(hostRule));
    QCOMPARE(sheet.count(QStringLiteral("/* window_system tooltip */")), 1);
    QCOMPARE(sheet.count(QStringLiteral("QToolTip ")), 1);
    const auto show = [](QWidget* button) {
        QHelpEvent help(QEvent::ToolTip, button->rect().center(), button->mapToGlobal(button->rect().center()));
        QApplication::sendEvent(button, &help);
    };
    // The first tip is created with the theme; moving straight to another
    // control reuses the visible tip, which must keep the same height.
    show(captionButton(window, "minimize"));
    QTRY_VERIFY(visibleToolTip());
    QTest::qWait(300); // Let the first tip finish showing.
    auto* first = visibleToolTip(); QVERIFY(first);
    const int height = first->height(); QVERIFY(height > 0);
    for (const auto* role : {"maximize", "close", "minimize"}) {
        show(captionButton(window, role));
        QCoreApplication::processEvents();
        auto* tip = visibleToolTip(); QVERIFY2(tip, role);
        QCOMPARE(QToolTip::text(), captionButton(window, role)->toolTip());
        QCOMPARE(tip->height(), height);
    }
    QToolTip::hideText();
}

#ifdef Q_OS_WIN
void CaptionStripTest::maximizedCaptionButtonsShowThemedToolTips()
{
    const auto effects = withoutToolTipEffects();
    WindowManager manager; const auto id = createFixture(manager, false);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    // Qt closes a tip as soon as its window is not the active one. Earlier
    // windows of the run can leave this one below other programs: raise it.
    window->raise(); window->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    QToolTip::hideText(); QTRY_VERIFY(!visibleToolTip());
    const auto location = [window](QWidget* button) { return nativePoint(window, button, button->rect().center()); };
    auto* close = captionButton(window, "close");
    auto* minimize = captionButton(window, "minimize");
    QVERIFY(close && minimize);
    // Qt reports the maximized state before the native resize settles: wait
    // until the close button reaches the work area's right edge.
    QTRY_COMPARE(window->geometry(), window->screen()->availableGeometry());
    QTRY_COMPARE(close->mapTo(window, QPoint(close->width(), 0)).x(), window->width());
    // Maximized, the controls are native caption hits. The system's own
    // hover handling (classic yellow tooltip) is not used for them...
    // The native leave tracking follows the real cursor, so place it on the
    // control (no click or key is sent).
    // Another program's always-on-top window over the control would receive
    // the real cursor and end the hover: report that as occlusion.
    const auto pointAt = [handle](LPARAM where) {
        const POINT point{GET_X_LPARAM(where), GET_Y_LPARAM(where)};
        if (!SetCursorPos(point.x, point.y)) return QStringLiteral("SetCursorPos failed");
        const HWND under = GetAncestor(WindowFromPoint(point), GA_ROOT);
        if (under == handle) return QString();
        wchar_t name[128]{}; GetClassNameW(under, name, 128);
        DWORD process = 0; GetWindowThreadProcessId(under, &process);
        return QStringLiteral("Control occluded by window class %1 of process %2")
            .arg(QString::fromWCharArray(name)).arg(process);
    };
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, location(close)), LRESULT(HTCLOSE));
    { const auto occluded = pointAt(location(close)); QVERIFY2(occluded.isEmpty(), qPrintable(occluded)); }
    SendMessageW(handle, WM_NCMOUSEMOVE, HTCLOSE, location(close));
    QCOMPARE(SendMessageW(handle, WM_NCMOUSEHOVER, HTCLOSE, location(close)), LRESULT(0));
    // ...the Qt button shows its themed tooltip after Qt's wake-up delay.
    QTRY_VERIFY_WITH_TIMEOUT(visibleToolTip(), 3000);
    QCOMPARE(QToolTip::text(), close->toolTip());
    auto* closeTip = visibleToolTip(); QVERIFY(closeTip);
    const int height = closeTip->height();
    // Moving straight to the next control switches the tip at once and
    // keeps its size.
    { const auto occluded = pointAt(location(minimize)); QVERIFY2(occluded.isEmpty(), qPrintable(occluded)); }
    SendMessageW(handle, WM_NCMOUSEMOVE, HTMINBUTTON, location(minimize));
    QTRY_COMPARE_WITH_TIMEOUT(QToolTip::text(), minimize->toolTip(), 500);
    QVERIFY(visibleToolTip()); QCOMPARE(visibleToolTip()->height(), height);
    // Leaving the controls hides it.
    SendMessageW(handle, WM_NCMOUSELEAVE, 0, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!visibleToolTip(), 2000);
}
#endif

void CaptionStripTest::expandedCaptionControlsReachEveryScreenCorner_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<bool>("left");
    QTest::addColumn<bool>("bottom"); QTest::addColumn<bool>("fullscreen");
    for (const bool floating : {false, true}) for (const bool fullscreen : {false, true})
        for (const bool bottom : {false, true}) for (const bool left : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3-%4").arg(floating ? "floating" : "workbench",
                fullscreen ? "fullscreen" : "maximized", bottom ? "bottom" : "top", left ? "left" : "right")))
                << floating << left << bottom << fullscreen;
}

void CaptionStripTest::expandedCaptionControlsReachEveryScreenCorner()
{
    QFETCH(bool, floating); QFETCH(bool, left); QFETCH(bool, bottom); QFETCH(bool, fullscreen);
    WindowManager manager; const auto id = createFixture(manager, floating);
    const auto tab = manager.addTab(id, new QLabel("Retained body"), {"Retained", "label", {}});
    // Close is the outermost button in every corner: the left corners use
    // both close-first orders (reversed in the full window, macOS in the
    // floating one).
    auto behavior = manager.behavior();
    behavior.captionButtonOrder = !left ? CaptionButtonOrder::Windows
        : floating ? CaptionButtonOrder::MacOS : CaptionButtonOrder::Reversed;
    manager.setBehavior(behavior);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(window->strips()->moveStrip("caption-controls",
        bottom ? CommandStripHost::Area::Bottom : CommandStripHost::Area::Top, 0, left ? 0 : 1000)); // Out-of-range index appends.
    // Alone in a new bottom row the strip starts at the leading edge. Put the
    // elastic title before it, as a user would, so it reaches the right corner.
    if (bottom && !left) QVERIFY(window->strips()->moveStrip("title", CommandStripHost::Area::Bottom, 0, 0));
    // The global status bar lies below the bottom toolbars; the controls can
    // only reach the bottom edge when nothing is between them and the edge.
    if (bottom && !floating) QVERIFY(manager.setStatusBarVisible(id, false));
    if (fullscreen) { QVERIFY(manager.setViewMode(id, ViewMode::WindowFullscreen)); QTRY_VERIFY(window->isFullScreen()); }
    else { window->showMaximized(); QTRY_VERIFY(window->isMaximized()); }
    auto* close = captionButton(window, "close"); QVERIFY(close && close->isVisible());
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    MONITORINFO monitor{sizeof(MONITORINFO)};
    QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
    const RECT target = fullscreen ? monitor.rcMonitor : monitor.rcWork;
    const auto clientFillsTarget = [handle, target] {
        POINT origin{}; RECT client{};
        return ClientToScreen(handle, &origin) && GetClientRect(handle, &client)
            && origin.x == target.left && origin.y == target.top
            && client.right - client.left == target.right - target.left
            && client.bottom - client.top == target.bottom - target.top;
    };
    QTRY_VERIFY(clientFillsTarget());
    // The actual last physical pixel of the screen (or work-area) corner.
    const POINT corner{left ? target.left : target.right - 1, bottom ? target.bottom - 1 : target.top};
    QPoint logical; QString diagnostic;
    const auto containsCorner = [&] {
        POINT client = corner;
        if (!ScreenToClient(handle, &client)) return false;
        const qreal ratio = window->devicePixelRatioF();
        logical = QPoint(qFloor(client.x / ratio), qFloor(client.y / ratio));
        const QRect area(close->mapTo(window, QPoint()), close->size());
        diagnostic = QStringLiteral("corner=(%1,%2) logical=(%3,%4) close=(%5,%6,%7,%8) window=%9x%10")
            .arg(corner.x).arg(corner.y).arg(logical.x()).arg(logical.y()).arg(area.x()).arg(area.y())
            .arg(area.width()).arg(area.height()).arg(window->width()).arg(window->height());
        return close->rect().contains(close->mapFrom(window, logical));
    };
    QTRY_VERIFY2(containsCorner(), qPrintable(diagnostic));
    QVector<TabId> closeRequests;
    manager.setCloseGuard([&closeRequests](const QVector<TabId>& tabs) { closeRequests = tabs; return false; });
    int clicks = 0; QObject::connect(close, &QToolButton::clicked, window, [&clicks] { ++clicks; });
    if (!fullscreen) {
        // A maximized window routes caption buttons through the native hit test.
        const LPARAM position = MAKELPARAM(corner.x, corner.y);
        QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, position), LRESULT(HTCLOSE));
        SendMessageW(handle, WM_NCLBUTTONDOWN, WPARAM(HTCLOSE), position);
        SendMessageW(handle, WM_NCLBUTTONUP, WPARAM(HTCLOSE), position);
    } else {
        // Fullscreen keeps the controls in Qt's client hit testing. Deliver
        // the press/release to the window like QTest does, but without its
        // rule that a null (0, 0) position means "the window centre", which
        // would miss the top-left corner.
        const auto send = [window, logical](QEvent::Type type, Qt::MouseButtons buttons) {
            QMouseEvent mouse(type, QPointF(logical), window->mapToGlobal(QPointF(logical)),
                              Qt::LeftButton, buttons, Qt::NoModifier);
            QApplication::sendEvent(window->windowHandle(), &mouse);
        };
        send(QEvent::MouseButtonPress, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::NoButton);
    }
    auto* hit = window->childAt(logical);
    const auto hitName = hit ? QStringLiteral("%1#%2 role=%3").arg(hit->metaObject()->className(), hit->objectName(),
        hit->property("role").toString()) : QStringLiteral("none");
    QTRY_VERIFY2(clicks == 1, qPrintable(QStringLiteral("clicks=%1 childAt=%2 %3").arg(clicks).arg(hitName, diagnostic)));
    QVERIFY(closeRequests.contains(tab));
    QVERIFY(window->isVisible());
    QVERIFY(manager.model().validate().ok);
}

void CaptionStripTest::fixedResizeAxesRemainRespected()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    window->setFixedWidth(window->width()); QCoreApplication::processEvents();
    RECT bounds{}; QVERIFY(GetWindowRect(handle, &bounds));
    const int midY = (bounds.top + bounds.bottom) / 2;
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(bounds.left + 1, midY)), LRESULT(HTCLIENT));
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(bounds.right - 2, midY)), LRESULT(HTCLIENT));
    QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(bounds.left + 1, bounds.top + 1)), LRESULT(HTTOP));
    window->setFixedHeight(window->height()); QCoreApplication::processEvents();
    QVERIFY(GetWindowRect(handle, &bounds));
    for (const auto& hit : resizeHits(bounds)) QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, hit.first), LRESULT(HTCLIENT));
}
#endif

QTEST_MAIN(CaptionStripTest)
#include "caption_strip_test.moc"
