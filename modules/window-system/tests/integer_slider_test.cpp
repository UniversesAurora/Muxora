// SPDX-License-Identifier: MIT
#include "style/integer_slider.hpp"
#include "style/themed_controls.hpp"
#include <window_system/theme.hpp>

#include <QApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTest>
#include <QtMath>

using namespace window_system;

class IntegerSliderTest final : public QObject {
    Q_OBJECT
private slots:
    void compositionWorksWithUnmodifiedQtEditors_data();
    void compositionWorksWithUnmodifiedQtEditors();
    void unitsUseTheLeadingInteger_data();
    void unitsUseTheLeadingInteger();
    void keyboardInputRestoresTheConfiguredUnitAndKeepsStepButtons();
    void themeAndGeometryFollowTheOtherControls_data();
    void themeAndGeometryFollowTheOtherControls();
};

void IntegerSliderTest::compositionWorksWithUnmodifiedQtEditors_data()
{
    QTest::addColumn<QString>("suffix");
    QTest::newRow("unitless") << QString{};
    QTest::newRow("duration") << QStringLiteral(" ms");
    QTest::newRow("percentage") << QStringLiteral(" %");
}

void IntegerSliderTest::compositionWorksWithUnmodifiedQtEditors()
{
    QFETCH(QString, suffix);
    auto* editor = new QSpinBox;
    editor->setRange(-1000000, 1000000);
    editor->setSuffix(suffix);
    editor->setValue(20);
    IntegerSlider control(editor, -100, 100);
    QSignalSpy changes(&control, &IntegerSlider::valueChanged);
    QCOMPARE(control.slider()->value(), 20);
    control.slider()->setValue(-30);
    QCOMPARE(control.value(), -30);
    QCOMPARE(editor->value(), -30);
    QCOMPARE(changes.size(), 1);
    control.setValue(50000);
    QCOMPARE(control.value(), 50000);
    QCOMPARE(control.slider()->value(), 100);
    QCOMPARE(changes.size(), 2);
    editor->stepUp();
    QCOMPARE(control.value(), 50001);
    QCOMPARE(control.slider()->value(), 100);
    QCOMPARE(changes.size(), 3);
    control.setValue(50001);
    QCOMPARE(changes.size(), 3);
    control.setValue(0);
    QCOMPARE(control.slider()->value(), 0);
    QCOMPARE(control.value(), 0);
    QCOMPARE(editor->suffix(), suffix);
    control.setEnabled(false);
    QVERIFY(!control.slider()->isEnabled());
    QVERIFY(!editor->isEnabled());
}

void IntegerSliderTest::unitsUseTheLeadingInteger_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<int>("expected");
    QTest::newRow("normal-unit") << QStringLiteral("246 px") << 246;
    QTest::newRow("deleted-unit") << QStringLiteral("246") << 246;
    QTest::newRow("other-unit") << QStringLiteral("246 cm") << 246;
    QTest::newRow("changed-unit") << QStringLiteral("246 anything") << 246;
    QTest::newRow("no-space") << QStringLiteral("246em") << 246;
    QTest::newRow("zero") << QStringLiteral("0 cm") << 0;
    QTest::newRow("large") << QStringLiteral("1000000 pt") << 1000000;
    QTest::newRow("no-number") << QStringLiteral("cm") << 160;
    QTest::newRow("cleared") << QString{} << 160;
    QTest::newRow("negative") << QStringLiteral("-1 px") << 160;
    QTest::newRow("out-of-range") << QStringLiteral("1000001 px") << 160;
    QTest::newRow("overflow") << QStringLiteral("999999999999999999999 px") << 160;
}

void IntegerSliderTest::unitsUseTheLeadingInteger()
{
    QFETCH(QString, input);
    QFETCH(int, expected);
    UnitSpinBox editor;
    editor.setRange(0, 1000000);
    editor.setSuffix(QStringLiteral(" px"));
    editor.setValue(160);
    auto* line = editor.findChild<QLineEdit*>();
    QVERIFY(line);
    // Replace the entire native editor text, including its display suffix.
    line->setText(input);
    editor.interpretText();
    QCOMPARE(editor.value(), expected);
    QCOMPARE(editor.text(), QString::number(expected) + QStringLiteral(" px"));
    QVERIFY(!QApplication::activePopupWidget());
    QVERIFY(!QApplication::activeModalWidget());
}

void IntegerSliderTest::keyboardInputRestoresTheConfiguredUnitAndKeepsStepButtons()
{
    UnitSpinBox editor;
    editor.setRange(-1000, 1000);
    editor.setSuffix(QStringLiteral(" ms"));
    editor.setValue(160);
    editor.show();
    QVERIFY(QTest::qWaitForWindowActive(&editor));
    editor.setFocus();
    QTest::keyClick(&editor, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(&editor, "251seconds");
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.value(), 251);
    QCOMPARE(editor.text(), QStringLiteral("251 ms"));
    QTest::keyClick(&editor, Qt::Key_Up);
    QCOMPARE(editor.value(), 252);
    QTest::keyClick(&editor, Qt::Key_Down);
    QCOMPARE(editor.value(), 251);
    QCOMPARE(editor.buttonSymbols(), QAbstractSpinBox::UpDownArrows);
    QStyleOptionSpinBox option; option.initFrom(&editor); option.frame = true;
    option.buttonSymbols = QAbstractSpinBox::UpDownArrows;
    option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
    const auto up = editor.style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, &editor);
    const auto down = editor.style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, &editor);
    QVERIFY(!up.isEmpty() && !down.isEmpty());
    QTest::mouseClick(&editor, Qt::LeftButton, Qt::NoModifier, up.center());
    QCOMPARE(editor.value(), 252);
    QTest::mouseClick(&editor, Qt::LeftButton, Qt::NoModifier, down.center());
    QCOMPARE(editor.value(), 251);
    QVERIFY(editor.hasFocus());
}

void IntegerSliderTest::themeAndGeometryFollowTheOtherControls_data()
{
    QTest::addColumn<QColor>("surface");
    QTest::addColumn<QColor>("border");
    QTest::newRow("light") << QColor("#ffffff") << QColor("#aaa0b5");
    QTest::newRow("dark") << QColor("#242126") << QColor("#685e72");
    QTest::newRow("black") << QColor("#080808") << QColor("#504956");
}

void IntegerSliderTest::themeAndGeometryFollowTheOtherControls()
{
    QFETCH(QColor, surface);
    QFETCH(QColor, border);
    auto theme = defaultTheme(); theme.surface = surface; theme.border = border;
    auto* editor = new UnitSpinBox;
    editor->setRange(0, 1000000); editor->setSuffix(QStringLiteral(" px")); editor->setValue(500);
    IntegerSlider control(editor, 0, 1000);
    applyTheme(&control, theme);
    control.resize(400, control.sizeHint().height()); control.show();
    QVERIFY(QTest::qWaitForWindowExposed(&control));
    auto* slider = control.slider();
    const QRect track(slider->mapTo(&control, QPoint()), slider->size());
    const QRect number(editor->mapTo(&control, QPoint()), editor->size());
    QVERIFY(number.left() > track.right());
    QVERIFY(qAbs(track.center().y() - number.center().y()) <= 1);
    QCOMPARE(control.height(), editor->height());
    QStyleOptionSlider option; option.initFrom(slider); option.orientation = Qt::Horizontal;
    option.minimum = 0; option.maximum = 1000; option.sliderPosition = 500; option.sliderValue = 500;
    const auto groove = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, slider);
    QVERIFY(groove.width() > 20);
    const auto image = slider->grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    const auto sample = [&](int numerator) {
        return image.pixelColor(qFloor((groove.left() + groove.width() * numerator / 4) * ratio),
            qFloor(groove.center().y() * ratio));
    };
    QCOMPARE(sample(1), theme.accent);
    QCOMPARE(sample(3), theme.border);
}

QTEST_MAIN(IntegerSliderTest)
#include "integer_slider_test.moc"
