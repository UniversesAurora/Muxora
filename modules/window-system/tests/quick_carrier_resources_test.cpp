// SPDX-License-Identifier: MIT
// Quick rendering design sections 2-4. External carriers retain stable label
// identity, independent piece placement and window-owned scene resources.
// Dimensions: piece order/count x source lifetime x scene/window lifetime.
#include "quick/tab_carrier_item.hpp"
#include "quick/tab_label_item.hpp"
#include "quick/tab_label_text_item.hpp"
#include "style/tab_carrier_label.hpp"
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

namespace {
TabCarrierPiece piece(QString title, QPoint offset, bool selected = false)
{
    auto content = std::make_shared<TabCarrierLabels>();
    content->palette.text = Qt::black;
    content->palette.mutedText = Qt::darkGray;
    content->palette.active = Qt::white;
    TabLabelPresentation label;
    label.state.title = std::move(title);
    label.state.shape = TabShape::Row;
    label.state.selected = selected;
    label.parts.tab = QRect(12, 8, 180, 28);
    label.parts.title = QRect(32, 8, 140, 28);
    label.presence = {0.8, 0.95, QPointF(3, 5)};
    content->labels.append({QUuid::createUuid(), label});
    auto frame = std::make_shared<TabCarrierFrame>();
    frame->content = content;
    frame->samples.append({label.parts, 1, 0});
    return {offset, frame};
}

QList<TabLabelItem*> labels(TabCarrierItem& carrier)
{
    QList<TabLabelItem*> result;
    for (auto* parent : carrier.childItems())
        for (auto* child : parent->childItems())
            if (auto* label = dynamic_cast<TabLabelItem*>(child)) result.append(label);
    return result;
}

void prepareWindow(QQuickWindow& window)
{
    auto format = window.format();
    format.setSamples(4);
    window.setFormat(format);
    window.resize(440, 240);
    window.setPersistentGraphics(false);
    window.setPersistentSceneGraph(false);
}
}

class QuickCarrierResourcesTest final : public QObject {
    Q_OBJECT
private slots:
    void piecesRetainIdentityAndReleaseRemovedSources()
    {
        TabDragVisual visual;
        visual.size = QSize(420, 80);
        visual.pieces = {piece(QStringLiteral("First"), QPoint(8, 4)),
                         piece(QStringLiteral("Second"), QPoint(210, 8))};
        auto firstSource = visual.pieces[0].frame->content;
        std::weak_ptr<const TabCarrierLabels> ownership = firstSource;
        TabCarrierItem carrier;
        QVERIFY(carrier.setVisual(visual));
        const auto initial = labels(carrier);
        QCOMPARE(initial.size(), 2);
        QPointer<TabLabelItem> first = initial[0], second = initial[1];
        const auto firstShaping = first->textItem()->renderStats().textLayoutBuilds;

        QQuickItem referenceRoot;
        referenceRoot.setPosition(visual.pieces[0].offset);
        TabLabelItem reference(&referenceRoot);
        TabStripLabelStyle style;
        style.metrics = firstSource->metrics;
        style.palette = firstSource->palette;
        QVERIFY(reference.setLabel(carrierLabelPresentation(firstSource->labels[0],
            visual.pieces[0].frame->samples[0], nullptr), style));
        const QRectF local(QPointF(), first->size());
        QCOMPARE(first->mapRectToItem(&carrier, local), reference.mapRectToItem(nullptr, local));

        std::swap(visual.pieces[0], visual.pieces[1]);
        QVERIFY(carrier.setVisual(visual));
        const auto reordered = labels(carrier);
        QCOMPARE(reordered[0], second.data());
        QCOMPARE(reordered[1], first.data());
        QCOMPARE(first->textItem()->renderStats().textLayoutBuilds, firstShaping);
        visual.pieces.removeFirst();
        QVERIFY(carrier.setVisual(visual));
        QCOMPARE(labels(carrier).front(), first.data());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(second.isNull());

        firstSource.reset();
        visual = {};
        QVERIFY(!ownership.expired());
        carrier.clearVisual();
        QVERIFY(ownership.expired());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(first.isNull());
    }

    void windowAndSceneChangesRecreateOnlyNativeResources()
    {
        QQuickWindow firstWindow, secondWindow;
        prepareWindow(firstWindow);
        prepareWindow(secondWindow);
        auto* carrier = new TabCarrierItem(firstWindow.contentItem());
        TabDragVisual visual;
        visual.size = QSize(240, 60);
        visual.pieces.append(piece(QStringLiteral("Owned carrier العربية"), QPoint(8, 6), true));
        SurfaceGeometry geometry;
        geometry.anchor = QRectF(visual.pieces[0].frame->samples[0].parts.tab.translated(visual.pieces[0].offset));
        geometry.anchorPath.addRoundedRect(geometry.anchor, 6, 6);
        geometry.mergedPath = geometry.anchorPath;
        QVERIFY(carrier->setVisual(visual, &geometry));
        QPointer<TabLabelItem> label = labels(*carrier).front();
        firstWindow.show();
        QVERIFY(QTest::qWaitForWindowExposed(&firstWindow));
        QTRY_VERIFY(label->textItem()->renderStats().nativeTextBuilds > 0);
        const auto initial = label->textItem()->renderStats();

        visual.pieces[0].offset += QPoint(20, 10);
        geometry = translatedSurfaceGeometry(geometry, QPoint(20, 10));
        auto nextFrame = std::make_shared<TabCarrierFrame>(*visual.pieces[0].frame);
        nextFrame->samples[0].titleOpacity = 0.4;
        visual.pieces[0].frame = nextFrame;
        QVERIFY(carrier->setVisual(visual, &geometry));
        QSignalSpy moved(&firstWindow, &QQuickWindow::frameSwapped);
        firstWindow.requestUpdate();
        QTRY_VERIFY(!moved.isEmpty());
        QCOMPARE(label->textItem()->renderStats().textLayoutBuilds, initial.textLayoutBuilds);
        QCOMPARE(label->textItem()->renderStats().nativeTextBuilds, initial.nativeTextBuilds);

        carrier->setParent(secondWindow.contentItem());
        carrier->setParentItem(secondWindow.contentItem());
        secondWindow.show();
        QVERIFY(QTest::qWaitForWindowExposed(&secondWindow));
        QTRY_VERIFY(label->textItem()->renderStats().sceneGraphBuilds > initial.sceneGraphBuilds);
        const auto movedResources = label->textItem()->renderStats();
        QCOMPARE(movedResources.textLayoutBuilds, initial.textLayoutBuilds);
        secondWindow.hide();
        QTRY_VERIFY(!secondWindow.isExposed());
        secondWindow.releaseResources();
        secondWindow.show();
        QVERIFY(QTest::qWaitForWindowExposed(&secondWindow));
        QTRY_VERIFY(label->textItem()->renderStats().sceneGraphBuilds > movedResources.sceneGraphBuilds);
        QCOMPARE(label->textItem()->renderStats().textLayoutBuilds, initial.textLayoutBuilds);
        delete carrier;
        QVERIFY(label.isNull());
    }
};

QTEST_MAIN(QuickCarrierResourcesTest)
#include "quick_carrier_resources_test.moc"
