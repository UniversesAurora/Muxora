// SPDX-License-Identifier: MIT
// Phase 10.4b: popup input and lifetime. Dimensions: disabled/hidden/separator
// rows, keyboard cascades, outside capture, screen edge, owner destruction.
#include "quick/popup_menu.hpp"
#include "native_input.hpp"
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using window_system::quick::PopupMenu;

namespace {
MenuItemDescription action(QString id, bool enabled = true)
{
    MenuItemDescription result;
    result.id = result.commandId = id;
    result.action.title = std::move(id);
    result.action.enabled = enabled;
    return result;
}
QQuickWindow* popup(QQuickWindow* parent)
{
    for (auto* window : QGuiApplication::topLevelWindows())
        if (window->type() == Qt::Popup && window->isVisible() && window->transientParent() == parent)
            return qobject_cast<QQuickWindow*>(window);
    return nullptr;
}
class InputItem final : public QQuickItem {
public:
    explicit InputItem(QQuickItem* parent) : QQuickItem(parent) { setAcceptedMouseButtons(Qt::LeftButton); }
    int presses = 0;
protected:
    void mousePressEvent(QMouseEvent* event) override { ++presses; event->accept(); }
};
}

class QuickPopupMenuTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*(Unable to assign|TypeError|ReferenceError|Binding loop).*"));
    }

    void keyboardSkipsUnavailableRowsAndPreservesPayload_data()
    {
        QTest::addColumn<int>("key");
        QTest::newRow("down") << int(Qt::Key_Down);
        QTest::newRow("home") << int(Qt::Key_Home);
        QTest::newRow("end") << int(Qt::Key_End);
    }
    void keyboardSkipsUnavailableRowsAndPreservesPayload()
    {
        QFETCH(int, key);
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto disabled = action("disabled", false), hidden = action("hidden"), chosen = action("chosen");
        hidden.action.visible = false;
        chosen.action.checkable = true; chosen.action.checked = true;
        chosen.action.shortcut = "Ctrl+Shift+K"; chosen.arguments.insert("value", 42);
        MenuItemDescription separator; separator.kind = MenuItemDescription::Kind::Separator;
        PopupMenu menu(defaultTheme());
        QString command; QVariantMap arguments;
        connect(&menu, &PopupMenu::triggered, this, [&](const auto& item) { command = item.commandId; arguments = item.arguments; });
        QSignalSpy closed(&menu, &PopupMenu::closed);
        QVERIFY(menu.open(window.contentItem(), window.mapToGlobal(QPoint(30, 40)), {disabled, hidden, separator, chosen}));
        QTRY_VERIFY(popup(&window));
        auto* view = popup(&window);
        QTest::keyClick(view, Qt::Key(key));
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_COMPARE(command, QString("chosen"));
        QCOMPARE(arguments.value("value").toInt(), 42);
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(!popup(&window));
    }

    void nestedKeyboardAndEscapeRestoreFocus_data()
    {
        QTest::addColumn<bool>("execute");
        QTest::newRow("escape") << false;
        QTest::newRow("execute") << true;
    }
    void nestedKeyboardAndEscapeRestoreFocus()
    {
        QFETCH(bool, execute);
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QQuickItem input(window.contentItem()); input.setSize({100, 30}); input.forceActiveFocus();
        auto branch = action("branch"); branch.kind = MenuItemDescription::Kind::Submenu;
        branch.children = {action("nested")};
        PopupMenu menu(defaultTheme());
        QSignalSpy triggered(&menu, &PopupMenu::triggered), closed(&menu, &PopupMenu::closed);
        QVERIFY(menu.open(window.contentItem(), window.mapToGlobal(QPoint(30, 40)), {branch, action("last")}));
        QTRY_VERIFY(popup(&window));
        auto* root = popup(&window);
        QTest::keyClick(root, Qt::Key_Down); QTest::keyClick(root, Qt::Key_Right);
        QTRY_VERIFY(popup(root));
        if (execute) QTest::keyClick(popup(root), Qt::Key_Return);
        else {
            QTest::keyClick(popup(root), Qt::Key_Escape);
            QTRY_VERIFY(!popup(root));
            QVERIFY(root->isVisible());
            QTest::keyClick(root, Qt::Key_Escape);
        }
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(triggered.count(), execute ? 1 : 0);
        if (execute) QCOMPARE(triggered.first().first().value<MenuItemDescription>().commandId, QString("nested"));
        QTRY_VERIFY(input.hasActiveFocus());
    }

    void outsideClickOnlyDismissesAndReleasesCapture()
    {
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        InputItem input(window.contentItem()); input.setPosition({20, 300}); input.setSize({150, 60});
        PopupMenu menu(defaultTheme()); QSignalSpy closed(&menu, &PopupMenu::closed);
        QVERIFY(menu.open(window.contentItem(), window.mapToGlobal(QPoint(30, 40)), {action("item")}));
        QTRY_VERIFY(popup(&window));
        native_input::mouse(&window, {80, 330}, true);
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(input.presses, 0);
        native_input::mouse(&window, {80, 330}, true);
        QTRY_COMPARE(input.presses, 1);
    }

    void ownerClosingReleasesPopupWindows()
    {
        auto window = std::make_unique<QQuickWindow>();
        window->resize(600, 400); window->show(); window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window.get()));
        auto* menu = new PopupMenu(defaultTheme(), window.get());
        QVERIFY(menu->open(window->contentItem(), window->mapToGlobal(QPoint(30, 40)), {action("item")}));
        QTRY_VERIFY(popup(window.get()));
        const QPointer<QQuickWindow> view(popup(window.get()));
        const QPointer<PopupMenu> session(menu);
        window.reset();
        QVERIFY(!session);
        QTRY_VERIFY(!view);
    }

    void screenEdgeAndScrollingKeepTheLastCommandReachable()
    {
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVector<MenuItemDescription> items;
        for (int index = 0; index < 100; ++index) items.append(action(QString::number(index)));
        PopupMenu menu(defaultTheme());
        QString command;
        connect(&menu, &PopupMenu::triggered, this, [&](const auto& item) { command = item.commandId; });
        const auto available = window.screen()->availableGeometry();
        QVERIFY(menu.open(window.contentItem(), available.bottomRight() - QPoint(1, 1), items));
        QTRY_VERIFY(popup(&window));
        auto* view = popup(&window);
        QTRY_VERIFY(available.contains(view->geometry()));
        QTest::keyClick(view, Qt::Key_End); QTest::keyClick(view, Qt::Key_Return);
        QTRY_COMPARE(command, QString("99"));
    }
};

QTEST_MAIN(QuickPopupMenuTest)
#include "quick_popup_menu_test.moc"
