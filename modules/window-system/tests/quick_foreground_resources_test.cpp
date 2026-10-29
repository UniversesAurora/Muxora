// SPDX-License-Identifier: MIT
// Quick foreground: stable ownership through lift/settle, retained resources
// across motion and windows, and source-independent frame lifetime. No pixels.
#include "quick/tab_foreground_item.hpp"
#include "quick/tab_label_item.hpp"
#include "quick/tab_label_text_item.hpp"
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

namespace {
std::shared_ptr<TabForegroundFrame> frame()
{
    auto result = std::make_shared<TabForegroundFrame>();
    auto style = std::make_shared<TabStripLabelStyle>();
    style->palette.active = Qt::white;
    style->palette.text = Qt::black;
    result->style = style;
    SurfaceGeometry geometry;
    geometry.anchor = QRectF(50, 60, 180, 28);
    geometry.anchorPath.addRoundedRect(geometry.anchor, 6, 6);
    geometry.mergedPath = geometry.anchorPath;
    TabLabelPresentation label;
    label.parts.tab = geometry.anchor.toRect();
    label.parts.title = label.parts.tab.adjusted(16, 0, -16, 0);
    label.state.shape = TabShape::Row;
    label.state.title = QStringLiteral("Owned foreground title");
    label.state.selected = true;
    label.state.lifted = true;
    label.state.elevationManaged = true;
    label.surface = geometry;
    result->carried.append({QUuid::createUuid(), 0, {}, label});
    result->shadows.append({geometry, elevationProfile(ElevationLevel::Flat),
        elevationProfile(ElevationLevel::Lifted), {}, false});
    return result;
}

QList<TabLabelItem*> labels(TabForegroundItem& item)
{
    QList<TabLabelItem*> result;
    for (auto* child : item.childItems())
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

class QuickForegroundResourcesTest final : public QObject {
    Q_OBJECT
private slots:
    void retainedFrameSurvivesSourceReleaseAndReusesSettlingIdentity()
    {
        auto source = frame();
        std::weak_ptr<const TabForegroundFrame> lifetime = source;
        TabForegroundItem foreground;
        QVERIFY(foreground.setFrame(source));
        QPointer<TabLabelItem> label = labels(foreground).front();
        const auto shaping = label->textItem()->renderStats().textLayoutBuilds;
        auto next = std::make_shared<TabForegroundFrame>(*source);
        next->settling = std::move(next->carried);
        next->carried.clear();
        next->settling[0].label.presence = {0.8, 0.95, QPointF(7, 3)};
        source.reset();
        QVERIFY(!lifetime.expired());
        QVERIFY(foreground.setFrame(next));
        QVERIFY(lifetime.expired());
        QCOMPARE(labels(foreground).front(), label.data());
        QCOMPARE(label->textItem()->renderStats().textLayoutBuilds, shaping);
        foreground.clearFrame();
        QVERIFY(label.isNull());
    }

    void motionAndSceneChangesKeepOrRecreateTheRightResources()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*(ShaderEffect|QML|shader|texture).*")));
        QQuickWindow first, second;
        prepareWindow(first);
        prepareWindow(second);
        auto* foreground = new TabForegroundItem(first.contentItem());
        auto source = frame();
        QVERIFY(foreground->setFrame(source));
        QPointer<TabLabelItem> label = labels(*foreground).front();
        first.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QTRY_VERIFY(foreground->shadowStats().textureUploads > 0);
        QTRY_VERIFY(label->textItem()->renderStats().nativeTextBuilds > 0);
        const auto shadow = foreground->shadowStats();
        const auto text = label->textItem()->renderStats();

        auto next = std::make_shared<TabForegroundFrame>(*source);
        next->carried[0].label.presence = {0.6, 0.95, QPointF(8, 4)};
        next->shadows[0].presence = next->carried[0].label.presence;
        QVERIFY(foreground->setFrame(next));
        QSignalSpy moved(&first, &QQuickWindow::frameSwapped);
        first.requestUpdate();
        QTRY_VERIFY(!moved.isEmpty());
        QCOMPARE(foreground->shadowStats().textureUploads, shadow.textureUploads);
        QCOMPARE(label->textItem()->renderStats().nativeTextBuilds, text.nativeTextBuilds);
        foreground->setParent(second.contentItem());
        foreground->setParentItem(second.contentItem());
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(foreground->shadowStats().textureUploads > shadow.textureUploads);
        QTRY_VERIFY(label->textItem()->renderStats().sceneGraphBuilds > text.sceneGraphBuilds);
        const auto movedShadow = foreground->shadowStats();
        second.hide();
        QTRY_VERIFY(!second.isExposed());
        second.releaseResources();
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(foreground->shadowStats().textureUploads > movedShadow.textureUploads);
        QCOMPARE(label->textItem()->renderStats().textLayoutBuilds, text.textLayoutBuilds);
        delete foreground;
        QVERIFY(label.isNull());
    }
};

QTEST_MAIN(QuickForegroundResourcesTest)
#include "quick_foreground_resources_test.moc"
