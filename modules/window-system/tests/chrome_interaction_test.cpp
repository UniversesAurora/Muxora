// SPDX-License-Identifier: MIT
#include "native_input.hpp"
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"

#include <window_system/theme.hpp>
#include <window_system/window_manager.hpp>

#include <QAction>
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QTimer>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>

using namespace window_system;

namespace {

// Optional, bounded diagnostics for the real popup routing. This does not add
// waits or replace any timing/visibility assertions in the interaction test.
class PopupEventTrace final : public QObject
#ifdef Q_OS_WIN
    , public QAbstractNativeEventFilter
#endif
{
public:
    PopupEventTrace(QWidget* host, QToolButton* button)
        : host_(host), button_(button), menu_(button->menu()), path_(qEnvironmentVariable("MUXORA_CHROME_TRACE_PATH"))
    {
        timer_.start();
        if (path_.isEmpty()) return;
        command_ = host->findChild<QToolButton*>("StripCommandButton");
        qApp->installEventFilter(this);
#ifdef Q_OS_WIN
        hostHandle_ = reinterpret_cast<HWND>(host->effectiveWinId());
        qApp->installNativeEventFilter(this);
#endif
        connect(menu_, &QMenu::aboutToShow, this, [this] { note("signal-aboutToShow"); });
        connect(menu_, &QMenu::aboutToHide, this, [this] { note("signal-aboutToHide"); });
        note("trace-start");
    }
    ~PopupEventTrace() override
    {
        if (path_.isEmpty()) return;
#ifdef Q_OS_WIN
        qApp->removeNativeEventFilter(this);
#endif
        qApp->removeEventFilter(this); note("trace-end");
#ifdef Q_OS_WIN
        // Foreign-window hit testing can send synchronous messages. Collect it
        // only after the timed interaction and both filters have ended.
        POINT cursor{};
        GetCursorPos(&cursor);
        const auto hit = WindowFromPoint(cursor);
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        GetGUIThreadInfo(GetCurrentThreadId(), &gui);
        lines_.append(QStringLiteral("final-native cursor=%1,%2 capture=%3 hit=%4 hitRoot=%5 foreground=%6 guiCapture=%7 guiMenuOwner=%8")
            .arg(cursor.x).arg(cursor.y).arg(quintptr(GetCapture()), 0, 16)
            .arg(quintptr(hit), 0, 16).arg(quintptr(GetAncestor(hit, GA_ROOT)), 0, 16)
            .arg(quintptr(GetForegroundWindow()), 0, 16).arg(quintptr(gui.hwndCapture), 0, 16)
            .arg(quintptr(gui.hwndMenuOwner), 0, 16));
#endif
        QFile output(path_);
        if (output.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            output.write(lines_.join('\n').toUtf8());
    }
    void note(const QString& event)
    {
        if (path_.isEmpty() || lines_.size() >= 256) return;
        const auto cursor = QCursor::pos();
        lines_.append(QStringLiteral("%1ms %2 visible=%3 hidden=%4 popup=%5 down=%6 buttons=%7 cursor=%8,%9")
            .arg(timer_.elapsed()).arg(event).arg(menu_ && menu_->isVisible()).arg(menu_ && menu_->isHidden())
            .arg(QApplication::activePopupWidget() == menu_).arg(button_ && button_->isDown())
            .arg(int(QApplication::mouseButtons())).arg(cursor.x()).arg(cursor.y()));
        lines_.last() += QStringLiteral(" commandHover=%1 commandHit=%2 mouseGrabber=%3")
            .arg(command_ && command_->underMouse())
            .arg(command_ && command_->rect().contains(command_->mapFromGlobal(cursor)))
            .arg(quintptr(QWidget::mouseGrabber()), 0, 16);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        QString target;
        if (watched == host_) target = "host";
        else if (watched == button_) target = "button";
        else if (watched == menu_) target = "menu";
        else if (watched == command_) target = "command";
        else if (menu_ && watched == menu_->windowHandle()) target = "menu-window";
        else if (host_ && watched == host_->windowHandle()) target = "host-window";
        else return false;
#ifdef Q_OS_WIN
        if (event->type() == QEvent::WinIdChange) {
            if (watched == host_ && host_) hostHandle_ = reinterpret_cast<HWND>(host_->effectiveWinId());
            if (watched == menu_ && menu_) popupHandle_ = reinterpret_cast<HWND>(menu_->effectiveWinId());
        }
#endif
        switch (event->type()) {
        case QEvent::Show: case QEvent::Hide: case QEvent::ShowToParent: case QEvent::HideToParent:
        case QEvent::WinIdChange: case QEvent::WindowActivate: case QEvent::WindowDeactivate:
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::FocusIn: case QEvent::FocusOut:
        case QEvent::PlatformSurface: case QEvent::WindowStateChange: case QEvent::Move: case QEvent::Resize:
        case QEvent::Expose:
        case QEvent::Enter: case QEvent::Leave: case QEvent::MouseMove:
        case QEvent::HoverEnter: case QEvent::HoverLeave: case QEvent::HoverMove:
            note(target + QStringLiteral("-event-%1").arg(int(event->type()))); break;
        default: break;
        }
        return false;
    }
#ifdef Q_OS_WIN
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const auto* native = static_cast<const MSG*>(message);
        if (!native || lines_.size() >= 256) return false;
        if (native->message == WM_CAPTURECHANGED || native->message == WM_CANCELMODE
            || native->message == WM_DISPLAYCHANGE || native->message == WM_DPICHANGED
            || native->message == WM_SETTINGCHANGE || native->message == WM_ACTIVATEAPP
            || ((native->hwnd == hostHandle_ || native->hwnd == popupHandle_)
                && (native->message == WM_MOUSEMOVE || native->message == WM_MOUSELEAVE
                    || native->message == WM_LBUTTONDOWN || native->message == WM_LBUTTONUP
                    || native->message == WM_ACTIVATE)))
            lines_.append(QStringLiteral("%1ms native-%2 hwnd=%3 point=%4,%5 time=%6")
                .arg(timer_.elapsed()).arg(native->message, 0, 16).arg(quintptr(native->hwnd), 0, 16)
                .arg(native->pt.x).arg(native->pt.y).arg(native->time));
        return false;
    }
#endif
private:
    QPointer<QWidget> host_;
    QPointer<QToolButton> button_;
    QPointer<QToolButton> command_;
    QPointer<QMenu> menu_;
    QString path_;
    QElapsedTimer timer_;
    QStringList lines_;
#ifdef Q_OS_WIN
    HWND hostHandle_ = nullptr;
    HWND popupHandle_ = nullptr;
#endif
};

QList<QAction*> actions(QWidget* owner, int count)
{
    QList<QAction*> result;
    for (int index = 0; index < count; ++index) {
        auto* action = new QAction(iconFor("settings"), QStringLiteral("Action %1").arg(index), owner);
        action->setObjectName(QStringLiteral("command.%1").arg(index));
        result.append(action);
    }
    return result;
}

void mouse(QWidget* target, QEvent::Type type, QPoint global, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(target->mapFromGlobal(global)), QPointF(global), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &event);
    QCoreApplication::processEvents();
}

void startDrag(CommandStripHost& host, QWidget* handle, QPoint destination)
{
    const auto start = handle->mapToGlobal(handle->rect().center());
    mouse(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(handle, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    QVERIFY(host.isDragging());
}

// Real input through the target HWND (native popup capture and its outside
// release), refused unless this process owns the receiving window.
void nativeMouse(QWidget* target, QPoint local, bool click = false) { native_input::mouse(target, local, click); }
void nativeEscape(QMenu* menu) { native_input::key(menu, Qt::Key_Escape); }

int pixelsOfColor(const QImage& image, QColor expected)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        if (image.pixelColor(x, y).rgb() == expected.rgb()) ++count;
    return count;
}

} // namespace

class ChromeInteractionTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void narrowStripsKeepTheTailAndExposeOverflowActions();
    void hiddenActionsDoNotReserveButtonsOrOverflowEntries();
    void hiddenStripContentDoesNotReserveWidthOrOverflow();
    void visibilityCustomizationAndWidthsSurviveRestore();
    void restoringLockedMinimumWidthDoesNotUseTheCurrentUnlockedMinimum();
    void titleFirstFillsTheRowAndIsAWindowDragArea();
    void optionsPolicyDoesNotReserveSpaceOrChangeSavedLayout();
    void lockedEmptyOptionsDisappearAndReturnForOverflow();
    void disabledToolbarButtonsHaveNoBorderOrHoverFeedback();
    void oneElasticStripOwnsEachRowAndRestoresLegacyDefaults();
    void movingElasticStripClaimsTheTargetRowAndEscRestoresTheOwner();
    void elasticContentDoesNotBecomeAWindowDragTarget();
    void customizationUpdatesMenuAndActionGeometryImmediately();
    void overflowToggleRestoresHoverAndPressState();
    void outsidePopupDismissalRestoresNativeHoverAndPainting_data();
    void outsidePopupDismissalRestoresNativeHoverAndPainting();
    void toolbarDragImmediatelyMovesThePresentationAndCancelsCleanly();
    void slidingTheFollowingHandleCompressesThePreviousStrip();
    void slidingTheFollowingHandleRestoresPreviouslyCompressedButtons();
    void removingFollowingStripsRepairsTheVacatedBoundary_data();
    void removingFollowingStripsRepairsTheVacatedBoundary();
    void movingFollowingStripsRepairsLiveAndEscRestores_data();
    void movingFollowingStripsRepairsLiveAndEscRestores();
    void rightOfElasticStripsResizeAgainstAnAnchoredRightEdge_data();
    void rightOfElasticStripsResizeAgainstAnAnchoredRightEdge();
    void fixedWidthStripRightOfElasticOnlyReorders();
    void insertionRightOfElasticSpansToTheRightNeighbour();
    void removalRightOfElasticKeepsTheStripsBeforeInPlace();
    void stripConstraintsProtectContentThroughSizingAndRestore();
    void partialRestoreIgnoresUnavailableStripsButRejectsMalformedKnownRows();
    void extremeRequestedWidthsRemainBoundedAndRestorable();
    void resettingWidthsKeepsTheCorrectAnchorAndOtherRequests_data();
    void resettingWidthsKeepsTheCorrectAnchorAndOtherRequests();
    void resetWidthMenuUsesTheHostCommandCallback();
    void resettingHiddenStripsDoesNotChangeVisibleGeometry();
    void widthResetPrioritySurvivesRestoreAndDragCancellation();
    void narrowWidthResetsYieldOnTheOppositeSide_data();
    void narrowWidthResetsYieldOnTheOppositeSide();
    void resetBeforeElasticKeepsAnAlreadyCompressedPrefix();
    void rightBlockTracksTheWindowEdgeAndRecoversRequestedWidths_data();
    void rightBlockTracksTheWindowEdgeAndRecoversRequestedWidths();
    void contextMenuListsEveryStripAndOmitsUnrelatedWindowKinds();
    void stripCustomizationsSurviveWindowKindConversions();
};

void ChromeInteractionTest::initTestCase() { qApp->setQuitOnLastWindowClosed(false); }

void ChromeInteractionTest::lockedEmptyOptionsDisappearAndReturnForOverflow()
{
    CommandStripHost host; applyTheme(&host);
    const auto commands = actions(&host, 5);
    host.addActionStrip("tools", "Tools", commands, 0);
    host.resize(600, host.height()); host.show(); QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* strip = host.stripWidget("tools");
    auto* tail = strip->findChild<QToolButton*>("StripOverflowButton"); QVERIFY(tail && tail->isVisible());
    const auto layout = host.saveLayout();
    const int originalWidth = strip->width();
    host.setHideOptionsWhenLocked(true);
    QVERIFY(host.hideOptionsWhenLocked()); QVERIFY(tail->isHidden());
    QCOMPARE(strip->width(), originalWidth - 12); QCOMPARE(host.saveLayout(), layout);
    host.setLocked(false); QVERIFY(tail->isVisible());
    host.setLocked(true); QVERIFY(tail->isHidden());
    QVERIFY(host.setStripWidth("tools", 65));
    QVERIFY(tail->isVisible()); QVERIFY(tail->property("hasOverflow").toBool());
    QVERIFY(!host.overflowItems("tools").isEmpty());
    QVERIFY(host.resetStripWidth("tools"));
    QVERIFY(tail->isHidden()); QVERIFY(host.overflowItems("tools").isEmpty());
    host.setHideOptionsWhenLocked(false); QVERIFY(tail->isVisible());
    QCOMPARE(strip->width(), originalWidth);
    host.setHideOptionsWhenLocked(true);
    host.addActionStrip("later", "Later", actions(&host, 1), 1);
    QVERIFY(host.stripWidget("later")->findChild<QToolButton*>("StripOverflowButton")->isHidden());
}

void ChromeInteractionTest::disabledToolbarButtonsHaveNoBorderOrHoverFeedback()
{
    CommandStripHost host; applyTheme(&host);
    auto commands = actions(&host, 1); commands[0]->setEnabled(false);
    host.addActionStrip("tools", "Tools", commands, 0);
    host.resize(300, host.height()); host.show(); QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* button = host.findChild<QToolButton*>("StripCommandButton"); QVERIFY(button && !button->isEnabled());
    const QRect buttonArea(button->mapTo(&host, QPoint()), button->size());
    QVERIFY(host.rect().contains(buttonArea));
    // The button deliberately paints transparent background and border pixels.
    // Render its parent too, so these checks inspect the composed toolbar rather
    // than comparing a transparent child-only grab to an opaque header color.
    const auto composedButton = [&] {
        const auto image = host.grab().toImage(); const auto scale = image.devicePixelRatio();
        const int left = qRound(buttonArea.x() * scale);
        const int top = qRound(buttonArea.y() * scale);
        return image.copy(left, top, qRound((buttonArea.x() + buttonArea.width()) * scale) - left,
                          qRound((buttonArea.y() + buttonArea.height()) * scale) - top);
    };
    QTest::mouseMove(&host, QPoint(host.width() - 1, host.height() - 1));
    const auto before = composedButton();
    QVERIFY(!before.isNull());
    for (int x = 0; x < before.width(); ++x) {
        QCOMPARE(before.pixelColor(x, 0), defaultTheme().headerBackground);
        QCOMPARE(before.pixelColor(x, before.height() - 1), defaultTheme().headerBackground);
    }
    for (int y = 0; y < before.height(); ++y) {
        QCOMPARE(before.pixelColor(0, y), defaultTheme().headerBackground);
        QCOMPARE(before.pixelColor(before.width() - 1, y), defaultTheme().headerBackground);
    }
    QTest::mouseMove(button, button->rect().center());
    QCOMPARE(composedButton(), before);
    QSignalSpy invoked(commands[0], &QAction::triggered);
    QTest::mouseClick(button, Qt::LeftButton); QCOMPARE(invoked.count(), 0);
    const auto disabledIcon = button->icon().pixmap(button->iconSize(), QIcon::Disabled).toImage();
    const auto normalIcon = button->icon().pixmap(button->iconSize(), QIcon::Normal).toImage();
    QVERIFY(disabledIcon != normalIcon);
    QVERIFY(pixelsOfColor(disabledIcon, defaultTheme().mutedText) > 0);
    QVERIFY(pixelsOfColor(normalIcon, defaultTheme().text) > 0);
}

void ChromeInteractionTest::narrowStripsKeepTheTailAndExposeOverflowActions()
{
    CommandStripHost host;
    applyTheme(&host, defaultTheme());
    const auto commands = actions(&host, 8);
    host.addActionStrip("tools", "Tools", commands, 0);
    host.resize(110, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto overflow = host.overflowItems("tools");
    QVERIFY(!overflow.isEmpty());
    QCOMPARE(host.width(), 110);
    auto* strip = host.stripWidget("tools");
    auto* tail = strip->findChild<QWidget*>("StripOverflowButton");
    QVERIFY(tail); QVERIFY(tail->isVisible());
    QVERIFY(strip->rect().contains(tail->geometry()));
    QVERIFY(tail->property("hasOverflow").toBool());
    std::unique_ptr<QMenu> menu(host.createOverflowMenu("tools", &host));
    QVERIFY(menu);
    const auto id = overflow.first();
    const auto commandIterator = std::find_if(commands.begin(), commands.end(), [&id](QAction* action) { return action->objectName() == id; });
    QVERIFY(commandIterator != commands.end());
    auto* command = *commandIterator;
    QVERIFY(menu->actions().contains(command));
    QSignalSpy executed(command, &QAction::triggered); command->trigger(); QCOMPARE(executed.count(), 1);
    QVERIFY(menu->findChild<QMenu*>("StripCustomizeMenu"));
    host.resize(800, host.height()); QCoreApplication::processEvents();
    QVERIFY(host.overflowItems("tools").isEmpty());
    QVERIFY(tail->isVisible());
    QVERIFY(!tail->property("hasOverflow").toBool());
    std::unique_ptr<QMenu> withoutOverflow(host.createOverflowMenu("tools", &host));
    QCOMPARE(withoutOverflow->actions().size(), 3);
    QCOMPARE(withoutOverflow->actions()[0]->objectName(), QStringLiteral("StripResetWidthAction"));
    QCOMPARE(withoutOverflow->actions()[1]->objectName(), QStringLiteral("StripElasticAction"));
    QVERIFY(withoutOverflow->findChild<QAction*>("StripResetWidthAction"));
    QVERIFY(withoutOverflow->actions().last()->menu());
}

void ChromeInteractionTest::visibilityCustomizationAndWidthsSurviveRestore()
{
    CommandStripHost original;
    original.addActionStrip("tools", "Tools", actions(&original, 6), 0);
    original.addStrip("title", "Title", new QLabel("Workspace"), 0, CommandStripHost::StripSizing::Elastic);
    original.setLocked(false);
    QVERIFY(original.setStripItemVisible("tools", "command.1", false));
    QVERIFY(original.setStripWidth("tools", 90));
    QVERIFY(original.setStripVisible("title", false));
    QVERIFY(original.moveStrip("tools", 1, 0));
    const auto snapshot = original.saveLayout();
    CommandStripHost restored;
    restored.addActionStrip("tools", "Tools", actions(&restored, 6), 0);
    restored.addStrip("title", "Title", new QLabel("Workspace"), 0, CommandStripHost::StripSizing::Elastic);
    restored.restoreLayout(snapshot);
    QCOMPARE(restored.saveLayout(), snapshot);
    QVERIFY(!restored.isLocked());
    QVERIFY(!restored.isStripVisible("title"));
    QVERIFY(!restored.isStripItemVisible("tools", "command.1"));
    std::unique_ptr<QMenu> menu(restored.createOverflowMenu("tools", &restored));
    auto* customization = menu->findChild<QMenu*>("StripCustomizeMenu"); QVERIFY(customization);
    const auto customizedActions = customization->actions();
    const auto itemIterator = std::find_if(customizedActions.begin(), customizedActions.end(), [](QAction* action) {
        return action->data().toString() == "command.1";
    });
    QVERIFY(itemIterator != customizedActions.end());
    auto* item = *itemIterator;
    QVERIFY(item->isCheckable()); QVERIFY(!item->isChecked()); item->trigger();
    QVERIFY(restored.isStripItemVisible("tools", "command.1"));
}

void ChromeInteractionTest::hiddenActionsDoNotReserveButtonsOrOverflowEntries()
{
    CommandStripHost host;
    const auto commands = actions(&host, 5);
    host.addActionStrip("tools", "Tools", commands, 0);
    host.resize(80, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto id = commands.first()->objectName();
    auto* strip = host.stripWidget("tools");
    QToolButton* button = nullptr;
    for (auto* candidate : strip->findChildren<QToolButton*>()) if (candidate->property("commandId").toString() == id) button = candidate;
    QVERIFY(button);
    QVERIFY(button->isVisible());
    commands.first()->setVisible(false); host.refreshLayout();
    QVERIFY(!button->isVisible()); QVERIFY(!host.overflowItems("tools").contains(id));
    commands.first()->setVisible(true); host.refreshLayout();
    QVERIFY(button->isVisible());
    commands.last()->setVisible(false); host.refreshLayout();
    QVERIFY(!host.overflowItems("tools").contains(commands.last()->objectName()));
}

void ChromeInteractionTest::hiddenStripContentDoesNotReserveWidthOrOverflow()
{
    CommandStripHost host;
    host.addStrip("content", "Content", new QLabel("A long custom toolbar content widget"), 0);
    host.resize(650, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* strip = host.stripWidget("content");
    auto* tail = strip->findChild<QToolButton*>("StripOverflowButton");
    QVERIFY(tail);
    const int originalWidth = strip->width();
    QVERIFY(host.setStripItemVisible("content", "content", false));
    QVERIFY(strip->width() < originalWidth);
    QVERIFY(host.overflowItems("content").isEmpty());
    QVERIFY(!tail->property("hasOverflow").toBool());
    QVERIFY(tail->isVisible());
    QVERIFY(host.setStripItemVisible("content", "content", true));
    QCOMPARE(strip->width(), originalWidth);
    QVERIFY(host.setStripWidth("content", 24));
    QCOMPARE(host.overflowItems("content"), QStringList({"content"}));
    QVERIFY(tail->property("hasOverflow").toBool());
    QVERIFY(host.setStripItemVisible("content", "content", false));
    QVERIFY(host.overflowItems("content").isEmpty());
    QVERIFY(!tail->property("hasOverflow").toBool());
    std::unique_ptr<QMenu> menu(host.createOverflowMenu("content", &host));
    QCOMPARE(menu->actions().size(), 3);
    QCOMPARE(menu->actions()[0]->objectName(), QStringLiteral("StripResetWidthAction"));
    QCOMPARE(menu->actions()[1]->objectName(), QStringLiteral("StripElasticAction"));
    QVERIFY(menu->findChild<QAction*>("StripResetWidthAction"));
    QVERIFY(menu->actions().last()->menu());
    auto* customize = menu->findChild<QMenu*>("StripCustomizeMenu"); QVERIFY(customize);
    QAction* contentItem = nullptr;
    for (auto* action : customize->actions()) if (action->data().toString() == "content") contentItem = action;
    QVERIFY(contentItem); QVERIFY(!contentItem->isChecked());
    contentItem->trigger();
    QCOMPARE(host.overflowItems("content"), QStringList({"content"}));
    QVERIFY(tail->property("hasOverflow").toBool());
}

void ChromeInteractionTest::titleFirstFillsTheRowAndIsAWindowDragArea()
{
    CommandStripHost host;
    auto* title = new QLabel("Workspace");
    title->setProperty("windowDragArea", true);
    host.addStrip("title", "Title", title, 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("tools", "Tools", actions(&host, 2), 0);
    host.resize(640, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto titleWidth = host.stripWidget("title")->width();
    QVERIFY(titleWidth > 400);
    QVERIFY(host.windowDragAreas().contains(title));
    QCOMPARE(title->alignment() & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
    QVERIFY(host.moveStrip("title", 0, 1));
    // Elastic sizing supersedes the former "only when first" condition.
    QCOMPARE(host.stripWidget("title")->width(), titleWidth);
    QCOMPARE(host.stripWidget("tools")->geometry().left(), 0); // No outer row gutter.
    QCOMPARE(host.stripWidget("title")->geometry().right(), host.width() - 1); // No outer row gutter.
    host.setLocked(false);
    auto* handle = host.stripWidget("title")->findChild<QWidget*>("StripHandle");
    QVERIFY(handle); QCOMPARE(handle->cursor().shape(), Qt::SizeAllCursor);
    QVERIFY(!qobject_cast<QToolButton*>(handle));
}

void ChromeInteractionTest::oneElasticStripOwnsEachRowAndRestoresLegacyDefaults()
{
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 2), 0);
    host.addStrip("elastic", "Elastic", new QLabel("Workspace"), 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("after", "After", actions(&host, 3), 0);
    host.resize(850, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const int beforeWidth = host.stripWidget("before")->width();
    const int afterWidth = host.stripWidget("after")->width();
    const int elasticWidth = host.stripWidget("elastic")->width();
    QCOMPARE(host.stripWidget("after")->geometry().right(), host.width() - 1); // No outer row gutter.
    host.resize(1000, host.height());
    QCOMPARE(host.stripWidget("before")->width(), beforeWidth);
    QCOMPARE(host.stripWidget("after")->width(), afterWidth);
    QCOMPARE(host.stripWidget("elastic")->width(), elasticWidth + 150);
    QCOMPARE(host.stripWidget("after")->geometry().right(), host.width() - 1); // No outer row gutter.
    QVERIFY(host.setStripSizing("before", CommandStripHost::StripSizing::Elastic));
    QCOMPARE(host.stripSizing("before"), CommandStripHost::StripSizing::Elastic);
    QCOMPARE(host.stripSizing("elastic"), CommandStripHost::StripSizing::Content);
    QVERIFY(host.stripWidget("before")->width() > host.stripWidget("elastic")->width());
    QCOMPARE(host.stripWidget("after")->width(), afterWidth);
    QCOMPARE(host.stripWidget("after")->geometry().right(), host.width() - 1); // No outer row gutter.
    const auto snapshot = host.saveLayout();
    auto legacy = snapshot;
    auto settings = legacy.value("strips").toObject();
    for (const auto& id : host.stripIds()) {
        auto entry = settings.value(id).toObject(); entry.remove("sizing"); settings.insert(id, entry);
    }
    legacy.insert("strips", settings);
    CommandStripHost restored;
    restored.addActionStrip("before", "Before", actions(&restored, 2), 0);
    restored.addStrip("elastic", "Elastic", new QLabel("Workspace"), 0, CommandStripHost::StripSizing::Elastic);
    restored.addActionStrip("after", "After", actions(&restored, 3), 0);
    restored.restoreLayout(legacy);
    QCOMPARE(restored.stripSizing("before"), CommandStripHost::StripSizing::Content);
    QCOMPARE(restored.stripSizing("elastic"), CommandStripHost::StripSizing::Elastic);
    restored.restoreLayout(snapshot);
    QCOMPARE(restored.saveLayout(), snapshot);
    restored.addStrip("second-row", "Second row", new QLabel("Second"), 1, CommandStripHost::StripSizing::Elastic);
    QVERIFY(restored.moveStrip("before", 1, 0));
    QCOMPARE(restored.stripSizing("before"), CommandStripHost::StripSizing::Elastic);
    QCOMPARE(restored.stripSizing("second-row"), CommandStripHost::StripSizing::Content);
    auto multipleElastic = snapshot;
    auto multipleSettings = multipleElastic.value("strips").toObject();
    auto elasticEntry = multipleSettings.value("elastic").toObject();
    elasticEntry.insert("sizing", "elastic"); multipleSettings.insert("elastic", elasticEntry);
    multipleElastic.insert("strips", multipleSettings);
    restored.restoreLayout(multipleElastic);
    QCOMPARE(restored.stripSizing("before"), CommandStripHost::StripSizing::Content);
    QCOMPARE(restored.stripSizing("elastic"), CommandStripHost::StripSizing::Elastic);
    const auto normalized = restored.saveLayout().value("strips").toObject();
    QCOMPARE(normalized.value("before").toObject().value("sizing").toString(), QString("content"));
    QCOMPARE(normalized.value("elastic").toObject().value("sizing").toString(), QString("elastic"));
    std::unique_ptr<QMenu> normalizedMenu(restored.createOverflowMenu("before", &restored));
    auto* elasticAction = normalizedMenu->findChild<QAction*>("StripElasticAction");
    QVERIFY(elasticAction); QVERIFY(!elasticAction->isChecked());
}

void ChromeInteractionTest::elasticContentDoesNotBecomeAWindowDragTarget()
{
    CommandStripHost host;
    auto* interactive = new QToolButton;
    interactive->setText("Interactive content");
    auto* caption = new QLabel("Workspace");
    caption->setProperty("windowDragArea", true);
    host.addStrip("interactive", "Interactive", interactive, 0, CommandStripHost::StripSizing::Elastic);
    host.addStrip("caption", "Caption", caption, 0, CommandStripHost::StripSizing::Elastic);
    QVERIFY(!host.windowDragAreas().contains(interactive));
    QVERIFY(host.windowDragAreas().contains(caption));
    QVERIFY(host.setStripSizing("caption", CommandStripHost::StripSizing::Content));
    QVERIFY(host.windowDragAreas().contains(caption));
    QVERIFY(host.setStripSizing("interactive", CommandStripHost::StripSizing::Content));
    QVERIFY(!host.windowDragAreas().contains(interactive));
}

void ChromeInteractionTest::movingElasticStripClaimsTheTargetRowAndEscRestoresTheOwner()
{
    CommandStripHost host;
    host.addStrip("source", "Source", new QLabel("Source"), 0, CommandStripHost::StripSizing::Elastic);
    host.addStrip("target", "Target", new QLabel("Target"), 1, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(600, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto snapshot = host.saveLayout();
    auto* handle = host.stripWidget("source")->findChild<QWidget*>("StripHandle");
    const auto destination = host.mapToGlobal(QPoint(100, 48));
    startDrag(host, handle, destination);
    QCOMPARE(host.stripSizing("source"), CommandStripHost::StripSizing::Elastic);
    QCOMPARE(host.stripSizing("target"), CommandStripHost::StripSizing::Content);
    QCOMPARE(host.saveLayout(), snapshot);
    QTest::keyClick(&host, Qt::Key_Escape);
    QCOMPARE(host.saveLayout(), snapshot);
    QCOMPARE(host.stripSizing("target"), CommandStripHost::StripSizing::Elastic);
    startDrag(host, handle, destination);
    mouse(&host, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(host.stripOrder(1), QStringList({"target", "source"}));
    QCOMPARE(host.stripSizing("target"), CommandStripHost::StripSizing::Content);
    QCOMPARE(host.stripSizing("source"), CommandStripHost::StripSizing::Elastic);
    QCOMPARE(host.stripWidget("source")->geometry().right(), host.width() - 1); // No outer row gutter.
}

void ChromeInteractionTest::customizationUpdatesMenuAndActionGeometryImmediately()
{
    CommandStripHost host;
    auto* menuBar = new QMenuBar;
    menuBar->setNativeMenuBar(false);
    for (int index = 0; index < 4; ++index) {
        auto* action = menuBar->addMenu(QStringLiteral("Category %1").arg(index))->menuAction();
        action->setObjectName(QStringLiteral("category.%1").arg(index));
    }
    host.addStrip("menu", "Menu", menuBar, 0);
    const auto commands = actions(&host, 3);
    host.addActionStrip("tools", "Tools", commands, 0);
    host.resize(900, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const int originalWidth = host.stripWidget("menu")->width();
    std::unique_ptr<QMenu> menu(host.createOverflowMenu("menu", &host));
    auto* customized = menu->findChild<QMenu*>("StripCustomizeMenu"); QVERIFY(customized);
    QAction* item = nullptr;
    for (auto* action : customized->actions()) if (action->data().toString() == "category.0") item = action;
    QVERIFY(item); item->trigger();
    QVERIFY(!menuBar->actions().first()->isVisible());
    QVERIFY(host.stripWidget("menu")->width() < originalWidth);
    item->trigger(); QCOMPARE(host.stripWidget("menu")->width(), originalWidth);
    auto* strip = host.stripWidget("tools");
    const int toolWidth = strip->width();
    QVERIFY(host.setStripItemVisible("tools", "command.0", false));
    QVERIFY(strip->width() < toolWidth);
    for (auto* button : strip->findChildren<QToolButton*>()) {
        if (button->property("commandId").toString() == "command.0") QVERIFY(button->isHidden());
    }
    commands[1]->setVisible(false);
    QVERIFY(!host.overflowItems("tools").contains("command.1"));
    QVERIFY(strip->width() < toolWidth);
}

void ChromeInteractionTest::overflowToggleRestoresHoverAndPressState()
{
    CommandStripHost host;
    host.setWindowFlag(Qt::WindowStaysOnTopHint);
    host.move(150, 150);
    applyTheme(&host, defaultTheme());
    host.addActionStrip("tools", "Tools", actions(&host, 3), 0);
    host.resize(400, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    host.raise(); host.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&host));
    auto* tail = host.stripWidget("tools")->findChild<QToolButton*>("StripOverflowButton");
    QVERIFY(tail); QVERIFY(tail->menu());
    PopupEventTrace trace(&host, tail);
    bool visibleBeforeSecondClick = false;
    bool watchdogFired = false;
    QSignalSpy shown(tail->menu(), &QMenu::aboutToShow);
    // Show is delivered before the native popup is exposed. On a cold first
    // show, platform effects/color-profile work can block both timers, letting
    // the old watchdog close the menu before its closing click is delivered.
    // Establish the real interaction preconditions first; the closing-click
    // delay and its watchdog remain exactly 30 and 300 ms.
    nativeMouse(tail, tail->rect().center(), true);
    trace.note("first-input-queued");
    QTRY_VERIFY(tail->menu()->isVisible());
    QTRY_VERIFY(QApplication::activePopupWidget() == tail->menu());
    QVERIFY(QTest::qWaitForWindowExposed(tail->menu()));
    QTRY_COMPARE(QApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
    trace.note("popup-ready");
    QTimer::singleShot(30, &host, [&] {
        trace.note("second-click-timer");
        visibleBeforeSecondClick = tail->menu()->isVisible();
        nativeMouse(tail, tail->rect().center(), true);
    });
    // Watchdog closes only after recording the expected toggle; it prevents a
    // broken nested popup from hanging the test runner.
    QTimer::singleShot(300, &host, [tail, &watchdogFired, &trace] {
        trace.note("watchdog-timer");
        if (tail->menu() && tail->menu()->isVisible()) { watchdogFired = true; tail->menu()->close(); }
    });
    QTRY_VERIFY(visibleBeforeSecondClick);
    // SendInput queues the closing click as well. Observe its result before
    // checking the watchdog, so forced dismissal can never count as success.
    QTRY_VERIFY(!tail->menu()->isVisible());
    QCOMPARE(shown.count(), 1);
    QVERIFY(!watchdogFired);
    QVERIFY(!tail->isDown());
    QVERIFY(!QApplication::activePopupWidget());
    QTRY_COMPARE(QApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
    auto* button = host.stripWidget("tools")->findChild<QToolButton*>("StripCommandButton");
    QVERIFY(button);
    nativeMouse(button, button->rect().center());
    trace.note("command-move-queued");
    QCoreApplication::processEvents();
    QTRY_VERIFY(button->underMouse());
    QTRY_VERIFY(!tail->underMouse());
}

void ChromeInteractionTest::outsidePopupDismissalRestoresNativeHoverAndPainting_data()
{
    QTest::addColumn<bool>("workbench");
    QTest::addColumn<QString>("closeTarget");
    QTest::newRow("standalone-blank") << false << QString("blank");
    QTest::newRow("workbench-title") << true << QString("blank");
    QTest::newRow("workbench-content") << true << QString("content");
    QTest::newRow("workbench-other-button") << true << QString("button");
    QTest::newRow("workbench-escape") << true << QString("escape");
    QTest::newRow("standalone-command") << false << QString("command");
    QTest::newRow("workbench-submenu-outside") << true << QString("submenu");
}

void ChromeInteractionTest::outsidePopupDismissalRestoresNativeHoverAndPainting()
{
    QFETCH(bool, workbench);
    QFETCH(QString, closeTarget);
    WindowManager manager;
    std::unique_ptr<CommandStripHost> standalone;
    QWidget* window = nullptr;
    QWidget* content = nullptr;
    CommandStripHost* host = nullptr;
    QString stripId;
    const auto theme = defaultTheme();
    if (workbench) {
        const auto id = manager.createWindow();
        auto* workspace = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(id));
        QVERIFY(workspace);
        content = new QLabel("Content outside the menu");
        QVERIFY(!manager.addTab(id, content, {"Content", "test.label", {}}).isNull());
        window = workspace; host = workspace->strips(); stripId = "workspace";
        window->resize(750, 460);
    } else {
        standalone = std::make_unique<CommandStripHost>();
        host = standalone.get(); window = host; stripId = "tools";
        applyTheme(host, theme);
        host->addActionStrip(stripId, "Tools", actions(host, 6), 0);
        if (closeTarget == "command") QVERIFY(host->setStripWidth(stripId, 70));
        host->resize(600, host->height());
    }
    window->setWindowFlag(Qt::WindowStaysOnTopHint);
    window->move(150, 150); window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->raise(); window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(window));
    auto* tail = host->stripWidget(stripId)->findChild<QToolButton*>("StripOverflowButton");
    auto* button = host->stripWidget(stripId)->findChild<QToolButton*>("StripCommandButton");
    QVERIFY(tail); QVERIFY(tail->menu()); QVERIFY(button);
    QPoint blank(host->width() - 30, host->height() / 2);
    if (workbench) {
        auto* caption = window->findChild<QLabel*>("CaptionTitle");
        QVERIFY(caption); QVERIFY(caption->rect().contains(QPoint(50, 16)));
        blank = host->mapFromGlobal(caption->mapToGlobal(QPoint(50, 16)));
    }
    nativeMouse(host, blank); QTest::qWait(30);
    QTRY_VERIFY(!button->underMouse());
    QVERIFY(!button->isDown()); QVERIFY(!QApplication::activePopupWidget());
    const int baseline = pixelsOfColor(button->grab().toImage(), theme.buttonHover);
    QSignalSpy buttonTriggered(button->defaultAction(), &QAction::triggered);
    QSignalSpy menuTriggered(tail->menu(), &QMenu::triggered);
    bool visibleBeforeClose = false;
    bool submenuBeforeClose = false;
    bool watchdogFired = false;
    QTimer::singleShot(100, host, [&] {
        visibleBeforeClose = tail->menu()->isVisible();
        if (closeTarget == "content") nativeMouse(content, content->rect().center(), true);
        else if (closeTarget == "button") nativeMouse(button, button->rect().center(), true);
        else if (closeTarget == "escape") nativeEscape(tail->menu());
        else if (closeTarget == "command") {
            nativeMouse(tail->menu(), tail->menu()->actionGeometry(tail->menu()->actions().first()).center(), true);
        } else if (closeTarget == "submenu") {
            auto* customize = tail->menu()->findChild<QMenu*>("StripCustomizeMenu");
            QVERIFY(customize);
            nativeMouse(tail->menu(), tail->menu()->actionGeometry(customize->menuAction()).center(), true);
            QTimer::singleShot(350, host, [&, customize] {
                submenuBeforeClose = customize->isVisible();
                nativeMouse(host, blank, true);
            });
        } else nativeMouse(host, blank, true);
    });
    QTimer::singleShot(900, host, [&] {
        if (tail->menu()->isVisible()) { watchdogFired = true; tail->menu()->close(); }
    });
    nativeMouse(tail, tail->rect().center(), true);
    QTRY_VERIFY(visibleBeforeClose);
    QVERIFY(!watchdogFired);
    QTRY_VERIFY(!tail->menu()->isVisible());
    if (closeTarget == "submenu") QVERIFY(submenuBeforeClose);
    if (closeTarget == "command") QCOMPARE(menuTriggered.count(), 1);
    else QCOMPARE(buttonTriggered.count(), 0);
    QVERIFY(!QApplication::activePopupWidget());
    QVERIFY(!QWidget::mouseGrabber());
    QVERIFY(!tail->isDown());
    QTRY_COMPARE(QApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
    nativeMouse(button, button->rect().center());
    QTRY_VERIFY(button->underMouse());
    QTRY_VERIFY(pixelsOfColor(button->grab().toImage(), theme.buttonHover) > baseline + 15);
    nativeMouse(host, blank); QTest::qWait(30);
    QTRY_VERIFY(!button->underMouse());
    QCOMPARE(pixelsOfColor(button->grab().toImage(), theme.buttonHover), baseline);
}

void ChromeInteractionTest::optionsPolicyDoesNotReserveSpaceOrChangeSavedLayout()
{
    CommandStripHost host;
    auto* content = new QLabel("Workspace");
    content->setProperty("windowDragArea", true);
    host.addStrip("title", "Title", content, 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("tools", "Tools", actions(&host, 3), 0);
    host.setLocked(false); host.resize(650, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto layout = host.saveLayout();
    auto* strip = host.stripWidget("title");
    auto* tail = strip->findChild<QToolButton*>("StripOverflowButton");
    auto* viewport = strip->findChild<QWidget*>("StripViewport");
    QVERIFY(tail); QVERIFY(viewport); QVERIFY(tail->isVisible());
    const int oldViewportWidth = viewport->width();
    QVERIFY(host.setStripOptionsEnabled("title", false));
    QVERIFY(!host.isStripOptionsEnabled("title"));
    QVERIFY(host.isStripOptionsEnabled("tools"));
    QVERIFY(tail->isHidden());
    QCOMPARE(viewport->geometry().right(), strip->rect().right());
    QCOMPARE(viewport->width(), oldViewportWidth + 12);
    QVERIFY(!host.createOverflowMenu("title", &host));
    QCOMPARE(host.saveLayout(), layout);
    QVERIFY(host.windowDragAreas().contains(content));
    host.restoreLayout(layout);
    QVERIFY(tail->isHidden());
    QCOMPARE(viewport->geometry().right(), strip->rect().right());
    QVERIFY(host.setStripOptionsEnabled("title", true));
    QVERIFY(tail->isVisible());
    QCOMPARE(viewport->width(), oldViewportWidth);
    WindowManager manager;
    const auto id = manager.createWindow();
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(window); QVERIFY(window->strips());
    QVERIFY(!window->strips()->isStripOptionsEnabled("title"));
    QVERIFY(window->strips()->isStripOptionsEnabled("menu"));
    QVERIFY(window->strips()->isStripOptionsEnabled("workspace"));
    auto* titleTail = window->strips()->stripWidget("title")->findChild<QToolButton*>("StripOverflowButton");
    QVERIFY(titleTail); QVERIFY(titleTail->isHidden());
}

void ChromeInteractionTest::restoringLockedMinimumWidthDoesNotUseTheCurrentUnlockedMinimum()
{
    CommandStripHost original;
    original.addActionStrip("tools", "Tools", actions(&original, 4), 0);
    QVERIFY(original.isLocked()); QVERIFY(original.setStripWidth("tools", 24));
    QVERIFY(original.moveStrip("tools", 1, 0));
    const auto snapshot = original.saveLayout();
    CommandStripHost restored;
    restored.addActionStrip("tools", "Tools", actions(&restored, 4), 0);
    restored.setLocked(false);
    restored.restoreLayout(snapshot);
    QVERIFY(restored.isLocked()); QCOMPARE(restored.saveLayout(), snapshot);
}

void ChromeInteractionTest::toolbarDragImmediatelyMovesThePresentationAndCancelsCleanly()
{
    CommandStripHost host;
    host.addActionStrip("tools", "Tools", actions(&host, 6), 0);
    host.addStrip("title", "Title", new QLabel("Workspace"), 1, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(650, host.height()); host.move(150, 150); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto snapshot = host.saveLayout();
    auto* strip = host.stripWidget("tools");
    auto* handle = strip->findChild<QWidget*>("StripHandle");
    const auto destination = host.mapToGlobal(QPoint(100, 48));
    startDrag(host, handle, destination);
    QVERIFY(!strip->isVisible());
    auto* preview = host.findChild<QLabel*>("StripDragPreview");
    QVERIFY(preview); QVERIFY(preview->isVisible()); QVERIFY(preview->isWindow());
    QCOMPARE(host.saveLayout(), snapshot);
    const auto firstPosition = preview->pos();
    mouse(&host, QEvent::MouseMove, destination + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
    QCOMPARE(preview->pos(), firstPosition + QPoint(40, 0));
    QPointer<QLabel> observed = preview;
    QTest::keyClick(&host, Qt::Key_Escape);
    QVERIFY(!host.isDragging()); QVERIFY(!observed);
    QVERIFY(strip->isVisible()); QCOMPARE(host.saveLayout(), snapshot);
}

void ChromeInteractionTest::slidingTheFollowingHandleCompressesThePreviousStrip()
{
    CommandStripHost host;
    host.addActionStrip("tools", "Tools", actions(&host, 12), 0);
    host.addStrip("title", "Title", new QLabel("Workspace"), 0, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(800, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* first = host.stripWidget("tools");
    const int originalWidth = first->width();
    auto* handle = host.stripWidget("title")->findChild<QWidget*>("StripHandle");
    const auto destination = host.mapToGlobal(QPoint(100, 12));
    startDrag(host, handle, destination);
    mouse(&host, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!host.isDragging());
    QCOMPARE(host.stripOrder(0), QStringList({"tools", "title"}));
    QVERIFY(first->width() < originalWidth);
    QVERIFY(!host.overflowItems("tools").isEmpty());
    const int firstRight = first->geometry().right();
    QVERIFY(host.stripWidget("title")->geometry().left() > firstRight);
}

void ChromeInteractionTest::slidingTheFollowingHandleRestoresPreviouslyCompressedButtons()
{
    CommandStripHost host;
    host.addActionStrip("tools", "Tools", actions(&host, 10), 0);
    host.addStrip("title", "Title", new QLabel("Workspace"), 0, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(900, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* first = host.stripWidget("tools");
    const int originalWidth = first->width();
    auto* handle = host.stripWidget("title")->findChild<QWidget*>("StripHandle");
    const auto compressed = host.mapToGlobal(QPoint(95, 12));
    startDrag(host, handle, compressed);
    mouse(&host, QEvent::MouseButtonRelease, compressed, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!host.isDragging()); QVERIFY(first->width() < originalWidth);
    QVERIFY(!host.overflowItems("tools").isEmpty());
    const auto expanded = host.mapToGlobal(QPoint(originalWidth + 35, 12));
    startDrag(host, handle, expanded);
    mouse(&host, QEvent::MouseButtonRelease, expanded, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!host.isDragging());
    QCOMPARE(host.stripOrder(0), QStringList({"tools", "title"}));
    QVERIFY(first->width() >= originalWidth);
    QVERIFY(host.overflowItems("tools").isEmpty());
    const auto snapshot = host.saveLayout();
    startDrag(host, handle, compressed);
    QTest::keyClick(&host, Qt::Key_Escape);
    QCOMPARE(host.saveLayout(), snapshot);
    QVERIFY(host.overflowItems("tools").isEmpty());
}

void ChromeInteractionTest::removingFollowingStripsRepairsTheVacatedBoundary_data()
{
    QTest::addColumn<bool>("compressed");
    QTest::addColumn<QString>("nextKind");
    for (bool compressed : {false, true}) for (const auto& next : {"none", "elastic", "content"})
        QTest::newRow(qPrintable(QString("%1-%2").arg(compressed ? "compressed" : "stretched", next)))
            << compressed << QString(next);
}

void ChromeInteractionTest::removingFollowingStripsRepairsTheVacatedBoundary()
{
    QFETCH(bool, compressed);
    QFETCH(QString, nextKind);
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 6), 0);
    host.addActionStrip("moved", "Moved", actions(&host, 3), 0);
    if (nextKind == "elastic") host.addStrip("next", "Next", new QLabel("Elastic"), 0, CommandStripHost::StripSizing::Elastic);
    if (nextKind == "content") host.addActionStrip("next", "Next", actions(&host, 3), 0);
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* before = host.stripWidget("before");
    const int natural = before->width();
    QVERIFY(host.setStripWidth("before", compressed ? 45 : natural + 90));
    const int previousWidth = before->width();
    const int vacatedWidth = host.stripWidget("moved")->width();
    const int followingLeft = nextKind == "none" ? -1 : host.stripWidget("next")->x();
    const auto snapshot = host.saveLayout();
    QVERIFY(host.moveStrip("moved", 1, 0));
    QCOMPARE(before->width(), nextKind == "content" ? previousWidth + vacatedWidth + 4 : natural);
    if (nextKind == "content") QCOMPARE(host.stripWidget("next")->x(), followingLeft);
    if (nextKind == "elastic") QCOMPARE(host.stripWidget("next")->geometry().right(), host.width() - 1); // No outer row gutter.
    QCOMPARE(host.stripOrder(1), QStringList({"moved"}));
    host.restoreLayout(snapshot);
    QCOMPARE(host.saveLayout(), snapshot);
    QCOMPARE(before->width(), previousWidth);
}

void ChromeInteractionTest::movingFollowingStripsRepairsLiveAndEscRestores_data()
{
    QTest::addColumn<QString>("nextKind");
    QTest::newRow("empty-after") << QString("none");
    QTest::newRow("elastic-after") << QString("elastic");
    QTest::newRow("ordinary-after") << QString("content");
}

void ChromeInteractionTest::movingFollowingStripsRepairsLiveAndEscRestores()
{
    QFETCH(QString, nextKind);
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 6), 0);
    host.addActionStrip("moved", "Moved", actions(&host, 3), 0);
    if (nextKind == "elastic") host.addStrip("next", "Next", new QLabel("Elastic"), 0, CommandStripHost::StripSizing::Elastic);
    if (nextKind == "content") host.addActionStrip("next", "Next", actions(&host, 3), 0);
    host.addStrip("other-row", "Other row", new QLabel("Other row"), 1, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* before = host.stripWidget("before");
    const int natural = before->width();
    QVERIFY(host.setStripWidth("before", 45));
    const int removedWidth = host.stripWidget("moved")->width();
    const int followingLeft = nextKind == "none" ? -1 : host.stripWidget("next")->x();
    const auto snapshot = host.saveLayout();
    auto* handle = host.stripWidget("moved")->findChild<QWidget*>("StripHandle");
    const auto destination = host.mapToGlobal(QPoint(200, 48));
    startDrag(host, handle, destination);
    QCOMPARE(before->width(), nextKind == "content" ? 45 + removedWidth + 4 : natural);
    if (nextKind == "content") QCOMPARE(host.stripWidget("next")->x(), followingLeft);
    QVERIFY(!host.stripWidget("moved")->isVisible());
    QCOMPARE(host.saveLayout(), snapshot);
    QTest::keyClick(&host, Qt::Key_Escape);
    QCOMPARE(host.saveLayout(), snapshot);
    QCOMPARE(before->width(), 45);
    QVERIFY(host.stripWidget("moved")->isVisible());
}

void ChromeInteractionTest::rightOfElasticStripsResizeAgainstAnAnchoredRightEdge_data()
{
    QTest::addColumn<bool>("ordinaryBefore");
    QTest::addColumn<bool>("ordinaryAfter");
    for (bool before : {false, true}) for (bool after : {false, true})
        QTest::newRow(qPrintable(QString("%1-%2").arg(before ? "ordinary-before" : "elastic-before", after ? "fixed-next" : "window-edge")))
            << before << after;
}

void ChromeInteractionTest::rightOfElasticStripsResizeAgainstAnAnchoredRightEdge()
{
    QFETCH(bool, ordinaryBefore);
    QFETCH(bool, ordinaryAfter);
    CommandStripHost host;
    host.addStrip("elastic", "Elastic", new QLabel("Workspace"), 0, CommandStripHost::StripSizing::Elastic);
    if (ordinaryBefore) host.addActionStrip("before", "Before", actions(&host, 6), 0);
    host.addActionStrip("resized", "Resized", actions(&host, 6), 0);
    if (ordinaryAfter) host.addActionStrip("after", "After", actions(&host, 3), 0);
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* resized = host.stripWidget("resized");
    auto* before = host.stripWidget(ordinaryBefore ? "before" : "elastic");
    auto* elastic = host.stripWidget("elastic");
    const int originalWidth = resized->width();
    const int originalRight = resized->geometry().right();
    const QRect followingBounds = ordinaryAfter ? host.stripWidget("after")->geometry() : QRect{};
    const QRect beforeBounds = before->geometry();
    const int elasticWidth = elastic->width();
    // The boundary at the handle moves; the dragged strip is on the anchored
    // (right) side, so it alone changes width. An ordinary strip before it
    // keeps its width and moves with the boundary; the elastic strip absorbs.
    const auto checkBefore = [&](int shift) {
        QCOMPARE(elastic->width(), elasticWidth + shift);
        if (ordinaryBefore) QCOMPARE(before->geometry(), beforeBounds.translated(shift, 0));
    };
    auto* handle = resized->findChild<QWidget*>("StripHandle");
    const int handleOffset = handle->rect().center().x();
    auto destination = host.mapToGlobal(QPoint(resized->x() + handleOffset + 50, 12));
    startDrag(host, handle, destination);
    QCOMPARE(resized->width(), originalWidth - 50);
    QCOMPARE(resized->geometry().right(), originalRight);
    checkBefore(50);
    if (ordinaryAfter) QCOMPARE(host.stripWidget("after")->geometry(), followingBounds);
    QVERIFY(!resized->isVisible());
    mouse(&host, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(resized->width(), originalWidth - 50);
    QCOMPARE(resized->geometry().right(), originalRight);
    destination = host.mapToGlobal(QPoint(resized->x() + handleOffset - 75, 12));
    startDrag(host, handle, destination);
    QCOMPARE(resized->width(), originalWidth + 25);
    QCOMPARE(resized->geometry().right(), originalRight);
    checkBefore(-25);
    if (ordinaryAfter) QCOMPARE(host.stripWidget("after")->geometry(), followingBounds);
    mouse(&host, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    const auto stretched = host.saveLayout();
    const auto stretchedGeometry = resized->geometry();
    destination = host.mapToGlobal(QPoint(resized->x() + handleOffset + 45, 12));
    startDrag(host, handle, destination);
    QTest::keyClick(&host, Qt::Key_Escape);
    QCOMPARE(host.saveLayout(), stretched);
    QCOMPARE(resized->geometry(), stretchedGeometry);
    if (ordinaryAfter) QCOMPARE(host.stripWidget("after")->geometry(), followingBounds);
}

void ChromeInteractionTest::fixedWidthStripRightOfElasticOnlyReorders()
{
    // A fixed-width strip on the anchored side of its handle cannot change
    // width, so dragging its handle moves no boundary: no other strip may
    // shrink or grow while it stays put.
    CommandStripHost host;
    host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("before", "Before", actions(&host, 6), 0);
    host.addActionStrip("fixed", "Fixed", actions(&host, 3), 0);
    QVERIFY(host.setStripConstraints("fixed", {false, false, 12, false, true}));
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* fixed = host.stripWidget("fixed");
    auto* before = host.stripWidget("before");
    auto* elastic = host.stripWidget("elastic");
    const QRect fixedBounds = fixed->geometry(), beforeBounds = before->geometry(), elasticBounds = elastic->geometry();
    const auto layout = host.saveLayout();
    auto* handle = fixed->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    const int offset = handle->rect().center().x();
    for (const int shift : {-60, -20, 25}) {
        const auto destination = host.mapToGlobal(QPoint(fixedBounds.x() + offset + shift, 12));
        startDrag(host, handle, destination);
        QCOMPARE(before->geometry(), beforeBounds);
        QCOMPARE(elastic->geometry(), elasticBounds);
        mouse(&host, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(fixed->geometry(), fixedBounds);
        QCOMPARE(before->geometry(), beforeBounds);
        QCOMPARE(host.saveLayout().value("strips"), layout.value("strips"));
    }
}

void ChromeInteractionTest::insertionRightOfElasticSpansToTheRightNeighbour()
{
    // Dropped right of the elastic strip, the inserted strip starts at the
    // pointer and reaches its right neighbour; the strip before it keeps its
    // width and the strip after it stays where it is.
    CommandStripHost host;
    host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("before", "Before", actions(&host, 6), 0);
    host.addActionStrip("after", "After", actions(&host, 3), 0);
    host.addStrip("other-row", "Other row", new QLabel("Other row"), 1, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("moved", "Moved", actions(&host, 2), 1);
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* before = host.stripWidget("before");
    auto* after = host.stripWidget("after");
    auto* moved = host.stripWidget("moved");
    const int beforeWidth = before->width();
    const QRect afterBounds = after->geometry();
    auto* handle = moved->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    const int offset = handle->rect().center().x();
    for (const int inset : {before->width() / 2 + 10, before->width() / 2 + 30}) {
        const int pointer = before->geometry().left() + inset;
        const auto destination = host.mapToGlobal(QPoint(pointer, 12));
        startDrag(host, handle, destination);
        QCOMPARE(host.stripOrder(0), (QStringList{"elastic", "before", "moved", "after"}));
        QCOMPARE(moved->x(), pointer - offset);
        QCOMPARE(moved->geometry().right() + 1 + 4, afterBounds.left());
        QCOMPARE(after->geometry(), afterBounds);
        QCOMPARE(before->width(), beforeWidth);
        QTest::keyClick(&host, Qt::Key_Escape);
    }
}

void ChromeInteractionTest::removalRightOfElasticKeepsTheStripsBeforeInPlace()
{
    // Right of the elastic strip the row is anchored to the right edge: the
    // right neighbour fills a vacated slot, so the strips before it stay put.
    CommandStripHost host;
    host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("left", "Left", actions(&host, 3), 0);
    host.addActionStrip("removed", "Removed", actions(&host, 3), 0);
    host.addActionStrip("right", "Right", actions(&host, 3), 0);
    host.addStrip("other-row", "Other row", new QLabel("Other row"), 1, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* left = host.stripWidget("left");
    auto* right = host.stripWidget("right");
    const QRect leftBounds = left->geometry(), rightBounds = right->geometry();
    const int removedWidth = host.stripWidget("removed")->width();
    auto* handle = host.stripWidget("removed")->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    const auto destination = host.mapToGlobal(QPoint(200, 48));
    startDrag(host, handle, destination);
    QCOMPARE(left->geometry(), leftBounds);
    QCOMPARE(right->geometry().right(), rightBounds.right());
    QCOMPARE(right->width(), rightBounds.width() + removedWidth + 4);
    mouse(&host, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(host.stripOrder(0), (QStringList{"elastic", "left", "right"}));
    QCOMPARE(left->geometry(), leftBounds);
    QCOMPARE(right->geometry().right(), rightBounds.right());
}

void ChromeInteractionTest::stripConstraintsProtectContentThroughSizingAndRestore()
{
    CommandStripHost host;
    host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("protected", "Protected", actions(&host, 3), 0);
    QVERIFY(host.setStripOptionsEnabled("protected", false));
    QVERIFY(host.setStripConstraints("protected", {false, false}));
    host.setLocked(false); host.resize(800, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* protectedStrip = host.stripWidget("protected");
    const int natural = protectedStrip->width();
    QVERIFY(!host.setStripWidth("protected", natural - 1));
    QVERIFY(!host.setStripSizing("protected", CommandStripHost::StripSizing::Elastic));
    QVERIFY(host.setStripWidth("protected", natural + 60));
    QCOMPARE(protectedStrip->width(), natural + 60);
    host.resize(20, host.height());
    QVERIFY(host.width() >= host.minimumSizeHint().width());
    QCOMPARE(protectedStrip->width(), natural);
    QVERIFY(host.rect().contains(protectedStrip->mapTo(&host, protectedStrip->rect().bottomRight())));
    QVERIFY(host.overflowItems("protected").isEmpty());
    auto saved = host.saveLayout(); auto settings = saved.value("strips").toObject();
    auto entry = settings.value("protected").toObject(); entry.insert("width", 36); entry.insert("sizing", "elastic");
    settings.insert("protected", entry); saved.insert("strips", settings);
    host.restoreLayout(saved);
    QCOMPARE(host.stripSizing("protected"), CommandStripHost::StripSizing::Content);
    QCOMPARE(protectedStrip->width(), natural);
    QVERIFY(!host.stripConstraints("protected").compressible);
    QVERIFY(!host.stripConstraints("protected").elasticAllowed);
    QVERIFY(!host.isStripOptionsEnabled("protected"));
}

void ChromeInteractionTest::partialRestoreIgnoresUnavailableStripsButRejectsMalformedKnownRows()
{
    CommandStripHost source;
    source.addStrip("missing", "Missing", new QLabel("Not in this window"), 0);
    source.addStrip("known", "Known", new QLabel("Known"), 1);
    source.setLocked(false); QVERIFY(source.setStripWidth("known", 120));
    CommandStripHost target;
    target.addStrip("known", "Known", new QLabel("Known"), 0);
    target.restoreLayout(source.saveLayout());
    QCOMPARE(target.stripOrder(0), QStringList{});
    QCOMPARE(target.stripOrder(1), QStringList({"known"}));
    QVERIFY(!target.isLocked());
    QCOMPARE(target.saveLayout().value("strips").toObject().value("known").toObject().value("width").toInt(), 120);
    const auto valid = target.saveLayout();
    auto duplicate = valid;
    duplicate.insert("rows", QJsonArray{QJsonArray{"known"}, QJsonArray{"known"}});
    target.restoreLayout(duplicate); QCOMPARE(target.saveLayout(), valid);
    auto malformed = valid;
    malformed.insert("rows", QJsonArray{QJsonArray{42}, QJsonArray{"known"}});
    target.restoreLayout(malformed); QCOMPARE(target.saveLayout(), valid);
}

void ChromeInteractionTest::extremeRequestedWidthsRemainBoundedAndRestorable()
{
    CommandStripHost host;
    for (const auto& id : {"first", "second", "third"}) host.addActionStrip(id, id, actions(&host, 3), 0);
    host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(640, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    for (const auto& id : {"first", "second", "third"})
        QVERIFY(host.setStripWidth(id, std::numeric_limits<int>::max()));
    const auto saved = host.saveLayout();
    for (const auto& id : host.stripIds()) {
        auto* strip = host.stripWidget(id);
        QVERIFY(strip->width() >= 36);
        QVERIFY(host.rect().contains(strip->mapTo(&host, strip->rect().bottomRight())));
    }
    QVERIFY(host.moveStrip("second", 1, 0));
    host.restoreLayout(saved);
    QCOMPARE(host.saveLayout(), saved);
    QCOMPARE(host.stripWidget("elastic")->geometry().right(), host.width() - 1); // No outer row gutter.
}

void ChromeInteractionTest::resettingWidthsKeepsTheCorrectAnchorAndOtherRequests_data()
{
    QTest::addColumn<QString>("side");
    QTest::addColumn<bool>("compressed");
    for (const auto& side : {"no-elastic", "before-elastic", "after-elastic"})
        for (bool compressed : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(side, compressed ? "compressed" : "stretched")))
                << QString(side) << compressed;
}

void ChromeInteractionTest::resettingWidthsKeepsTheCorrectAnchorAndOtherRequests()
{
    QFETCH(QString, side);
    QFETCH(bool, compressed);
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 2), 0);
    if (side == "after-elastic") host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("target", "Target", actions(&host, 6), 0);
    host.addActionStrip("after", "After", actions(&host, 4), 0);
    if (side == "before-elastic") host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* target = host.stripWidget("target");
    const int natural = target->width();
    QVERIFY(host.setStripWidth("before", 100));
    QVERIFY(host.setStripWidth("after", 150));
    QVERIFY(host.setStripWidth("target", compressed ? 45 : natural + 80));
    const int head = target->x(); const int tail = target->geometry().right();
    const QRect after = host.stripWidget("after")->geometry();
    const auto original = host.saveLayout();
    QVERIFY(host.resetStripWidth("target"));
    QCOMPARE(target->width(), natural);
    if (side == "after-elastic") {
        QCOMPARE(target->geometry().right(), tail);
        QCOMPARE(host.stripWidget("after")->geometry(), after);
    } else {
        QCOMPARE(target->x(), head);
        QCOMPARE(host.stripWidget("after")->x(), after.x() + natural - (compressed ? 45 : natural + 80));
    }
    const auto reset = host.saveLayout();
    QCOMPARE(reset.value("rows"), original.value("rows"));
    const auto settings = reset.value("strips").toObject();
    for (const auto& id : host.stripIds()) if (id != "target")
        QCOMPARE(settings.value(id), original.value("strips").toObject().value(id));
    QCOMPARE(settings.value("target").toObject().value("width").toInt(), -1);
    QVERIFY(settings.value("target").toObject().value("naturalWidthPriority").toBool());
    const auto resetGeometry = target->geometry();
    host.restoreLayout(original); host.restoreLayout(reset);
    QCOMPARE(host.saveLayout(), reset); QCOMPARE(target->geometry(), resetGeometry);
    host.refreshLayout(); QCOMPARE(target->geometry(), resetGeometry);
}

void ChromeInteractionTest::resetWidthMenuUsesTheHostCommandCallback()
{
    CommandStripHost host;
    host.addActionStrip("tools", "工具测试", actions(&host, 5), 0);
    host.resize(600, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const int natural = host.stripWidget("tools")->width();
    QVERIFY(host.setStripWidth("tools", 45));
    QCOMPARE(host.stripTitle("tools"), QStringLiteral("工具测试"));
    QVERIFY(host.stripTitle("missing").isEmpty());
    int requests = 0; QString requested;
    host.setWidthResetRequest([&](const QString& id) { ++requests; requested = id; QVERIFY(host.resetStripWidth(id)); });
    std::unique_ptr<QMenu> menu(host.createOverflowMenu("tools", &host));
    auto* reset = menu->findChild<QAction*>("StripResetWidthAction"); QVERIFY(reset);
    reset->trigger(); QCOMPARE(requests, 1); QCOMPARE(requested, QString("tools"));
    QCOMPARE(host.stripWidget("tools")->width(), natural);
    QVERIFY(!host.resetStripWidth("missing"));
}

void ChromeInteractionTest::resettingHiddenStripsDoesNotChangeVisibleGeometry()
{
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 2), 0);
    host.addActionStrip("target", "Target", actions(&host, 6), 0);
    host.addActionStrip("hidden", "Hidden", actions(&host, 4), 0);
    host.addActionStrip("after", "After", actions(&host, 3), 0);
    host.setLocked(false); host.resize(700, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const int natural = host.stripWidget("target")->width();
    QVERIFY(host.setStripWidth("hidden", 70)); QVERIFY(host.setStripVisible("hidden", false));
    QVERIFY(host.setStripWidth("target", 45));
    const int head = host.stripWidget("target")->x();
    QVERIFY(host.resetStripWidth("target"));
    QCOMPARE(host.stripWidget("target")->x(), head); QCOMPARE(host.stripWidget("target")->width(), natural);
    const QRect before = host.stripWidget("before")->geometry();
    const QRect target = host.stripWidget("target")->geometry();
    const QRect after = host.stripWidget("after")->geometry();
    const auto beforeReset = host.saveLayout();
    QVERIFY(host.resetStripWidth("hidden"));
    QCOMPARE(host.stripWidget("before")->geometry(), before);
    QCOMPARE(host.stripWidget("target")->geometry(), target);
    QCOMPARE(host.stripWidget("after")->geometry(), after);
    const auto settings = host.saveLayout().value("strips").toObject();
    QCOMPARE(settings.value("hidden").toObject().value("width").toInt(), -1);
    QCOMPARE(settings.value("target"), beforeReset.value("strips").toObject().value("target"));
}

void ChromeInteractionTest::widthResetPrioritySurvivesRestoreAndDragCancellation()
{
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 2), 0);
    host.addActionStrip("target", "Target", actions(&host, 6), 0);
    host.addActionStrip("after", "After", actions(&host, 4), 0);
    host.addStrip("second-row", "Second row", new QLabel("Title"), 1, CommandStripHost::StripSizing::Elastic);
    host.setLocked(false); host.resize(500, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QVERIFY(host.setStripWidth("target", 45)); QVERIFY(host.resetStripWidth("target"));
    const auto reset = host.saveLayout();
    const QRect targetBounds = host.stripWidget("target")->geometry();
    auto* handle = host.stripWidget("target")->findChild<QWidget*>("StripHandle");
    startDrag(host, handle, host.mapToGlobal(QPoint(100, 48)));
    QCOMPARE(host.saveLayout(), reset);
    QTest::keyClick(&host, Qt::Key_Escape);
    QCOMPARE(host.saveLayout(), reset); QCOMPARE(host.stripWidget("target")->geometry(), targetBounds);
    QVERIFY(host.setStripWidth("target", 50));
    QVERIFY(!host.saveLayout().value("strips").toObject().value("target").toObject().contains("naturalWidthPriority"));
    host.restoreLayout(reset); QCOMPARE(host.saveLayout(), reset);
    auto multiple = reset; auto settings = multiple.value("strips").toObject();
    auto before = settings.value("before").toObject(); before.insert("naturalWidthPriority", true); settings.insert("before", before);
    multiple.insert("strips", settings); host.restoreLayout(multiple);
    const auto normalized = host.saveLayout().value("strips").toObject();
    QVERIFY(!normalized.value("before").toObject().contains("naturalWidthPriority"));
    QVERIFY(normalized.value("target").toObject().value("naturalWidthPriority").toBool());
    auto malformed = reset; auto malformedSettings = malformed.value("strips").toObject();
    auto target = malformedSettings.value("target").toObject(); target.insert("naturalWidthPriority", "invalid");
    malformedSettings.insert("target", target); malformed.insert("strips", malformedSettings);
    const auto valid = host.saveLayout(); host.restoreLayout(malformed); QCOMPARE(host.saveLayout(), valid);
    auto explicitWidth = reset; auto explicitSettings = explicitWidth.value("strips").toObject();
    auto explicitTarget = explicitSettings.value("target").toObject();
    explicitTarget.insert("width", 90); explicitSettings.insert("target", explicitTarget);
    explicitWidth.insert("strips", explicitSettings); host.restoreLayout(explicitWidth);
    const auto restoredTarget = host.saveLayout().value("strips").toObject().value("target").toObject();
    QCOMPARE(restoredTarget.value("width").toInt(), 90);
    QVERIFY(!restoredTarget.contains("naturalWidthPriority"));
    QVERIFY(host.moveStrip("target", 1, 0));
    for (const auto& value : host.saveLayout().value("strips").toObject())
        QVERIFY(!value.toObject().contains("naturalWidthPriority"));
}

void ChromeInteractionTest::resetBeforeElasticKeepsAnAlreadyCompressedPrefix()
{
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 8), 0);
    host.addActionStrip("target", "Target", actions(&host, 6), 0);
    host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("right", "Right", actions(&host, 8), 0);
    host.setLocked(false); host.resize(600, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QVERIFY(host.setStripWidth("before", 400));
    QVERIFY(host.setStripWidth("target", 45));
    QVERIFY(host.setStripWidth("right", 400));
    auto* before = host.stripWidget("before");
    auto* target = host.stripWidget("target");
    auto* right = host.stripWidget("right");
    QVERIFY(before->width() < 400);
    const int head = target->x();
    const int prefixWidth = before->width();
    const QRect rightBounds = right->geometry();
    QVERIFY(host.resetStripWidth("target"));
    QCOMPARE(target->x(), head);
    QCOMPARE(before->width(), prefixWidth);
    QCOMPARE(right->geometry(), rightBounds);
    const auto state = host.saveLayout().value("strips").toObject();
    QCOMPARE(state.value("before").toObject().value("width").toInt(), 400);
    QCOMPARE(state.value("right").toObject().value("width").toInt(), 400);
    QCOMPARE(state.value("target").toObject().value("width").toInt(), -1);
}

void ChromeInteractionTest::narrowWidthResetsYieldOnTheOppositeSide_data()
{
    QTest::addColumn<bool>("rightOfElastic");
    QTest::newRow("head-fixed") << false;
    QTest::newRow("tail-fixed") << true;
}

void ChromeInteractionTest::narrowWidthResetsYieldOnTheOppositeSide()
{
    QFETCH(bool, rightOfElastic);
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 2), 0);
    if (rightOfElastic) {
        host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
        host.addActionStrip("preceding", "Preceding", actions(&host, 8), 0);
    }
    host.addActionStrip("target", "Target", actions(&host, 8), 0);
    host.addActionStrip("after", "After", actions(&host, 8), 0);
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const int natural = host.stripWidget("target")->width();
    QVERIFY(host.setStripWidth("before", 100)); QVERIFY(host.setStripWidth("target", 45));
    QVERIFY(host.setStripWidth("after", rightOfElastic ? 100 : 220));
    if (rightOfElastic) QVERIFY(host.setStripWidth("preceding", 220));
    host.resize(360, host.height());
    const QRect original = host.stripWidget("target")->geometry();
    const auto snapshot = host.saveLayout();
    QVERIFY(host.resetStripWidth("target"));
    auto* target = host.stripWidget("target");
    QVERIFY(target->width() >= 36); QVERIFY(target->width() <= natural);
    if (rightOfElastic) {
        QCOMPARE(target->geometry().right(), original.right());
        QCOMPARE(host.stripWidget("preceding")->width(), 36);
    } else {
        QCOMPARE(target->x(), original.x());
        QCOMPARE(host.stripWidget("after")->width(), 36);
    }
    const auto requests = host.saveLayout().value("strips").toObject();
    for (const auto& id : host.stripIds()) if (id != "target")
        QCOMPARE(requests.value(id), snapshot.value("strips").toObject().value(id));
    host.resize(1200, host.height());
    QCOMPARE(target->width(), natural);
    host.resize(host.minimumSizeHint().width(), host.height());
    for (const auto& id : host.stripIds()) {
        auto* strip = host.stripWidget(id);
        QVERIFY(strip->width() >= 36);
        QVERIFY(host.rect().contains(strip->mapTo(&host, strip->rect().bottomRight())));
    }
}

void ChromeInteractionTest::rightBlockTracksTheWindowEdgeAndRecoversRequestedWidths_data()
{
    QTest::addColumn<bool>("hiddenTrailing");
    QTest::newRow("two-trailing-strips") << false;
    QTest::newRow("hidden-trailing-strip") << true;
}

void ChromeInteractionTest::rightBlockTracksTheWindowEdgeAndRecoversRequestedWidths()
{
    QFETCH(bool, hiddenTrailing);
    CommandStripHost host;
    host.addActionStrip("before", "Before", actions(&host, 10), 0);
    host.addStrip("elastic", "Elastic", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    host.addActionStrip("right-first", "Right first", actions(&host, 5), 0);
    host.addActionStrip("right-last", "Right last", actions(&host, 3), 0);
    host.setLocked(false); host.resize(1200, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QVERIFY(host.setStripWidth("before", 400)); QVERIFY(host.setStripWidth("right-first", 130));
    QVERIFY(host.setStripWidth("right-last", 90));
    if (hiddenTrailing) QVERIFY(host.setStripVisible("right-last", false));
    const auto saved = host.saveLayout();
    const auto lastId = hiddenTrailing ? "right-first" : "right-last";
    for (const int width : {800, 500, host.minimumSizeHint().width(), 1300, 1200}) {
        host.resize(width, host.height());
        QCOMPARE(host.width() - host.stripWidget(lastId)->geometry().right() - 1, 0); // No outer row gutter.
        if (!hiddenTrailing) QCOMPARE(host.stripWidget("right-last")->x() - host.stripWidget("right-first")->geometry().right() - 1, 4);
        QCOMPARE(host.saveLayout(), saved);
        if (width >= 500) {
            QCOMPARE(host.stripWidget("right-first")->width(), 130);
            if (!hiddenTrailing) QCOMPARE(host.stripWidget("right-last")->width(), 90);
        }
    }
    host.restoreLayout(saved);
    QCOMPARE(host.stripWidget("right-first")->width(), 130);
    QCOMPARE(host.width() - host.stripWidget(lastId)->geometry().right() - 1, 0); // No outer row gutter.
}

void ChromeInteractionTest::contextMenuListsEveryStripAndOmitsUnrelatedWindowKinds()
{
    WindowManager manager;
    const auto windowId = manager.createWindow();
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(windowId));
    QVERIFY(window); QVERIFY(window->strips());
    std::unique_ptr<QMenu> menu(window->strips()->createContextMenu(window));
    QStringList listed;
    for (auto* action : menu->actions()) {
        if (action->objectName() == "StripVisibilityAction") listed.append(action->data().toString());
        QVERIFY(action->data().toString() != "window.promote");
        QVERIFY(action->data().toString() != "window.redock");
        QVERIFY(action->data().toString() != "window.demote");
    }
    QCOMPARE(listed, QStringList({"menu", "title", "caption-controls", "workspace"}));
    const auto menuActions = menu->actions();
    const auto visibilityIterator = std::find_if(menuActions.begin(), menuActions.end(), [](QAction* action) {
        return action->objectName() == "StripVisibilityAction" && action->data().toString() == "workspace";
    });
    QVERIFY(visibilityIterator != menuActions.end());
    auto* visibility = *visibilityIterator;
    QVERIFY(visibility->isChecked()); visibility->trigger();
    QVERIFY(!window->strips()->isStripVisible("workspace"));
}

void ChromeInteractionTest::stripCustomizationsSurviveWindowKindConversions()
{
    WindowManager manager;
    const auto windowId = manager.createWindow();
    const auto otherId = manager.createWindow();
    QVERIFY(!manager.addTab(windowId, new QLabel("Content"), {"Content", "test.label", {}}).isNull());
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(windowId)); QVERIFY(window);
    auto* strips = window->strips(); QVERIFY(strips);
    strips->setLocked(false);
    QVERIFY(strips->moveStrip("title", 1, 0));
    QVERIFY(strips->setStripVisible("workspace", false));
    QVERIFY(strips->setStripItemVisible("workspace", "pane.split.right", false));
    const auto snapshot = strips->saveLayout();
    QVERIFY(manager.demoteWindow(windowId, otherId));
    QVERIFY(window->strips());
    QCOMPARE(window->strips()->stripIds(), QStringList({"floating-menu", "title", "caption-controls"}));
    // Each shell keeps its own toolbar layout. Editing the lightweight shell
    // cannot erase the dormant Workbench button choices or row positions.
    QCOMPARE(window->saveInactiveStripLayouts().value("workbench").toObject(), snapshot);
    QVERIFY(window->strips()->moveStrip("caption-controls", CommandStripHost::Area::Bottom, 0, 0));
    const auto floating = window->saveStripLayout();
    QVERIFY(!floating.value("strips").toObject().contains("menu"));
    QVERIFY(!floating.value("strips").toObject().contains("workspace"));
    QVERIFY(manager.promoteWindow(windowId));
    QVERIFY(window->strips());
    QCOMPARE(window->strips()->saveLayout(), snapshot);
    QVERIFY(manager.demoteWindow(windowId, otherId));
    QCOMPARE(window->saveStripLayout(), floating);
    QCOMPARE(window->saveInactiveStripLayouts().value("workbench").toObject(), snapshot);
}

QTEST_MAIN(ChromeInteractionTest)
#include "chrome_interaction_test.moc"
