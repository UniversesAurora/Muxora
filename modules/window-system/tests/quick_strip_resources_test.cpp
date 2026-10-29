// SPDX-License-Identifier: MIT
// Stable-ID ownership across lane changes and destruction. No appearance
// assertions: these checks detect leaked objects and lost retained resources.
#include "quick/tab_strip_item.hpp"
#include "quick/tab_label_item.hpp"
#include "quick/tab_folder_item.hpp"
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

namespace {
template<class T> T* visualChild(QQuickItem* root)
{
    for (auto* child : root->childItems()) {
        if (auto* result = dynamic_cast<T*>(child)) return result;
        if (auto* result = visualChild<T>(child)) return result;
    }
    return nullptr;
}

std::shared_ptr<TabStripFrame> stripFrame()
{
    auto frame = std::make_shared<TabStripFrame>();
    frame->style = std::make_shared<TabStripLabelStyle>();
    TabStripLaneFrame lane;
    lane.viewport = lane.regularClip = QRect(0, 0, 360, 60);
    TabLabelPresentation label;
    label.parts.tab = QRect(8, 8, 130, 28);
    label.parts.title = QRect(30, 8, 90, 28);
    label.state.title = QStringLiteral("Owned label");
    lane.regular.append({QUuid::createUuid(), 0, {}, label});
    TabFolderPaintState folder;
    folder.name = QStringLiteral("Folder");
    folder.count = 2;
    lane.folders.append({QRect(150, 8, 100, 28), folder, QUuid::createUuid()});
    frame->lanes.append(lane);
    return frame;
}
}

class QuickStripResourcesTest final : public QObject {
    Q_OBJECT
private slots:
    void stripDestructionReleasesLabelsAndFolders()
    {
        auto* strip = new TabStripItem;
        QVERIFY(strip->setFrame(stripFrame()));
        QPointer<TabLabelItem> label = visualChild<TabLabelItem>(strip);
        QPointer<TabFolderItem> folder = visualChild<TabFolderItem>(strip);
        QVERIFY(label);
        QVERIFY(folder);
        delete strip;
        QVERIFY(label.isNull());
        QVERIFY(folder.isNull());
    }

    void stableIdsSurviveLaneRemovalAndClearReleasesThem()
    {
        TabStripItem strip;
        auto frame = stripFrame();
        auto twoLanes = std::make_shared<TabStripFrame>(*frame);
        twoLanes->lanes.prepend({});
        QVERIFY(strip.setFrame(twoLanes));
        QPointer<TabLabelItem> label = visualChild<TabLabelItem>(&strip);
        QPointer<TabFolderItem> folder = visualChild<TabFolderItem>(&strip);
        QVERIFY(label);
        QVERIFY(folder);
        QVERIFY(strip.setFrame(frame));
        QCOMPARE(visualChild<TabLabelItem>(&strip), label.data());
        QCOMPARE(visualChild<TabFolderItem>(&strip), folder.data());
        strip.clearFrame();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(label.isNull());
        QVERIFY(folder.isNull());
    }

    void laneLocalizationPreservesPublishedCoordinates()
    {
        TabStripItem strip;
        auto frame = stripFrame();
        auto& lane = frame->lanes[0];
        lane.viewport = QRect(10, 20, 360, 60);
        lane.regularClip = QRect(70, 20, 300, 60);
        auto& label = lane.regular[0].label;
        label.parts.tab.translate(100, 20);
        label.parts.title.translate(100, 20);
        label.presence = labelPresencePose(QRectF(label.parts.tab), 0.4);
        TabLabelItem reference;
        QVERIFY(reference.setLabel(label, *frame->style));
        QVERIFY(strip.setFrame(frame));
        auto* placed = visualChild<TabLabelItem>(&strip);
        QVERIFY(placed);
        const QRectF area(QPointF(), label.parts.tab.size());
        QCOMPARE(placed->mapRectToItem(&strip, area), reference.mapRectToItem(nullptr, area));
    }

    void maskedLanesFollowWindowAndSceneGraphLifetime()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*(ShaderEffect|QML|shader|Required property).*")));
        QQuickWindow first, second;
        first.resize(400, 240);
        second.resize(400, 240);
        second.setPersistentGraphics(false);
        second.setPersistentSceneGraph(false);
        auto* strip = new TabStripItem(first.contentItem());
        auto frame = stripFrame();
        frame->lanes[0].fades = {{QRect(0, 0, 16, 60), Qt::LeftEdge, {}},
                                {QRect(0, 0, 360, 12), Qt::TopEdge, QLineF(0, 0, 12, 12)}};
        QVERIFY(strip->setFrame(frame));
        QPointer<TabLabelItem> label = visualChild<TabLabelItem>(strip);
        QSignalSpy firstFrames(&first, &QQuickWindow::frameSwapped);
        first.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QTRY_VERIFY(!firstFrames.isEmpty());
        strip->setParent(second.contentItem());
        strip->setParentItem(second.contentItem());
        QSignalSpy secondFrames(&second, &QQuickWindow::frameSwapped);
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(!secondFrames.isEmpty());
        second.hide();
        QTRY_VERIFY(!second.isExposed());
        second.releaseResources();
        secondFrames.clear();
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(!secondFrames.isEmpty());
        delete strip;
        QVERIFY(label.isNull());
    }
};

QTEST_MAIN(QuickStripResourcesTest)
#include "quick_strip_resources_test.moc"
