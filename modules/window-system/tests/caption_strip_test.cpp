// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
#include <QMenuBar>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QTest>
#include <QToolButton>

#ifdef Q_OS_WIN
#include <qt_windows.h>
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

bool reserveTitleWidth(WorkspaceWindow* window, int contentWidth)
{
    // The title is now required to remain elastic. Reserve the rest of the
    // row with its right neighbour, exercising the actual elastic layout.
    auto* caption = window->findChild<QLabel*>("CaptionTitle");
    auto* controls = window->strips()->stripWidget("caption-controls");
    return caption && controls && window->strips()->setStripWidth("caption-controls",
        controls->width() + caption->width() - contentWidth);
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
    QVERIFY(!strips->setStripWidth("caption-controls", naturalWidth - 1));
    QVERIFY(strips->setStripWidth("caption-controls", naturalWidth + 80));
    QCOMPARE(controls->width(), naturalWidth + 80);
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
    QVERIFY(strips->setStripWidth("caption-controls", 206));
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
    QCOMPARE(caption->toolTip(), full);
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
    for (const bool floating : {false, true}) for (const bool maximized : {false, true})
        for (const int vertical : {0, 1, 2})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(floating ? "floating" : "workbench")
                .arg(maximized ? "maximized" : "restored").arg(vertical))) << floating << maximized << vertical;
}

void CaptionStripTest::maximizeEntireRectangleRetainsNativeSnapHitAndQtHover()
{
    QFETCH(bool, floating); QFETCH(bool, maximized); QFETCH(int, vertical);
    WindowManager manager; const auto id = createFixture(manager, floating);
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
