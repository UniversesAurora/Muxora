// SPDX-License-Identifier: MIT
// Phase 10.4j: the accessibility tree, names, states, native accessible
// actions and ownership. No rendering or static appearance assertions.
#include "quick/button_item.hpp"
#include <window_system/theme.hpp>
#include <QAccessible>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;

class QuickAccessibilityTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*(Unable to assign|TypeError|ReferenceError|Binding loop).*"));
    }

    void buttonActionsFollowStateAndReleaseWithTheirOwner()
    {
        QQuickWindow window;
        window.resize(500, 300);
        auto* button = new quick::ButtonItem(defaultTheme(), window.contentItem());
        button->setSize(QSizeF(80, 30));
        button->setToolTip(QStringLiteral("Toggle view\nCtrl+Alt+V"));
        button->setCheckable(true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* accessible = QAccessible::queryAccessibleInterface(button);
        QVERIFY(accessible);
        QCOMPARE(accessible->role(), QAccessible::Button);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Toggle view"));
        QCOMPARE(accessible->text(QAccessible::Description), button->toolTip());
        QVERIFY(accessible->state().checkable);
        QVERIFY(!accessible->state().checked);
        QVERIFY(!accessible->state().focusable);
        button->setActiveFocusOnTab(true);
        QVERIFY(accessible->state().focusable);
        // The native Quick tree must discover the C++ item, not just a
        // direct query by a test that already has its pointer.
        auto* parent = QAccessible::queryAccessibleInterface(&window);
        QVERIFY(parent && parent->indexOfChild(accessible) >= 0);
        auto* actions = accessible->actionInterface();
        QVERIFY(actions && actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
        QSignalSpy clicked(button, &quick::ButtonItem::clicked);
        actions->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(clicked.size(), 1);
        QVERIFY(button->isChecked() && accessible->state().checked);
        button->setEnabled(false);
        QVERIFY(accessible->state().disabled);
        actions->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(clicked.size(), 1);
        button->setEnabled(true);
        button->setChecked(false);
        QVERIFY(!accessible->state().checked);
        button->setToolTip(QStringLiteral("Updated title"));
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Updated title"));
        const auto id = QAccessible::uniqueId(accessible);
        delete button;
        QVERIFY(!QAccessible::accessibleInterface(id));
    }
};

QTEST_MAIN(QuickAccessibilityTest)
#include "quick_accessibility_test.moc"
