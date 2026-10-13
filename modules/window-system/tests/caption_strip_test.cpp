// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
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
    QCOMPARE(strips->stripIds(), QStringList({"title", "caption-controls"}));
    QCOMPARE(window->saveStripLayout(), complete);
    QVERIFY(!window->findChild<QWidget*>("MenuStrip"));
    QVERIFY(!window->findChild<QWidget*>("StatusBar"));
    QVERIFY(strips->setStripWidth("caption-controls", 206));
    QVERIFY(strips->moveStrip("caption-controls", 1, 0));
    const auto changed = window->saveStripLayout();
    const auto settings = changed.value("strips").toObject();
    QCOMPARE(settings.value("workspace"), complete.value("strips").toObject().value("workspace"));
    QVERIFY(manager.promoteWindow(id));
    strips = window->strips(); QVERIFY(strips);
    QCOMPARE(strips->saveLayout(), changed);
    QVERIFY(!strips->isStripVisible("workspace"));
    QVERIFY(!strips->isStripItemVisible("workspace", "pane.split.right"));
    QCOMPARE(strips->stripOrder(1).last(), QStringLiteral("caption-controls"));
    QCOMPARE(manager.paneWidget(pane), content);
    const auto snapshot = manager.saveLayout();
    int restoredContents = 0;
    manager.setContentFactory([&restoredContents](const TabOptions& options, PaneId) {
        ++restoredContents; return new QLabel(options.title);
    });
    QVERIFY(manager.restoreLayout(snapshot));
    auto* restored = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(restored);
    QCOMPARE(restored->saveStripLayout(), changed);
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

QTEST_MAIN(CaptionStripTest)
#include "caption_strip_test.moc"
