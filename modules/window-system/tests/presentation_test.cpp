// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/tab_strip.hpp"
#include "style/themed_controls.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QMenu>
#include <QStyleOption>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace window_system;
using StripSizing = CommandStripHost::StripSizing;

namespace {
void showFixture(QWidget& widget)
{
    widget.setWindowFlag(Qt::WindowStaysOnTopHint);
    widget.show();
}
} // namespace

class PresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void checkboxTogglesWithSpace();
    void spinButtonsChangeTheValue();
    void comboOpensAndClosesItsPopup();
    void comboDefaultAndDetailsPreserveItemData();
    void tabListButtonTogglesItsOpenMenu();
    void popupOutsidePressDoesNotReplayAndReopen();
    void sizingCommandChangesOnlyTheRequestedStripAndRejectsInvalidInput();
};

void PresentationTest::checkboxTogglesWithSpace()
{
    QWidget root;
    auto* layout = new QVBoxLayout(&root);
    auto* checkbox = new ThemedCheckBox("Reveal target", &root);
    layout->addWidget(checkbox);
    applyTheme(&root);
    root.resize(260, 70); showFixture(root);
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    checkbox->setFocus(); QTest::keyClick(checkbox, Qt::Key_Space); QVERIFY(checkbox->isChecked());
    QTest::keyClick(checkbox, Qt::Key_Space); QVERIFY(!checkbox->isChecked());
}

void PresentationTest::spinButtonsChangeTheValue()
{
    QWidget root;
    auto* layout = new QVBoxLayout(&root);
    auto* spin = new ThemedSpinBox(&root);
    spin->setRange(200, 2000); spin->setSingleStep(100); spin->setValue(500);
    layout->addWidget(spin); applyTheme(&root);
    root.resize(280, 80); showFixture(root); QVERIFY(QTest::qWaitForWindowExposed(&root));
    QStyleOptionSpinBox option; option.initFrom(spin);
    const auto up = spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin);
    QVERIFY(!up.isEmpty());
    QTest::mouseClick(spin, Qt::LeftButton, Qt::NoModifier, up.center());
    QCOMPARE(spin->value(), 600);
}

void PresentationTest::comboOpensAndClosesItsPopup()
{
    QWidget root;
    auto* layout = new QVBoxLayout(&root);
    auto* combo = new ThemedComboBox(&root); combo->addItems({"Floating", "Workbench"});
    layout->addWidget(combo); applyTheme(&root);
    root.resize(280, 80); showFixture(root); QVERIFY(QTest::qWaitForWindowExposed(&root));
    QTest::mouseClick(combo, Qt::LeftButton, Qt::NoModifier, QPoint(10, combo->height() / 2));
    QTRY_VERIFY(QApplication::activePopupWidget());
    combo->hidePopup(); QTRY_VERIFY(!QApplication::activePopupWidget());
}

void PresentationTest::comboDefaultAndDetailsPreserveItemData()
{
    QWidget root;
    auto* layout = new QVBoxLayout(&root);
    auto* combo = new ThemedComboBox(&root);
    combo->addItem("Top", 0); combo->addItem("Bottom", 1);
    layout->addWidget(combo); applyTheme(&root);
    QCOMPARE(combo->defaultIndex(), -1);
    QVERIFY(combo->itemData(1, Qt::AccessibleDescriptionRole).toString().isEmpty());

    // The default follows the item's data, not its position or text.
    combo->setDefaultData(1);
    QCOMPARE(combo->defaultIndex(), 1);
    QCOMPARE(combo->itemData(1, Qt::AccessibleDescriptionRole).toString(), QStringLiteral("Default"));
    combo->setItemDetail(0, "top");
    QCOMPARE(combo->itemText(0), QStringLiteral("Top"));

    root.resize(280, 80); showFixture(root); QVERIFY(QTest::qWaitForWindowExposed(&root));
    combo->setCurrentIndex(0);
    combo->showPopup(); QTRY_VERIFY(QApplication::activePopupWidget());
    auto* view = combo->view();
    QTRY_VERIFY(view->isVisible());
    combo->hidePopup(); QTRY_VERIFY(!QApplication::activePopupWidget());

    // Clearing or naming no item removes the mark and its spoken description.
    combo->setDefaultData(QVariant());
    QCOMPARE(combo->defaultIndex(), -1);
    QVERIFY(combo->itemData(1, Qt::AccessibleDescriptionRole).toString().isEmpty());
    combo->setDefaultData(7);
    QCOMPARE(combo->defaultIndex(), -1);
}

void PresentationTest::tabListButtonTogglesItsOpenMenu()
{
    GroupTabs tabs(defaultTheme(), nullptr); tabs.addTab(new QLabel("Content"), {}, "One", QUuid::createUuid());
    tabs.resize(400, 250); applyTheme(&tabs); showFixture(tabs); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* button = tabs.findChild<QToolButton*>("TabListButton"); QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(QApplication::activePopupWidget());
    QPointer<QWidget> popup = QApplication::activePopupWidget();
    QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(!popup || !popup->isVisible());
    QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(QApplication::activePopupWidget());
    QTest::keyClick(QApplication::activePopupWidget(), Qt::Key_Escape);
    QTRY_VERIFY(!QApplication::activePopupWidget());
}

void PresentationTest::popupOutsidePressDoesNotReplayAndReopen()
{
    GroupTabs tabs(defaultTheme(), nullptr); tabs.addTab(new QLabel("Content"), {}, "One", QUuid::createUuid());
    tabs.resize(400, 250); applyTheme(&tabs); showFixture(tabs); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* button = tabs.findChild<QToolButton*>("TabListButton"); QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(QApplication::activePopupWidget());
    auto* popup = QApplication::activePopupWidget();
    const auto global = button->mapToGlobal(button->rect().center());
    QTest::mousePress(popup, Qt::LeftButton, Qt::NoModifier, popup->mapFromGlobal(global));
    QTest::mouseRelease(button, Qt::LeftButton);
    QTRY_VERIFY(!QApplication::activePopupWidget());
    QVERIFY(!button->isDown());
}

void PresentationTest::sizingCommandChangesOnlyTheRequestedStripAndRejectsInvalidInput()
{
    WindowManager manager;
    const auto id = manager.createWindow();
    auto* strips = static_cast<WorkspaceWindow*>(manager.windowWidget(id))->strips(); QVERIFY(strips);
    const auto before = strips->saveLayout();
    QVERIFY(!manager.execute("window.toolbar.setSizing", {{"strip", "missing"}, {"sizing", "elastic"}}, id));
    QVERIFY(!manager.execute("window.toolbar.setSizing", {{"strip", "title"}, {"sizing", "invalid"}}, id));
    QCOMPARE(strips->saveLayout(), before);
    QVERIFY(!manager.execute("window.toolbar.setSizing", {{"strip", "title"}, {"sizing", "content"}}, id));
    QCOMPARE(strips->saveLayout(), before);
    QVERIFY(manager.execute("window.toolbar.setSizing", {{"strip", "workspace"}, {"sizing", "elastic"}}, id));
    QCOMPARE(strips->stripSizing("workspace"), StripSizing::Elastic);
    QCOMPARE(strips->stripSizing("title"), StripSizing::Elastic);
    QCOMPARE(strips->stripSizing("menu"), StripSizing::Content);
    QVERIFY(manager.execute("window.toolbar.setSizing", {{"strip", "workspace"}, {"sizing", "content"}}, id));
    QCOMPARE(strips->stripSizing("workspace"), StripSizing::Content);
    bool selected = false;
    QTimer::singleShot(30, &manager, [&] {
        auto* picker = qobject_cast<QMenu*>(QApplication::activePopupWidget()); QVERIFY(picker);
        QCOMPARE(picker->objectName(), QStringLiteral("ToolbarSizingPicker"));
        QCOMPARE(picker->actions().size(), 1);
        QCOMPARE(picker->actions().first()->data().toString(), QStringLiteral("workspace"));
        QTest::keyClick(picker, Qt::Key_Down); QTest::keyClick(picker, Qt::Key_Return); selected = true;
    });
    QTimer::singleShot(900, &manager, [] {
        if (auto* picker = QApplication::activePopupWidget()) picker->close();
    });
    QVERIFY(manager.execute("window.toolbar.setSizing", {}, id));
    QVERIFY(selected);
    QCOMPARE(strips->stripSizing("workspace"), StripSizing::Elastic);
    QCOMPARE(strips->stripSizing("title"), StripSizing::Elastic);
}

QTEST_MAIN(PresentationTest)
#include "presentation_test.moc"
