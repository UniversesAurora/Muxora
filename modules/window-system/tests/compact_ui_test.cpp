// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "style/compact_tool_button.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
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
    for (int index = 0; index < 12; ++index)
        tabs->addTab(new QLabel("Content"), {}, QString("Long tab %1").arg(index), QUuid::createUuid());
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
    const auto point = scrollbar->mapTo(tabs, handle.center());
    const auto ratio = image.devicePixelRatio();
    QCOMPARE(image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio)).rgb(), QColor("#9C8FB0").rgb());
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
        QCOMPARE(button->size(), QSize(20, 20)); QCOMPARE(button->iconSize(), QSize(14, 14));
        QVERIFY(button->height() < tabs.tabBar()->height());
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
        const int extent = QString::fromLatin1(name) == "PinEdgeButton" ? 22 : 20;
        auto* button = new CompactToolButton(&root, extent); button->setObjectName(name);
        button->setIcon(iconFor("pin")); column->addWidget(button); buttons.append(button);
    }
    applyTheme(&root); root.resize(220, 260); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    const auto verifyTargets = [&] {
        for (auto* button : buttons) {
            const int extent = button->objectName() == "PinEdgeButton" ? 22 : 20;
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
    manager.addTab(id, new QLabel("Center"), {"Center", "label", {}});
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
    QCOMPARE(header->height(), 24);
    for (const auto& name : {"PinPeekTabButton", "ClosePeekTabButton"}) {
        auto* button = header->findChild<QToolButton*>(name); QVERIFY(button);
        QCOMPARE(button->size(), QSize(20, 20)); QCOMPARE(button->iconSize(), QSize(14, 14));
        QVERIFY(header->rect().contains(button->geometry()));
        QVERIFY(!button->icon().isNull()); QVERIFY(!button->accessibleName().isEmpty());
    }
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(content->isVisible());
    const auto image = window->grab().toImage(); const auto point = content->mapTo(window, QPoint(4, 4));
    QCOMPARE(image.pixelColor(qRound(point.x() * image.devicePixelRatio()), qRound(point.y() * image.devicePixelRatio())).rgb(),
        contentColor.rgb());
    QTest::mouseClick(header->findChild<QToolButton*>("PinPeekTabButton"), Qt::LeftButton);
    QCOMPARE(manager.model().group(manager.model().groupForTab(tab))->location, GroupLocation::Pinned);
    QCOMPARE(manager.paneWidget(pane), content);
}

QTEST_MAIN(CompactUiTest)
#include "compact_ui_test.moc"
