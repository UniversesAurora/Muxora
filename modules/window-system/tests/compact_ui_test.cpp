// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "style/compact_tool_button.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleOptionSlider>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

using namespace window_system;

namespace {

class ApplicationPaletteGuard final {
public:
    explicit ApplicationPaletteGuard(bool dark) : previous_(qApp->palette())
    {
        auto palette = previous_;
        palette.setColor(QPalette::Window, dark ? QColor("#171717") : QColor("#eeeeee"));
        palette.setColor(QPalette::Base, dark ? QColor("#202020") : QColor("#ffffff"));
        palette.setColor(QPalette::WindowText, dark ? QColor("#f2f2f2") : QColor("#181818"));
        palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
        palette.setColor(QPalette::Button, palette.color(QPalette::Window));
        palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
        qApp->setPalette(palette);
    }
    ~ApplicationPaletteGuard() { qApp->setPalette(previous_); }
private:
    QPalette previous_;
};

void addSystemPalettes()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light-system-palette") << false;
    QTest::newRow("dark-system-palette") << true;
}

QRect boundsIn(QWidget* widget, QWidget* parent)
{
    return QRect(widget->mapTo(parent, QPoint()), widget->size());
}

void sendEnter(QWidget* widget)
{
    const QPoint point(20, 5);
    QEnterEvent event(point, widget->mapTo(widget->window(), point), widget->mapToGlobal(point));
    QCoreApplication::sendEvent(widget, &event);
}

} // namespace

class CompactUiTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void elasticSizingIsAdjacentToResetAndUsesTheHostCommand();
    void protectedSizingDoesNotAppearInEitherMenu();
    void scrollbarOverlaysTheTabsWithoutMovingTheContent_data();
    void scrollbarOverlaysTheTabsWithoutMovingTheContent();
    void groupButtonsStaySmallerThanTheTabs_data();
    void groupButtonsStaySmallerThanTheTabs();
    void compactTargetsSurviveThemeAndFontChanges_data();
    void compactTargetsSurviveThemeAndFontChanges();
    void compactPeekAndEdgeBarsKeepRealContent_data();
    void compactPeekAndEdgeBarsKeepRealContent();
    void groupSeparatorsRemainCompactAndResizable_data();
    void groupSeparatorsRemainCompactAndResizable();
    void pinnedGroupSeparatorsUseIndependentThemeMetric_data();
    void pinnedGroupSeparatorsUseIndependentThemeMetric();
};

void CompactUiTest::elasticSizingIsAdjacentToResetAndUsesTheHostCommand()
{
    CommandStripHost host;
    host.addStrip("tools", "Tools", new QLabel("Tools"), 0);
    int requests = 0;
    host.setSizingChangeRequest([&](const QString& id, CommandStripHost::StripSizing sizing) {
        QCOMPARE(id, QStringLiteral("tools"));
        ++requests;
        QVERIFY(host.setStripSizing(id, sizing));
    });
    std::unique_ptr<QMenu> menu(host.createOverflowMenu("tools", &host));
    const auto items = menu->actions();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items[0]->objectName(), QStringLiteral("StripResetWidthAction"));
    QCOMPARE(items[1]->objectName(), QStringLiteral("StripElasticAction"));
    QVERIFY(items[1]->isCheckable()); QVERIFY(!items[1]->isChecked());
    auto* customize = menu->findChild<QMenu*>("StripCustomizeMenu", Qt::FindDirectChildrenOnly);
    QVERIFY(customize); QCOMPARE(items[2]->menu(), customize);
    QVERIFY(!customize->findChild<QAction*>("StripElasticAction"));
    QCOMPARE(customize->actions().size(), 1);
    QCOMPARE(customize->actions().first()->data().toString(), QStringLiteral("content"));
    items[1]->trigger();
    QCOMPARE(requests, 1); QCOMPARE(host.stripSizing("tools"), CommandStripHost::StripSizing::Elastic);
    menu.reset(host.createOverflowMenu("tools", &host));
    auto* elastic = menu->findChild<QAction*>("StripElasticAction", Qt::FindDirectChildrenOnly);
    QVERIFY(elastic); QVERIFY(elastic->isChecked());
    elastic->trigger();
    QCOMPARE(requests, 2); QCOMPARE(host.stripSizing("tools"), CommandStripHost::StripSizing::Content);
}

void CompactUiTest::protectedSizingDoesNotAppearInEitherMenu()
{
    CommandStripHost host;
    host.addStrip("required", "Required", new QLabel("Title"), 0);
    host.addStrip("ordinary", "Ordinary", new QLabel("Tools"), 0);
    QVERIFY(host.setStripConstraints("required", {true, true, 96, true}));
    for (const auto& id : {"required", "ordinary"}) {
        std::unique_ptr<QMenu> menu(host.createOverflowMenu(id, &host));
        QVERIFY(menu); QCOMPARE(menu->actions().size(), 2);
        QCOMPARE(menu->actions().first()->objectName(), QStringLiteral("StripResetWidthAction"));
        QVERIFY(menu->actions().last()->menu());
        QVERIFY(!menu->findChild<QAction*>("StripElasticAction"));
    }
}

void CompactUiTest::scrollbarOverlaysTheTabsWithoutMovingTheContent_data() { addSystemPalettes(); }
void CompactUiTest::scrollbarOverlaysTheTabsWithoutMovingTheContent()
{
    QFETCH(bool, dark);
    ApplicationPaletteGuard palette(dark);
    QWidget root;
    auto* column = new QVBoxLayout(&root); column->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new GroupTabs(defaultTheme(), &root); column->addWidget(tabs);
    for (int index = 0; index < 12; ++index) {
        const auto id = QUuid::createUuid();
        tabs->addTab(new QLabel("Content"), {}, QString("Long tab %1").arg(index), id);
        TabRecord record; record.id = id; record.colorId = "red"; tabs->setTabMetadata(index, record);
    }
    applyTheme(&root); root.resize(380, 240); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    auto* header = tabs->findChild<QWidget*>("TabGroupHeader"); QVERIFY(header);
    auto* scroll = tabs->findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    auto* scrollbar = tabs->findChild<QScrollBar*>("TabScrollBar"); QVERIFY(scrollbar);
    auto* stack = tabs->findChild<QStackedWidget*>(); QVERIFY(stack);
    QTRY_VERIFY(scrollbar->maximum() > 0);
    QTest::mouseMove(&root, QPoint(root.width() - 1, root.height() - 1));
    QTRY_VERIFY(scrollbar->isHidden());
    const auto headerBefore = boundsIn(header, tabs);
    const auto contentBefore = boundsIn(stack, tabs);
    QCOMPARE(headerBefore.height(), tabs->tabBar()->height());
    QCOMPARE(contentBefore.top(), headerBefore.bottom() + 1);
    QCOMPARE(boundsIn(scroll->viewport(), tabs).bottom(), headerBefore.bottom());
    const auto unoccluded = tabs->grab().toImage();
    sendEnter(scroll->viewport());
    QVERIFY(scrollbar->isVisible());
    QCOMPARE(scrollbar->parentWidget(), scroll->viewport());
    QCOMPARE(scrollbar->height(), 4);
    QCOMPARE(scrollbar->geometry().bottom(), scroll->viewport()->rect().bottom());
    QCOMPARE(scrollbar->width(), scroll->viewport()->width());
    QCOMPARE(boundsIn(header, tabs), headerBefore); QCOMPARE(boundsIn(stack, tabs), contentBefore);
    QVERIFY(boundsIn(tabs->tabBar(), tabs).contains(boundsIn(scrollbar, tabs).topLeft()));

    QStyleOptionSlider option; option.initFrom(scrollbar); option.orientation = Qt::Horizontal;
    option.minimum = scrollbar->minimum(); option.maximum = scrollbar->maximum();
    option.pageStep = scrollbar->pageStep(); option.sliderPosition = scrollbar->value();
    const auto handle = scrollbar->style()->subControlRect(QStyle::CC_ScrollBar, &option,
        QStyle::SC_ScrollBarSlider, scrollbar);
    QVERIFY(!handle.isEmpty());
    const auto image = tabs->grab().toImage();
    const auto point = scrollbar->mapTo(tabs, QPoint(handle.center().x(), scrollbar->height() - 1));
    const auto ratio = image.devicePixelRatio();
    const auto under = unoccluded.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
    const auto composite = [&](QColor handleColor, int alpha) {
        QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied); pixel.fill(under);
        QPainter painter(&pixel); handleColor.setAlpha(alpha); painter.fillRect(pixel.rect(), handleColor); painter.end();
        return pixel.pixelColor(0, 0);
    };
    const auto normal = image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
    QCOMPARE(normal, composite(defaultTheme().border, 112));
    QVERIFY(normal != defaultTheme().border); QVERIFY(normal != under);
    // Moving the native cursor queues Enter on Windows. Wait for that input
    // precondition before checking pixels; a QPA move alone does not establish
    // the widget's native underMouse state.
    QCursor::setPos(scrollbar->mapToGlobal(handle.center()));
    QTRY_VERIFY(scrollbar->underMouse());
    const auto hovered = tabs->grab().toImage().pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
    QCOMPARE(hovered, composite(defaultTheme().accent, 176));
    QVERIFY(hovered != defaultTheme().accent); QVERIFY(hovered != normal);
    const auto selected = tabs->currentIndex();
    scrollbar->setValue(scrollbar->maximum() / 2);
    QCOMPARE(scroll->horizontalScrollBar()->value(), scrollbar->value()); QCOMPARE(tabs->currentIndex(), selected);
    QCOMPARE(boundsIn(stack, tabs), contentBefore);
    for (int width : {520, 280}) {
        root.resize(width, root.height());
        QCoreApplication::processEvents();
        QCOMPARE(scrollbar->geometry().bottom(), scroll->viewport()->rect().bottom());
        QCOMPARE(scrollbar->width(), scroll->viewport()->width());
        QCOMPARE(boundsIn(stack, tabs).top(), boundsIn(header, tabs).bottom() + 1);
    }
    tabs->setTabBarVisible(false);
    QVERIFY(scrollbar->isHidden()); QCOMPARE(stack->geometry().top(), 0);
    tabs->setTabBarVisible(true);
    QCOMPARE(boundsIn(stack, tabs).top(), boundsIn(header, tabs).bottom() + 1);
}

void CompactUiTest::groupButtonsStaySmallerThanTheTabs_data() { addSystemPalettes(); }
void CompactUiTest::groupButtonsStaySmallerThanTheTabs()
{
    QFETCH(bool, dark);
    ApplicationPaletteGuard palette(dark);
    GroupTabs tabs(defaultTheme(), nullptr);
    tabs.addTab(new QLabel("Content"), {}, "Session", QUuid::createUuid());
    int current = 0; int all = 0;
    tabs.setPinnedActions([&] { ++current; }, [&] { ++all; });
    applyTheme(&tabs); tabs.resize(380, 240); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* header = tabs.findChild<QWidget*>("TabGroupHeader"); QVERIFY(header);
    for (const auto& name : {"TabListButton", "UnpinCurrentButton", "UnpinAllButton"}) {
        auto* button = tabs.findChild<QToolButton*>(name); QVERIFY(button);
        const bool list = button->objectName() == "TabListButton";
        QCOMPARE(button->size(), list ? QSize(12, tabs.tabBar()->height()) : QSize(20, 20));
        QCOMPARE(button->iconSize(), list ? QSize(10, 14) : QSize(14, 14));
        if (list) QCOMPARE(button->height(), tabs.tabBar()->height());
        else QVERIFY(button->height() < tabs.tabBar()->height());
        QVERIFY(header->rect().contains(button->geometry()));
        QVERIFY(!button->icon().isNull()); QVERIFY(!button->accessibleName().isEmpty());
        QCOMPARE(button->grab().devicePixelRatio(), tabs.devicePixelRatioF());
    }
    auto* currentButton = tabs.findChild<QToolButton*>("UnpinCurrentButton");
    auto* allButton = tabs.findChild<QToolButton*>("UnpinAllButton");
    auto* listButton = tabs.findChild<QToolButton*>("TabListButton");
    QVERIFY(currentButton->geometry().right() < allButton->geometry().left());
    QVERIFY(allButton->geometry().right() < listButton->geometry().left());
    QTest::mouseClick(currentButton, Qt::LeftButton); QCOMPARE(current, 1); QCOMPARE(all, 0);
    QTest::mouseClick(allButton, Qt::LeftButton); QCOMPARE(current, 1); QCOMPARE(all, 1);
    QCOMPARE(tabs.currentIndex(), 0);
}

void CompactUiTest::compactTargetsSurviveThemeAndFontChanges_data() { addSystemPalettes(); }
void CompactUiTest::compactTargetsSurviveThemeAndFontChanges()
{
    QFETCH(bool, dark); ApplicationPaletteGuard palette(dark);
    QWidget root; auto* column = new QVBoxLayout(&root);
    QList<QToolButton*> buttons;
    for (const auto& name : {"TabListButton", "UnpinCurrentButton", "UnpinAllButton",
                           "PinPeekTabButton", "ClosePeekTabButton", "PinEdgeButton"}) {
        const QString objectName = QString::fromLatin1(name);
        const int extent = objectName == "PinEdgeButton" ? 22 : objectName.contains("PeekTab") ? 18 : 20;
        auto* button = new CompactToolButton(&root, extent); button->setObjectName(name);
        button->setIcon(iconFor("pin")); column->addWidget(button); buttons.append(button);
    }
    applyTheme(&root); root.resize(220, 260); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    const auto verifyTargets = [&] {
        for (auto* button : buttons) {
            const auto name = button->objectName();
            const int extent = name == "PinEdgeButton" ? 22 : name.contains("PeekTab") ? 18 : 20;
            const QSize target(extent, extent);
            QCOMPARE(button->minimumSize(), target); QCOMPARE(button->maximumSize(), target);
            QCOMPARE(button->size(), target); QCOMPARE(button->iconSize(), QSize(14, 14));
        }
    };
    verifyTargets();
    auto theme = defaultTheme(); theme.accent = QColor("#925720"); theme.border = QColor("#b69370");
    applyTheme(&root, theme); column->activate(); QCoreApplication::processEvents();
    verifyTargets();
    auto font = root.font(); font.setPointSizeF(std::max(qreal(12), font.pointSizeF()) + 3);
    root.setFont(font); column->activate(); QCoreApplication::processEvents();
    verifyTargets();
    root.setEnabled(false); QCoreApplication::processEvents(); verifyTargets();
}

void CompactUiTest::compactPeekAndEdgeBarsKeepRealContent_data()
{
    QTest::addColumn<int>("edgeValue");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}
void CompactUiTest::compactPeekAndEdgeBarsKeepRealContent()
{
    QFETCH(int, edgeValue); const auto edge = Edge(edgeValue);
    WindowManager manager; const auto id = manager.createWindow();
    const auto center = manager.addTab(id, new QLabel("Center"), {"Center", "label", {}});
    auto visibleGroup = [&](TabId target) -> GroupTabs* {
        for (auto* widget : manager.windowWidget(id)->findChildren<QWidget*>())
            if (auto* group = dynamic_cast<GroupTabs*>(widget); group && group->isVisible()
                && group->property("groupId").toUuid() == manager.model().groupForTab(target)) return group;
        return nullptr;
    };
    manager.showWindow(id);
    auto* central = visibleGroup(center); QVERIFY(central);
    const int originalTabHeight = central->tabBar()->height();
    auto* content = new QLabel("Live side content");
    const QColor contentColor("#19ab7c");
    content->setStyleSheet(QStringLiteral("background: %1;").arg(contentColor.name()));
    const auto tab = manager.addTab(id, content, {"Side", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(manager.moveTabToEdge(tab, edge));
    manager.showWindow(id); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto edgeRect = window->surface()->edgeBarRects().value(edge);
    QVERIFY(!edgeRect.isEmpty());
    QCOMPARE(manager.theme().metrics.edgeThickness, 22);
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    QCOMPARE(vertical ? edgeRect.width() : edgeRect.height(), 22);
    auto* pinEdge = window->findChild<QToolButton*>("PinEdgeButton"); QVERIFY(pinEdge);
    QCOMPARE(pinEdge->size(), QSize(22, 22));
    QVERIFY(manager.activateTab(tab));
    auto* header = window->findChild<QWidget*>("PeekTitleBar"); QVERIFY(header); QVERIFY(header->isVisible());
    QCOMPARE(header->height(), 20);
    QCOMPARE(header->minimumHeight(), 20); QCOMPARE(header->maximumHeight(), 20);
    central = visibleGroup(center); QVERIFY(central);
    QCOMPARE(central->tabBar()->height(), originalTabHeight);
    for (const auto& name : {"PinPeekTabButton", "ClosePeekTabButton"}) {
        auto* button = header->findChild<QToolButton*>(name); QVERIFY(button);
        QCOMPARE(button->size(), QSize(18, 18)); QCOMPARE(button->iconSize(), QSize(14, 14));
        QVERIFY(header->rect().contains(button->geometry()));
        QVERIFY(!button->icon().isNull()); QVERIFY(!button->accessibleName().isEmpty());
    }
    const auto originalFont = window->font();
    auto largerFont = originalFont; largerFont.setPointSizeF(std::max(qreal(12), originalFont.pointSizeF() + 3));
    window->setFont(largerFont); QCoreApplication::processEvents(); header->layout()->activate();
    QCOMPARE(header->height(), std::max(20, header->fontMetrics().height() + 2));
    auto* title = header->findChild<QLabel*>(); QVERIFY(title);
    QVERIFY(title->height() >= title->fontMetrics().height());
    for (const auto& name : {"PinPeekTabButton", "ClosePeekTabButton"}) {
        auto* button = header->findChild<QToolButton*>(name); QVERIFY(button);
        QCOMPARE(button->size(), QSize(18, 18)); QVERIFY(header->rect().contains(button->geometry()));
    }
    window->setFont(originalFont); applyTheme(window, manager.theme()); QCoreApplication::processEvents();
    header->layout()->activate(); QCOMPARE(header->height(), 20);
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(content->isVisible());
    const auto image = window->grab().toImage(); const auto point = content->mapTo(window, QPoint(4, 4));
    QCOMPARE(image.pixelColor(qRound(point.x() * image.devicePixelRatio()), qRound(point.y() * image.devicePixelRatio())).rgb(),
        contentColor.rgb());
    const auto screenshots = qEnvironmentVariable("WINDOW_SYSTEM_SCREENSHOT_DIR");
    if (!screenshots.isEmpty()) {
        QVERIFY(QDir().mkpath(screenshots));
        auto* peek = window->findChild<QWidget*>("PeekPanel"); QVERIFY(peek);
        QVERIFY(peek->grab().save(QDir(screenshots).filePath(QString("peek-compact-%1.png").arg(edgeValue))));
    }
    QTest::mouseClick(header->findChild<QToolButton*>("PinPeekTabButton"), Qt::LeftButton);
    QCOMPARE(manager.model().group(manager.model().groupForTab(tab))->location, GroupLocation::Pinned);
    QCOMPARE(manager.paneWidget(pane), content);
    auto* pinned = visibleGroup(tab); QVERIFY(pinned);
    QCOMPARE(pinned->tabBar()->height(), originalTabHeight);
    for (const auto& name : {"UnpinCurrentButton", "UnpinAllButton", "TabListButton"}) {
        auto* button = pinned->findChild<QToolButton*>(name); QVERIFY(button);
        const bool list = button->objectName() == "TabListButton";
        QCOMPARE(button->size(), list ? QSize(12, pinned->tabBar()->height()) : QSize(20, 20));
        QCOMPARE(button->iconSize(), list ? QSize(10, 14) : QSize(14, 14));
    }
}

void CompactUiTest::groupSeparatorsRemainCompactAndResizable_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::newRow("side-by-side") << false;
    QTest::newRow("stacked") << true;
}

void CompactUiTest::groupSeparatorsRemainCompactAndResizable()
{
    QFETCH(bool, vertical);
    WindowManager manager;
    const auto windowId = manager.createWindow();
    auto* firstContent = new QLabel("First");
    const auto first = manager.addTab(windowId, firstContent, {"First", "label", {}});
    const auto firstPane = manager.model().tab(first)->activePane;
    auto* paneContent = new QLabel("Pane");
    QVERIFY(!manager.splitPane(first, firstPane, paneContent, Placement::Right).isNull());
    const auto second = manager.addTab(windowId, new QLabel("Second"), {"Second", "label", {}});
    QVERIFY(manager.moveTab(second, manager.model().groupForTab(first), vertical ? Placement::Bottom : Placement::Right));
    manager.showWindow(windowId);
    auto* window = manager.windowWidget(windowId);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto* splitter = window->findChild<QSplitter*>("GroupSplitter"); QVERIFY(splitter);
    auto* paneSplitter = window->findChild<QSplitter*>("PaneSplitter"); QVERIFY(paneSplitter);
    QCOMPARE(splitter->orientation(), vertical ? Qt::Vertical : Qt::Horizontal);
    const auto gap = [vertical](QSplitter* target) {
        const auto firstRect = target->widget(0)->geometry();
        const auto secondRect = target->widget(1)->geometry();
        return vertical ? secondRect.top() - firstRect.bottom() - 1 : secondRect.left() - firstRect.right() - 1;
    };
    QCOMPARE(splitter->handleWidth(), 2);
    QCOMPARE(gap(splitter), 2);
    QCOMPARE(paneSplitter->handleWidth(), 1);
    QCOMPARE(paneSplitter->widget(1)->x() - paneSplitter->widget(0)->geometry().right() - 1, 1);
    const auto node = manager.model().window(windowId)->central->id;
    const auto initialRatio = manager.model().window(windowId)->central->ratio;
    auto* handle = splitter->handle(1); QVERIFY(handle);
    QSignalSpy moves(splitter, &QSplitter::splitterMoved);
    const auto start = handle->rect().center();
    const auto delta = vertical ? QPoint(0, 45) : QPoint(45, 0);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(handle, start + delta);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, start + delta);
    QTRY_VERIFY(!moves.isEmpty());
    QCOMPARE(manager.model().window(windowId)->central->id, node);
    QVERIFY(manager.model().window(windowId)->central->ratio > initialRatio);
    QCOMPARE(gap(splitter), 2);
    QCOMPARE(manager.paneWidget(firstPane), firstContent);
    QVERIFY(paneContent->isVisible());
    window->resize(window->width() - 100, window->height() - 80);
    QCoreApplication::processEvents();
    QCOMPARE(gap(splitter), 2);
    QCOMPARE(paneSplitter->handleWidth(), 1);
    const auto paneNode = manager.model().tab(first)->panes->id;
    const auto paneRatio = manager.model().tab(first)->panes->ratio;
    auto* paneHandle = paneSplitter->handle(1); QVERIFY(paneHandle);
    QSignalSpy paneMoves(paneSplitter, &QSplitter::splitterMoved);
    const auto paneStart = paneHandle->rect().center();
    QTest::mousePress(paneHandle, Qt::LeftButton, Qt::NoModifier, paneStart);
    QTest::mouseMove(paneHandle, paneStart + QPoint(30, 0));
    QTest::mouseRelease(paneHandle, Qt::LeftButton, Qt::NoModifier, paneStart + QPoint(30, 0));
    QTRY_VERIFY(!paneMoves.isEmpty());
    QCOMPARE(manager.model().tab(first)->panes->id, paneNode);
    QVERIFY(manager.model().tab(first)->panes->ratio > paneRatio);
    QCOMPARE(manager.paneWidget(firstPane), firstContent);
    auto customTheme = manager.theme(); customTheme.metrics.splitterWidth = 3;
    manager.setTheme(customTheme); QCoreApplication::processEvents();
    QCOMPARE(paneSplitter->handleWidth(), 3);
    QCOMPARE(paneSplitter->widget(1)->x() - paneSplitter->widget(0)->geometry().right() - 1, 3);
    QCOMPARE(gap(splitter), 2);
    QVERIFY(manager.model().validate());
}

void CompactUiTest::pinnedGroupSeparatorsUseIndependentThemeMetric_data()
{
    QTest::addColumn<int>("edgeValue");
    for (int edge = 0; edge < 4; ++edge) QTest::newRow(qPrintable(QString("edge-%1").arg(edge))) << edge;
}

void CompactUiTest::pinnedGroupSeparatorsUseIndependentThemeMetric()
{
    QFETCH(int, edgeValue);
    WindowManager manager;
    const auto windowId = manager.createWindow();
    manager.addTab(windowId, new QLabel("Central"), {"Central", "label", {}});
    const auto first = manager.addTab(windowId, new QLabel("First"), {"First", "label", {}});
    const auto second = manager.addTab(windowId, new QLabel("Second"), {"Second", "label", {}});
    QVERIFY(manager.moveTabToEdge(first, Edge(edgeValue), true));
    QVERIFY(manager.moveTabToEdge(second, Edge(edgeValue), true));
    manager.showWindow(windowId);
    auto* window = manager.windowWidget(windowId);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto* splitter = window->findChild<QSplitter*>("PinnedRegion"); QVERIFY(splitter);
    auto* outer = qobject_cast<QSplitter*>(splitter->parentWidget()); QVERIFY(outer);
    QCOMPARE(outer->handleWidth(), 2);
    const auto outerGap = [outer] {
        const auto first = outer->widget(0)->geometry(), second = outer->widget(1)->geometry();
        return outer->orientation() == Qt::Vertical ? second.top() - first.bottom() - 1 : second.left() - first.right() - 1;
    };
    QCOMPARE(outerGap(), 2);
    QCOMPARE(splitter->count(), 2);
    const auto gap = [splitter] {
        const auto firstRect = splitter->widget(0)->geometry();
        const auto secondRect = splitter->widget(1)->geometry();
        return splitter->orientation() == Qt::Vertical
            ? secondRect.top() - firstRect.bottom() - 1 : secondRect.left() - firstRect.right() - 1;
    };
    QCOMPARE(splitter->handleWidth(), 2);
    QCOMPARE(gap(), 2);
    for (int requestedWidth : {5, 0, 2}) {
        auto theme = manager.theme(); theme.metrics.groupSplitterWidth = requestedWidth;
        manager.setTheme(theme);
        QCoreApplication::processEvents();
        QCOMPARE(splitter->handleWidth(), std::max(1, requestedWidth));
        QCOMPARE(gap(), std::max(1, requestedWidth));
        QCOMPARE(outer->handleWidth(), std::max(1, requestedWidth));
        QCOMPARE(outerGap(), std::max(1, requestedWidth));
    }
    QVERIFY(manager.model().validate());
}

QTEST_MAIN(CompactUiTest)
#include "compact_ui_test.moc"
