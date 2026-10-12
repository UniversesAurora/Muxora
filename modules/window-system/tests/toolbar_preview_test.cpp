// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include <window_system/theme.hpp>
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QTest>

using namespace window_system;
class ToolbarPreviewTest final : public QObject {
    Q_OBJECT
private slots:
    void colorsStayWithTheStrip_data();
    void colorsStayWithTheStrip();
};
void ToolbarPreviewTest::colorsStayWithTheStrip_data()
{
    QTest::addColumn<bool>("darkSystem"); QTest::addColumn<bool>("darkHeader");
    for (bool system : {false, true}) for (bool header : {false, true})
        QTest::newRow(qPrintable(QString("system-%1-header-%2").arg(system).arg(header))) << system << header;
}
void ToolbarPreviewTest::colorsStayWithTheStrip()
{
    QFETCH(bool, darkSystem); QFETCH(bool, darkHeader);
    const auto original = qApp->palette();
    const auto restore = qScopeGuard([&] { qApp->setPalette(original); });
    auto palette = original; palette.setColor(QPalette::Window, darkSystem ? QColor("#080808") : QColor("#ffffff"));
    qApp->setPalette(palette);
    auto theme = defaultTheme(); theme.headerBackground = QColor(darkHeader ? "#30244C" : "#DCD6EB");
    CommandStripHost host; host.addStrip("tools", "Tools", new QLabel("Text"), 0);
    host.addStrip("other", "Other", new QLabel("Other"), 0);
    applyTheme(&host, theme); host.setLocked(false); host.resize(640, 64); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* strip = host.stripWidget("tools"); auto* handle = strip->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    const auto source = host.grab(strip->geometry()).toImage();
    const auto background = source.pixelColor(qRound(2 * source.devicePixelRatio()), qRound(2 * source.devicePixelRatio()));
    QCOMPARE(background, theme.headerBackground);
    const auto start = handle->mapToGlobal(handle->rect().center());
    QMouseEvent press(QEvent::MouseButtonPress, handle->rect().center(), start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &press);
    const auto destination = host.mapToGlobal(QPoint(90, 48));
    QMouseEvent move(QEvent::MouseMove, handle->mapFromGlobal(destination), destination, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &move); QVERIFY(host.isDragging());
    auto* preview = host.findChild<QLabel*>("StripDragPreview"); QVERIFY(preview); QVERIFY(preview->isVisible());
    const auto image = preview->grab().toImage();
    QCOMPARE(image.pixelColor(qRound(2 * image.devicePixelRatio()), qRound(2 * image.devicePixelRatio())), background);
    const auto before = preview->pixmap().toImage();
    QCOMPARE(before.pixelColor(qRound(2 * before.devicePixelRatio()), qRound(2 * before.devicePixelRatio())), background);
    // A second move refreshes a hidden strip's snapshot, so compare again.
    const auto next = destination + QPoint(20, 0);
    QMouseEvent update(QEvent::MouseMove, host.mapFromGlobal(next), next, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&host, &update);
    const auto refreshed = preview->grab().toImage();
    QCOMPARE(refreshed.pixelColor(qRound(2 * refreshed.devicePixelRatio()), qRound(2 * refreshed.devicePixelRatio())), background);
    QTest::keyClick(&host, Qt::Key_Escape); QVERIFY(!host.isDragging());
}
QTEST_MAIN(ToolbarPreviewTest)
#include "toolbar_preview_test.moc"
