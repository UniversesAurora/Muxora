// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/tab_strip.hpp"
#include "style/themed_controls.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QMenu>
#include <QStyleOption>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>

using namespace window_system;
using StripSizing = CommandStripHost::StripSizing;

namespace {
int matchingPixels(const QImage& image, QRect area, QColor color, int tolerance = 0)
{
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) for (int x = area.left(); x <= area.right(); ++x) {
        const auto actual = image.pixelColor(x, y);
        if (qAbs(actual.red() - color.red()) <= tolerance && qAbs(actual.green() - color.green()) <= tolerance &&
            qAbs(actual.blue() - color.blue()) <= tolerance) ++count;
    }
    return count;
}

QImage logicalImage(QWidget* widget)
{
    const auto pixmap = widget->grab();
    return pixmap.toImage().scaled(widget->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void showFixture(QWidget& widget)
{
    widget.setWindowFlag(Qt::WindowStaysOnTopHint);
    widget.show();
}
} // namespace

class PresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void checkedIndicatorUsesWhiteAndCanFollowTheme();
    void spinButtonsShowAnOutlineOnlyUnderThePointer();
    void comboHasAnArrowWithoutAnInternalDivider();
    void tabListButtonTogglesItsOpenMenu();
    void popupOutsidePressDoesNotReplayAndReopen();
    void sizingCommandChangesOnlyTheRequestedStripAndRejectsInvalidInput();
    void allPinBadgesKeepTheirInteriorTransparent_data();
    void allPinBadgesKeepTheirInteriorTransparent();
};

void PresentationTest::checkedIndicatorUsesWhiteAndCanFollowTheme()
{
    QWidget root;
    auto* layout = new QVBoxLayout(&root);
    auto* checkbox = new ThemedCheckBox("Reveal target", &root);
    layout->addWidget(checkbox);
    applyTheme(&root);
    root.resize(260, 70); showFixture(root);
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    checkbox->setFocus(); QTest::keyClick(checkbox, Qt::Key_Space); QVERIFY(checkbox->isChecked());
    QStyleOptionButton option; option.initFrom(checkbox);
    const auto area = checkbox->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, checkbox);
    QVERIFY(matchingPixels(logicalImage(checkbox), area, Qt::white) >= 3);
    auto theme = defaultTheme(); theme.checkedIndicatorText = QColor("#ff5722");
    applyTheme(&root, theme);
    QVERIFY(matchingPixels(logicalImage(checkbox), area, theme.checkedIndicatorText) >= 3);
    QTest::keyClick(checkbox, Qt::Key_Space); QVERIFY(!checkbox->isChecked());
    QCOMPARE(matchingPixels(logicalImage(checkbox), area, theme.checkedIndicatorText), 0);
}

void PresentationTest::spinButtonsShowAnOutlineOnlyUnderThePointer()
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
    QTest::mouseMove(&root, QPoint(1, 1));
    const auto resting = logicalImage(spin);
    QCOMPARE(matchingPixels(resting, up.adjusted(0, 0, -1, -1), defaultTheme().accent), 0);
    QTest::mouseMove(spin, up.center());
    QTRY_VERIFY(matchingPixels(logicalImage(spin), up.adjusted(0, 0, -1, -1), defaultTheme().accent) >= 4);
    QTest::mouseClick(spin, Qt::LeftButton, Qt::NoModifier, up.center());
    QCOMPARE(spin->value(), 600);
    QTest::mouseMove(&root, QPoint(1, 1));
    QTRY_COMPARE(matchingPixels(logicalImage(spin), up.adjusted(0, 0, -1, -1), defaultTheme().accent), 0);
}

void PresentationTest::comboHasAnArrowWithoutAnInternalDivider()
{
    QWidget root;
    auto* layout = new QVBoxLayout(&root);
    auto* combo = new ThemedComboBox(&root); combo->addItems({"Floating", "Workbench"});
    layout->addWidget(combo); applyTheme(&root);
    root.resize(280, 80); showFixture(root); QVERIFY(QTest::qWaitForWindowExposed(&root));
    QStyleOptionComboBox option; option.initFrom(combo);
    const auto arrow = combo->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, combo);
    QVERIFY(!arrow.isEmpty());
    const auto image = logicalImage(combo);
    QVERIFY(matchingPixels(image, arrow.adjusted(3, 3, -3, -3), defaultTheme().text, 50) >= 4);
    const QRect divider(arrow.left(), arrow.top() + 3, 1, arrow.height() - 6);
    QCOMPARE(matchingPixels(image, divider, defaultTheme().border), 0);
    QTest::mouseClick(combo, Qt::LeftButton, Qt::NoModifier, QPoint(10, combo->height() / 2));
    QTRY_VERIFY(QApplication::activePopupWidget());
    combo->hidePopup(); QTRY_VERIFY(!QApplication::activePopupWidget());
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
    QVERIFY(manager.execute("window.toolbar.setSizing", {{"strip", "title"}, {"sizing", "content"}}, id));
    QCOMPARE(strips->stripSizing("title"), StripSizing::Content);
    QCOMPARE(strips->stripSizing("menu"), StripSizing::Content);
    QVERIFY(manager.execute("window.toolbar.setSizing", {}, id));
    QCOMPARE(strips->stripSizing("title"), StripSizing::Elastic);
}

void PresentationTest::allPinBadgesKeepTheirInteriorTransparent_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("pin-all") << QStringLiteral("pin-all");
    QTest::newRow("unpin-all") << QStringLiteral("unpin-all");
}

void PresentationTest::allPinBadgesKeepTheirInteriorTransparent()
{
    QFETCH(QString, name);
    auto theme = defaultTheme();
    theme.surface = QColor("#ff00bb");
    const auto image = iconFor(name, theme).pixmap(48, 48).toImage();
    QVERIFY(!image.isNull());
    QCOMPARE(matchingPixels(image, image.rect(), theme.surface), 0);
    const QRect badge(image.width() * 9 / 16, image.height() * 10 / 16,
                      image.width() * 6 / 16, image.height() * 6 / 16);
    int outlinePixels = 0;
    int transparentPixels = 0;
    for (int y = badge.top(); y <= badge.bottom(); ++y) for (int x = badge.left(); x <= badge.right(); ++x) {
        if (image.pixelColor(x, y).alpha() > 0) ++outlinePixels;
        else ++transparentPixels;
    }
    QVERIFY(outlinePixels > 5);
    QVERIFY(transparentPixels > 5);
}

QTEST_MAIN(PresentationTest)
#include "presentation_test.moc"
