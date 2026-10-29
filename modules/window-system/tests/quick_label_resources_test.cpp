// SPDX-License-Identifier: MIT
// Quick label contracts: published coordinates, retained text and independent
// texture/geometry updates. No screenshots or static appearance assertions.
#include "quick/tab_label_item.hpp"
#include "quick/tab_folder_item.hpp"
#include "quick/overlay_band_item.hpp"
#include "quick/path_stroke_item.hpp"

#include <QGuiApplication>
#include <QLineF>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

namespace {
void prepareWindow(QQuickWindow& window)
{
    window.resize(360, 240);
    window.setPersistentGraphics(false);
    window.setPersistentSceneGraph(false);
}
}

class QuickLabelResourcesTest final : public QObject {
    Q_OBJECT
private slots:
    void labelPoseUsesPublishedCoordinateSpace_data()
    {
        QTest::addColumn<QRect>("area");
        QTest::addColumn<qreal>("progress");
        for (const auto area : {QRect(24, 30, 180, 28), QRect(-80, 96, 160, 28)})
            for (const auto progress : {qreal(0), qreal(0.4), qreal(1)})
                QTest::newRow(qPrintable(QStringLiteral("x-%1-progress-%2").arg(area.x()).arg(progress)))
                    << area << progress;
    }

    void labelPoseUsesPublishedCoordinateSpace()
    {
        QFETCH(QRect, area);
        QFETCH(qreal, progress);
        TabLabelItem item;
        TabLabelPresentation label;
        label.parts.tab = area;
        label.state.shape = TabShape::Row;
        label.presence = labelPresencePose(QRectF(area), progress);
        TabStripLabelStyle style;
        QVERIFY(item.setLabel(label, style));
        // A drag grab point and its visual must use the frame's coordinate
        // transform, including before the label is fully visible.
        const auto expected = label.presence.mapRect(QRectF(area));
        const auto actual = item.mapRectToItem(nullptr, QRectF(QPointF(), area.size()));
        QVERIFY(QLineF(actual.topLeft(), expected.topLeft()).length() < 0.001);
        QVERIFY(QLineF(actual.bottomRight(), expected.bottomRight()).length() < 0.001);
    }

    void folderUpdatesRetainUnchangedShaping()
    {
        QQuickWindow window;
        prepareWindow(window);
        auto* item = new TabFolderItem(window.contentItem());
        item->setSize(QSizeF(220, 28));
        TabStripLabelStyle style;
        style.palette.text = Qt::black;
        TabFolderPaintState state;
        state.name = QStringLiteral("Long folder title");
        state.count = 4;
        state.color = Qt::darkBlue;
        state.wash = Qt::lightGray;
        state.washText = Qt::black;
        QVERIFY(item->setFolder(state, style, true));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(item->textLayoutBuilds() > 0);
        const auto initial = item->textLayoutBuilds();

        item->setPosition(QPointF(20, 60));
        QVERIFY(item->setFolder(state, style, true));
        QSignalSpy swapped(&window, &QQuickWindow::frameSwapped);
        window.update();
        QTRY_VERIFY(!swapped.isEmpty());
        QCOMPARE(item->textLayoutBuilds(), initial);

        state.count = 99;
        QVERIFY(item->setFolder(state, style, true));
        QTRY_VERIFY(item->textLayoutBuilds() > initial);
    }

    void overlayResizingKeepsTextureAndUpdatesGeometry()
    {
        QQuickWindow window;
        prepareWindow(window);
        auto* item = new OverlayBandItem(window.contentItem());
        item->setBand(QRect(10, 30, 80, 8), false, {}, 1);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(item->textureUploads() > 0);
        const auto uploads = item->textureUploads();
        const auto geometry = item->geometryUpdates();

        item->setBand(QRect(10, 30, 240, 12), false, {}, 0.5);
        QTRY_VERIFY(item->geometryUpdates() > geometry);
        QCOMPARE(item->textureUploads(), uploads);
        item->clearBand();
        item->setBand(QRect(10, 30, 160, 12), false, {}, 1);
        QTRY_VERIFY(item->textureUploads() > uploads);
    }

    void closedStrokesRecreateResourcesAcrossWindows()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
        QQuickWindow first, second;
        prepareWindow(first);
        prepareWindow(second);
        auto* item = new PathStrokeItem(first.contentItem());
        item->setSize(first.size());
        QPainterPath path;
        path.addRoundedRect(QRectF(20, 20, 180, 28), 6, 6);
        path.addRect(QRectF(30, 28, 20, 12));
        QVERIFY(item->setStroke(path, Qt::darkBlue, 1.5));
        QSignalSpy firstFrames(&first, &QQuickWindow::frameSwapped);
        first.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QTRY_VERIFY(!firstFrames.isEmpty());

        item->setParentItem(second.contentItem());
        item->setParent(second.contentItem());
        QVERIFY(item->setStroke(path, Qt::darkRed, 2));
        QSignalSpy secondFrames(&second, &QQuickWindow::frameSwapped);
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(!secondFrames.isEmpty());

        QSignalSpy invalidated(&second, &QQuickWindow::sceneGraphInvalidated);
        second.hide();
        QTRY_VERIFY(!second.isExposed());
        second.releaseResources();
        QTRY_VERIFY(!invalidated.isEmpty());
        secondFrames.clear();
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(!secondFrames.isEmpty());
    }
};

QTEST_MAIN(QuickLabelResourcesTest)
#include "quick_label_resources_test.moc"
