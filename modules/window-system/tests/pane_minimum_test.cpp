// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>

#include "panes/tab_content.hpp"

#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QScreen>
#include <QPointer>
#include <QResizeEvent>
#include <QSplitter>
#include <QTest>
#include <functional>
#include <memory>
#include <utility>

using namespace window_system;

namespace {
class MinimumContent final : public QWidget {
public:
    MinimumContent(QSize explicitMinimum, QSize hintedMinimum = {}) : hint_(hintedMinimum)
    {
        setMinimumSize(explicitMinimum.expandedTo(QSize(0, 0)));
    }
    QSize minimumSizeHint() const override { return hint_; }
    QSize sizeHint() const override { return hint_.expandedTo(QSize(240, 180)); }
    void setHint(QSize size) { hint_ = size; updateGeometry(); }
    std::function<void()> resized;
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (resized) { const auto callback = std::exchange(resized, {}); callback(); }
    }
private:
    QSize hint_;
};

QWidget* paneFrame(QWidget* content)
{
    for (auto* parent = content->parentWidget(); parent; parent = parent->parentWidget())
        if (parent->objectName() == "PaneHost") return parent;
    return nullptr;
}

TabContent* tabPage(QWidget* content)
{
    for (auto* parent = content->parentWidget(); parent; parent = parent->parentWidget())
        if (auto* page = dynamic_cast<TabContent*>(parent)) return page;
    return nullptr;
}

void verifyContentFits(QWidget* content, QSize minimum)
{
    auto* frame = paneFrame(content); QVERIFY(frame);
    QVERIFY(frame->width() >= minimum.width()); QVERIFY(frame->height() >= minimum.height());
    QVERIFY(frame->rect().contains(content->geometry()));
    QVERIFY(content->width() >= minimum.width()); QVERIFY(content->height() >= minimum.height());
}
}

class PaneMinimumTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void splittersAndWindowHonorContentMinimums_data();
    void splittersAndWindowHonorContentMinimums();
    void nestedSplittersPropagateBothAxes();
    void fallbackDoesNotOverrideExplicitOrHintedSmallMinimums();
    void dynamicMinimumChangesPropagateWithoutRebuildingContent();
    void sharedAndPerPaneBarsContributeTheirOwnMinimums();
    void initialPlacementDoesNotLowerContentMinimum();
    void sharingResizeMayDestroyTheManager();
};

void PaneMinimumTest::splittersAndWindowHonorContentMinimums_data()
{
    QTest::addColumn<int>("placement");
    QTest::addColumn<bool>("hintOnly");
    for (auto direction : {Placement::Left, Placement::Right, Placement::Top, Placement::Bottom})
        for (bool hintOnly : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(int(direction)).arg(hintOnly)))
                << int(direction) << hintOnly;
}

void PaneMinimumTest::splittersAndWindowHonorContentMinimums()
{
    QFETCH(int, placement); QFETCH(bool, hintOnly);
    WindowManager manager; const auto window = manager.createWindow();
    const QSize firstMinimum(210, 170), secondMinimum(160, 130);
    auto* first = new MinimumContent(hintOnly ? QSize(0, 0) : firstMinimum, hintOnly ? firstMinimum : QSize());
    auto* second = new MinimumContent(hintOnly ? QSize(0, 0) : secondMinimum, hintOnly ? secondMinimum : QSize());
    const auto tab = manager.addTab(window, first, {"Content minimum", "test", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(!manager.splitPane(tab, pane, second, Placement(placement)).isNull());
    manager.showWindow(window); auto* widget = manager.windowWidget(window);
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    auto* splitter = widget->findChild<QSplitter*>("PaneSplitter"); QVERIFY(splitter);
    const bool horizontal = placement == int(Placement::Left) || placement == int(Placement::Right);
    const QSize expected(horizontal ? firstMinimum.width() + secondMinimum.width() + splitter->handleWidth()
                                    : qMax(firstMinimum.width(), secondMinimum.width()),
                         horizontal ? qMax(firstMinimum.height(), secondMinimum.height())
                                    : firstMinimum.height() + secondMinimum.height() + splitter->handleWidth());
    auto* page = tabPage(first); QVERIFY(page);
    QTRY_COMPARE(page->minimumSizeHint(), expected);
    QTRY_VERIFY(widget->minimumWidth() >= expected.width());
    QTRY_VERIFY(widget->minimumHeight() >= expected.height());
    widget->resize(1, 1);
    QCoreApplication::processEvents();
    QVERIFY(widget->width() >= expected.width()); QVERIFY(widget->height() >= expected.height());
    for (const auto sizes : {QList<int>{1, 10000}, QList<int>{10000, 1}}) {
        splitter->setSizes(sizes); QCoreApplication::processEvents();
        verifyContentFits(first, firstMinimum); verifyContentFits(second, secondMinimum);
        const auto firstBounds = QRect(first->mapTo(page, QPoint()), first->size());
        const auto secondBounds = QRect(second->mapTo(page, QPoint()), second->size());
        QVERIFY(!firstBounds.intersects(secondBounds));
    }
}

void PaneMinimumTest::nestedSplittersPropagateBothAxes()
{
    WindowManager manager; const auto window = manager.createWindow();
    auto* first = new MinimumContent({190, 120});
    const auto tab = manager.addTab(window, first, {"Nested", "test", {}});
    const auto firstPane = manager.model().tab(tab)->activePane;
    auto* second = new MinimumContent({170, 140});
    const auto secondPane = manager.splitPane(tab, firstPane, second, Placement::Right); QVERIFY(!secondPane.isNull());
    auto* third = new MinimumContent({220, 100});
    QVERIFY(!manager.splitPane(tab, secondPane, third, Placement::Bottom).isNull());
    manager.showWindow(window); auto* widget = manager.windowWidget(window);
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    const int separator = qMax(1, manager.theme().metrics.splitterWidth);
    const QSize expected(190 + separator + 220, 140 + separator + 100);
    auto* page = tabPage(first); QVERIFY(page);
    QTRY_COMPARE(page->minimumSizeHint(), expected);
    widget->resize(1, 1); QCoreApplication::processEvents();
    verifyContentFits(first, {190, 120}); verifyContentFits(second, {170, 140}); verifyContentFits(third, {220, 100});
    const auto contents = QList<QWidget*>{first, second, third};
    for (int i = 0; i < contents.size(); ++i) for (int j = i + 1; j < contents.size(); ++j)
        QVERIFY(!QRect(contents[i]->mapTo(page, QPoint()), contents[i]->size())
            .intersects(QRect(contents[j]->mapTo(page, QPoint()), contents[j]->size())));
}

void PaneMinimumTest::fallbackDoesNotOverrideExplicitOrHintedSmallMinimums()
{
    WindowManager manager; const auto window = manager.createWindow();
    auto* content = new MinimumContent({0, 0});
    manager.addTab(window, content, {"Fallback", "test", {}});
    auto* frame = paneFrame(content); QVERIFY(frame);
    QCOMPARE(frame->minimumSizeHint(), QSize(140, 85));
    content->setMinimumSize(28, 19); QCoreApplication::processEvents();
    QCOMPARE(frame->minimumSizeHint(), QSize(28, 19));
    content->setMinimumSize(0, 0); content->setHint({33, 22}); QCoreApplication::processEvents();
    QCOMPARE(frame->minimumSizeHint(), QSize(33, 22));
}

void PaneMinimumTest::dynamicMinimumChangesPropagateWithoutRebuildingContent()
{
    WindowManager manager; const auto window = manager.createWindow();
    auto* first = new MinimumContent({0, 0}, {170, 95});
    const auto tab = manager.addTab(window, first, {"Dynamic", "test", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* second = new MinimumContent({130, 90});
    QVERIFY(!manager.splitPane(tab, pane, second, Placement::Right).isNull());
    manager.showWindow(window); auto* widget = manager.windowWidget(window);
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    const auto frame = paneFrame(first); const auto page = tabPage(first); QVERIFY(frame && page);
    const int separator = qMax(1, manager.theme().metrics.splitterWidth);
    first->setHint({390, 210});
    QTRY_COMPARE(page->minimumSizeHint(), QSize(390 + separator + 130, 210));
    QTRY_VERIFY(widget->minimumWidth() >= 390 + separator + 130);
    widget->resize(1, 1); QCoreApplication::processEvents();
    verifyContentFits(first, {390, 210}); verifyContentFits(second, {130, 90});
    first->setMinimumSize(450, 240);
    QTRY_COMPARE(page->minimumSizeHint(), QSize(450 + separator + 130, 240));
    QTRY_VERIFY(widget->minimumWidth() >= 450 + separator + 130);
    const int expandedMinimum = widget->minimumWidth();
    first->setMinimumSize(45, 30); first->setHint({45, 30});
    QTRY_COMPARE(page->minimumSizeHint(), QSize(45 + separator + 130, 90));
    QTRY_VERIFY(widget->minimumWidth() < expandedMinimum);
    QCOMPARE(paneFrame(first), frame); QCOMPARE(tabPage(first), page);
    QCOMPARE(manager.paneWidget(pane), first);
}

void PaneMinimumTest::sharedAndPerPaneBarsContributeTheirOwnMinimums()
{
    WindowManager manager; const auto window = manager.createWindow();
    auto* first = new MinimumContent({120, 100});
    const auto tab = manager.addTab(window, first, {"Bars", "test", {}});
    const auto firstPane = manager.model().tab(tab)->activePane;
    auto* second = new MinimumContent({90, 80});
    const auto secondPane = manager.splitPane(tab, firstPane, second, Placement::Right); QVERIFY(!secondPane.isNull());
    auto* top = new QWidget; top->setMinimumSize(300, 24);
    auto* bottom = new QWidget; bottom->setMinimumSize(200, 17);
    QVERIFY(manager.setPaneTopBar(firstPane, top)); QVERIFY(manager.setPaneBottomBar(firstPane, bottom));
    QVERIFY(manager.focusPane(tab, firstPane));
    manager.showWindow(window); auto* widget = manager.windowWidget(window);
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    const int separator = qMax(1, manager.theme().metrics.splitterWidth);
    auto* page = tabPage(first); QVERIFY(page);
    QTRY_COMPARE(page->minimumSizeHint(), QSize(300, 100 + 24 + 17));
    QVERIFY(manager.setTabTopBarSharing(tab, PaneBarSharing::PerPane));
    QVERIFY(manager.setTabBottomBarSharing(tab, PaneBarSharing::PerPane));
    page = tabPage(first); QVERIFY(page);
    QTRY_COMPARE(page->minimumSizeHint(), QSize(300 + separator + 90, 100 + 24 + 17));
    widget->resize(1, 1); QCoreApplication::processEvents();
    verifyContentFits(first, {120, 100}); verifyContentFits(second, {90, 80});
    const auto* frame = paneFrame(first);
    QCOMPARE(top->parentWidget(), frame); QCOMPARE(bottom->parentWidget(), frame);
    const auto bounds = [](const QWidget* value) {
        const auto geometry = value->geometry();
        return QStringLiteral("(%1,%2 %3x%4) min=%5x%6 hint=%7x%8 max=%9x%10")
            .arg(geometry.x()).arg(geometry.y()).arg(geometry.width()).arg(geometry.height())
            .arg(value->minimumWidth()).arg(value->minimumHeight())
            .arg(value->minimumSizeHint().width()).arg(value->minimumSizeHint().height())
            .arg(value->maximumWidth()).arg(value->maximumHeight());
    };
    const auto diagnostic = QStringLiteral("frame %1; top %2; bottom %3; page %4; window %5")
        .arg(bounds(frame), bounds(top), bounds(bottom), bounds(page), bounds(widget));
    QStringList chain;
    for (QWidget* parent = page; parent; parent = parent->parentWidget()) {
        const auto minimum = parent->layout() ? parent->layout()->minimumSize() : QSize();
        chain.append(QStringLiteral("%1/%2 %3 layoutMin=%4x%5 policy=%6/%7 hidden=%8")
            .arg(parent->metaObject()->className(), parent->objectName(), bounds(parent))
            .arg(minimum.width()).arg(minimum.height())
            .arg(int(parent->sizePolicy().horizontalPolicy())).arg(int(parent->sizePolicy().verticalPolicy()))
            .arg(parent->isHidden()));
    }
    QVERIFY2(frame->rect().contains(top->geometry()), qPrintable(diagnostic + "\n" + chain.join('\n')));
    QVERIFY2(frame->rect().contains(bottom->geometry()), qPrintable(diagnostic));
    top->setMinimumWidth(420);
    QTRY_COMPARE(page->minimumSizeHint().width(), 420 + separator + 90);
    QTRY_VERIFY(widget->minimumWidth() >= 420 + separator + 90);
    const auto perPaneWindowMinimum = widget->minimumWidth();
    QVERIFY(manager.setTabTopBarSharing(tab, PaneBarSharing::Shared));
    QVERIFY(manager.setTabBottomBarSharing(tab, PaneBarSharing::Shared));
    QTRY_COMPARE(page->minimumSizeHint(), QSize(420, 100 + 24 + 17));
    widget->resize(1, 1); QCoreApplication::processEvents();
    QCOMPARE(top->parentWidget(), page); QCOMPARE(bottom->parentWidget(), page);
    QVERIFY(page->rect().contains(top->geometry())); QVERIFY(page->rect().contains(bottom->geometry()));
    verifyContentFits(first, {120, 100}); verifyContentFits(second, {90, 80});
    QVERIFY(widget->minimumWidth() < perPaneWindowMinimum);
}

void PaneMinimumTest::initialPlacementDoesNotLowerContentMinimum()
{
    WindowManager manager; const auto window = manager.createWindow();
    const QSize minimum(qApp->primaryScreen()->availableGeometry().width() + 80, 160);
    auto* content = new MinimumContent(minimum);
    const auto tab = manager.addTab(window, content, {"Oversized content", "test", {}});
    manager.showWindow(window); auto* widget = manager.windowWidget(window);
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    QVERIFY(widget->minimumWidth() >= minimum.width());
    QVERIFY(widget->width() >= minimum.width());
    verifyContentFits(content, minimum);
    widget->resize(1, 1); QCoreApplication::processEvents();
    QVERIFY(widget->width() >= minimum.width());
    verifyContentFits(content, minimum);
    QCOMPARE(manager.paneWidget(manager.model().tab(tab)->activePane), content);
}

void PaneMinimumTest::sharingResizeMayDestroyTheManager()
{
    auto manager = std::make_unique<WindowManager>();
    const QPointer<WindowManager> owner(manager.get());
    const auto window = manager->createWindow();
    auto* first = new MinimumContent({120, 100}); QPointer<QWidget> content(first);
    const auto tab = manager->addTab(window, first, {"Resize lifetime", "test", {}});
    const auto pane = manager->model().tab(tab)->activePane;
    QVERIFY(!manager->splitPane(tab, pane, new MinimumContent({90, 80}), Placement::Right).isNull());
    auto* top = new QWidget; top->setMinimumSize(300, 24);
    QVERIFY(manager->setPaneTopBar(pane, top));
    QVERIFY(manager->focusPane(tab, pane));
    manager->showWindow(window); QPointer<QWidget> widget(manager->windowWidget(window));
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    widget->resize(1, 1); QCoreApplication::processEvents();
    bool invoked = false, aliveInCallback = false;
    first->resized = [&] {
        invoked = true; manager.reset(); aliveInCallback = widget && content;
    };
    QVERIFY(!manager->setTabTopBarSharing(tab, PaneBarSharing::PerPane));
    QVERIFY(invoked); QVERIFY(aliveInCallback); QVERIFY(!owner);
    QVERIFY(widget); QVERIFY(content);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!widget); QVERIFY(!content);
}

QTEST_MAIN(PaneMinimumTest)
#include "pane_minimum_test.moc"
