// SPDX-License-Identifier: MIT
#include "docking/tab_strip.hpp"
#include "commands/popup_menu.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/edge_tab_button.hpp"
#include "authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QContextMenuEvent>
#include <QDebug>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProxyStyle>
#include <QScreen>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QStyleOptionTab>
#include <QTest>
#include <QToolButton>

using namespace window_system;

namespace {
class CapturingTabShapeStyle final : public QProxyStyle {
public:
    CapturingTabShapeStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}
    mutable QHash<int, QStyleOptionTab> tabs;
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override
    {
        if (element == CE_TabBarTabShape)
            if (const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option)) tabs.insert(tab->tabIndex, *tab);
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

QVector<TabId> populate(GroupTabs& tabs, int count, bool withIcon = false)
{
    QVector<TabId> ids;
    QPixmap icon(16, 16); icon.fill(Qt::white);
    for (int index = 0; index < count; ++index) {
        const auto id = QUuid::createUuid(); ids.append(id);
        const auto title = QStringLiteral("MMMM tab %1").arg(index + 1);
        tabs.addTab(new QLabel(title), withIcon ? QIcon(icon) : QIcon{}, title, id);
        TabRecord record; record.id = id; record.title = title;
        tabs.setTabMetadata(index, record);
    }
    applyTheme(&tabs); return ids;
}
QColor sample(QWidget* widget, QPoint logical)
{
    const auto image = widget->grab().toImage(); const auto ratio = image.devicePixelRatio();
    return image.pixelColor(qRound(logical.x() * ratio), qRound(logical.y() * ratio));
}
bool containsColor(const QImage& image, QColor color)
{
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        if (image.pixelColor(x, y).rgb() == color.rgb()) return true;
    return false;
}

QImage renderAtRatio(QWidget* widget, qreal ratio)
{
    QImage image(QSize(qRound(widget->width() * ratio), qRound(widget->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio); image.fill(Qt::transparent);
    QPainter painter(&image); widget->render(&painter); painter.end(); return image;
}

QImage logicalCrop(const QImage& image, QRect rect)
{
    const auto ratio = image.devicePixelRatio();
    return image.copy(QRect(qRound(rect.x() * ratio), qRound(rect.y() * ratio),
        qRound(rect.width() * ratio), qRound(rect.height() * ratio)));
}

GroupTabs* groupView(QWidget* window, GroupId id)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget);
            group && group->isVisible() && group->property("groupId").toUuid() == id) return group;
    return nullptr;
}

QWidget* labelPreview()
{
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "DragLabelPreview") return widget;
    return nullptr;
}

void sendMouse(QWidget* receiver, QEvent::Type type, QPoint global,
               Qt::MouseButton button = Qt::NoButton, Qt::MouseButtons buttons = Qt::LeftButton)
{
    test::authoredMouseInput().takePriority();
    QMouseEvent event(type, receiver->mapFromGlobal(global), global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(receiver, &event);
    test::authoredMouseInput().takePriority();
    QApplication::processEvents();
}
}

class TabStripLayoutTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void multilineWrapsWithoutHorizontalScrollAndKeepsEveryTab();
    void multilineHeaderActionsReserveOnlyTheLastRow_data();
    void multilineHeaderActionsReserveOnlyTheLastRow();
    void wheelExpansionShowsEveryRowThenOnlyActiveRow();
    void placementIsIndependentForCentralAndPinnedGroups();
    void displayFlagsChangeActualLabelWidthsAndCreationControls();
    void titleClippingKeepsFullTextAndDecorationSpace_data();
    void titleClippingKeepsFullTextAndDecorationSpace();
    void zeroTitleLimitsKeepLabelsAndDecorationSpace();
    void zeroTitleLeavesOneGapBetweenIconAndClose();
    void zeroTitleRailLabelKeepsEqualEndPadding_data();
    void zeroTitleRailLabelKeepsEqualEndPadding();
    void closeTargetShowsHoverAndPressFeedback();
    void narrowMultilineTitlesUseHardClippingWithoutAWidthLimit();
    void tabShapeOptionsPreserveStyleStates_data();
    void tabShapeOptionsPreserveStyleStates();
    void paletteRendersSelectedAndUnselectedStatesAndActiveIcon();
    void newTabButtonsHaveGapAndBothOpenTypeMenuOnContextPress();
    void insertionPreviewAndHitTestingUseThePointerRow_data();
    void insertionPreviewAndHitTestingUseThePointerRow();
    void oversizedInsertionPreviewRemainsOneBoundedRow();
    void collapsedRowInsertionKeepsItsOriginalTabOrdinals();
    void multilineDragReordersInPlaceBeforeFirstExit_data();
    void multilineDragReordersInPlaceBeforeFirstExit();
    void multilineDragCreatesAWindowOnlyAfterLeavingEveryVisibleRow_data();
    void multilineDragCreatesAWindowOnlyAfterLeavingEveryVisibleRow();
    void detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow_data();
    void detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow();
    void wheelExpandedRowsSurviveApiReconstructionUntilExplicitCollapse_data();
    void wheelExpandedRowsSurviveApiReconstructionUntilExplicitCollapse();
    void activeOnlyLaterRowDragNeverFallsBackToFirstRow_data();
    void activeOnlyLaterRowDragNeverFallsBackToFirstRow();
};

void TabStripLayoutTest::multilineHeaderActionsReserveOnlyTheLastRow_data()
{
    QTest::addColumn<bool>("pinned");
    QTest::addColumn<bool>("newTab");
    QTest::newRow("central") << false << false;
    QTest::newRow("central-create") << false << true;
    QTest::newRow("pinned") << true << false;
    QTest::newRow("pinned-create") << true << true;
}

void TabStripLayoutTest::multilineHeaderActionsReserveOnlyTheLastRow()
{
    QFETCH(bool, pinned); QFETCH(bool, newTab);
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.showNewTabButton = newTab;
    tabs.setAppearance(appearance, pinned); populate(tabs, 8);
    if (pinned) tabs.setPinnedActions([] {}, [] {});
    tabs.resize(700, 400); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    const int tabWidth = bar->displayTabRect(0).width();
    auto* actions = tabs.findChild<QWidget*>("TabHeaderActions"); QVERIFY(actions);
    tabs.resize(tabWidth * 2 + actions->sizeHint().width() / 2, 400);
    auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QTRY_VERIFY(bar->rowCount() > 1);
    // The actions and scroll viewport live in sibling branches. mapTo() only
    // accepts an ancestor, so compare all rectangles through global coordinates.
    const auto inViewport = [&](QWidget* widget, QRect area) {
        return QRect(scroll->viewport()->mapFromGlobal(widget->mapToGlobal(area.topLeft())), area.size());
    };
    QCOMPARE(scroll->viewport()->width(), tabs.width());
    QCOMPARE(bar->displayTabRect(0).top(), bar->displayTabRect(1).top());
    QVERIFY(inViewport(bar, bar->displayTabRect(1)).right() >= scroll->viewport()->width() - actions->width());
    const QRect actionsInViewport = inViewport(actions, actions->rect());
    QString actionGeometry;
    QDebug(&actionGeometry) << "viewport" << scroll->viewport()->rect() << "actions" << actionsInViewport
        << "actions-in-header" << actions->geometry() << "header" << actions->parentWidget()->geometry()
        << "scroll" << scroll->geometry() << "bar" << bar->geometry() << "rows" << bar->rowCount();
    QVERIFY2(scroll->viewport()->rect().contains(actionsInViewport), qPrintable(actionGeometry));
    QCOMPARE(actionsInViewport.height(), bar->displayTabRect(0).height());
    QCOMPARE(actionsInViewport.right(), scroll->viewport()->rect().right());
    QCOMPARE(actionsInViewport.bottom(), scroll->viewport()->rect().bottom());
    for (int i = 0; i < bar->count(); ++i)
        QVERIFY(!inViewport(bar, bar->displayTabRect(i)).intersects(actionsInViewport));
    auto* create = tabs.findChild<QWidget*>("TabCreateButtons"); QVERIFY(create);
    if (newTab) QVERIFY(!inViewport(create, create->rect()).intersects(actionsInViewport));
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    // The ordinary one-line layout still reserves its compact trailing controls.
    appearance.multiline = false; tabs.setAppearance(appearance, pinned);
    QTRY_COMPARE(scroll->viewport()->width(), tabs.width() - actions->width());
}

void TabStripLayoutTest::multilineWrapsWithoutHorizontalScrollAndKeepsEveryTab()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 9);
    tabs.resize(410, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport");
    QVERIFY(scroll); QTRY_VERIFY(bar->rowCount() > 1);
    QCOMPARE(bar->count(), ids.size()); QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    QVERIFY(tabs.findChild<QScrollBar*>("TabScrollBar")->isHidden());
    QSet<int> rows;
    for (int index = 0; index < bar->count(); ++index) {
        const auto rect = bar->displayTabRect(index); QVERIFY(!rect.isEmpty());
        QVERIFY(scroll->viewport()->rect().contains(rect)); rows.insert(rect.top());
        QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
        for (int other = index + 1; other < bar->count(); ++other) QVERIFY(!rect.intersects(bar->displayTabRect(other)));
    }
    QCOMPARE(rows.size(), bar->rowCount());
    const auto last = bar->displayTabRect(bar->count() - 1);
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, last.center());
    QCOMPARE(tabs.currentIndex(), bar->count() - 1);
    QCOMPARE(tabs.findChild<QStackedWidget*>()->currentIndex(), bar->count() - 1);
}

void TabStripLayoutTest::wheelExpansionShowsEveryRowThenOnlyActiveRow()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); populate(tabs, 9); tabs.resize(410, 300); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    tabs.scrollByWheel({0, -120});
    auto* bar = tabs.tabBar(); QTRY_VERIFY(bar->rowCount() > 1);
    for (int index = 0; index < bar->count(); ++index) QVERIFY(!bar->displayTabRect(index).isEmpty());
    const int laterIndex = bar->count() - 1;
    const int laterRow = bar->displayTabRect(laterIndex).top(); QVERIFY(laterRow > 0);
    QVector<bool> belongs;
    for (int index = 0; index < bar->count(); ++index) belongs.append(bar->displayTabRect(index).top() == laterRow);
    tabs.setCurrentIndex(laterIndex);
    const int fullHeight = bar->height(); tabs.scrollByWheel({0, 120});
    QTRY_VERIFY(bar->height() < fullHeight);
    for (int index = 0; index < bar->count(); ++index) {
        QCOMPARE(!bar->displayTabRect(index).isEmpty(), belongs[index]);
        if (belongs[index]) QCOMPARE(bar->displayTabRect(index).top(), 0);
    }
    QCOMPARE(tabs.currentIndex(), laterIndex); QCOMPARE(tabs.scrollOffset(), 0);
    tabs.scrollByWheel({0, -120}); QTRY_COMPARE(bar->height(), fullHeight);
    for (int index = 0; index < bar->count(); ++index) QVERIFY(!bar->displayTabRect(index).isEmpty());
}

void TabStripLayoutTest::placementIsIndependentForCentralAndPinnedGroups()
{
    auto appearance = TabAppearance{}; appearance.centralPlacement = TabBarPlacement::Bottom;
    GroupTabs central(defaultTheme(), nullptr), pinned(defaultTheme(), nullptr);
    central.setAppearance(appearance); pinned.setAppearance(appearance, true);
    populate(central, 1); populate(pinned, 1); central.resize(400, 220); pinned.resize(400, 220);
    central.show(); pinned.show();
    QVERIFY(QTest::qWaitForWindowExposed(&central)); QVERIFY(QTest::qWaitForWindowExposed(&pinned));
    auto* centralHeader = central.findChild<QWidget*>("TabGroupHeader"); auto* pinnedHeader = pinned.findChild<QWidget*>("TabGroupHeader");
    auto* centralContent = central.findChild<QStackedWidget*>(); auto* pinnedContent = pinned.findChild<QStackedWidget*>();
    QVERIFY(centralHeader && pinnedHeader && centralContent && pinnedContent);
    QVERIFY(centralHeader->y() > centralContent->y()); QVERIFY(pinnedHeader->y() < pinnedContent->y());
    appearance.centralPlacement = TabBarPlacement::Top; appearance.pinnedPlacement = TabBarPlacement::Bottom;
    central.setAppearance(appearance); pinned.setAppearance(appearance, true);
    QCoreApplication::processEvents();
    QVERIFY(centralHeader->y() < centralContent->y()); QVERIFY(pinnedHeader->y() > pinnedContent->y());
    QCOMPARE(central.count(), 1); QCOMPARE(pinned.count(), 1);
}

void TabStripLayoutTest::displayFlagsChangeActualLabelWidthsAndCreationControls()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 2, true);
    tabs.resize(640, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const int original = bar->displayTabRect(0).width();
    auto appearance = TabAppearance{}; appearance.showIcons = false; tabs.setAppearance(appearance);
    QTRY_COMPARE(bar->displayTabRect(0).width(), original - bar->iconSize().width() - 4);
    const int noIcon = bar->displayTabRect(0).width(); appearance.showNumbers = true; tabs.setAppearance(appearance);
    QTRY_COMPARE(bar->displayTabRect(0).width(), noIcon + bar->fontMetrics().horizontalAdvance("1") + 4);
    auto* controls = tabs.findChild<QWidget*>("TabCreateButtons"); QVERIFY(controls);
    QVERIFY(!controls->isHidden()); appearance.showNewTabButton = false; tabs.setAppearance(appearance);
    QVERIFY(controls->isHidden());
    auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QCOMPARE(scroll->widget()->width(), bar->width());
    appearance.showNewTabButton = true; tabs.setAppearance(appearance); QVERIFY(!controls->isHidden());
    QCOMPARE(scroll->widget()->width(), bar->width() + controls->width());
}

void TabStripLayoutTest::titleClippingKeepsFullTextAndDecorationSpace_data()
{
    QTest::addColumn<bool>("pinned"); QTest::addColumn<bool>("decorated"); QTest::addColumn<int>("fontPixels");
    QTest::newRow("central-plain") << false << false << 15;
    QTest::newRow("central-decorated-large-font") << false << true << 21;
    QTest::newRow("pinned-plain-large-font") << true << false << 21;
    QTest::newRow("pinned-decorated") << true << true << 15;
}

void TabStripLayoutTest::titleClippingKeepsFullTextAndDecorationSpace()
{
    QFETCH(bool, pinned); QFETCH(bool, decorated); QFETCH(int, fontPixels);
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs);
    auto font = tabs.font(); font.setPixelSize(fontPixels); tabs.setFont(font);
    auto appearance = TabAppearance{};
    appearance.showIcons = appearance.showSidebarIcons = decorated;
    appearance.showNumbers = appearance.showSidebarNumbers = decorated;
    appearance.showNewTabButton = false;
    auto preset = appearance.colors.front(); preset.id = "clipping-reference";
    preset.groupActive = {Qt::white, Qt::black, QColor("#123456"), QColor("#2468ac")};
    appearance.colors = {preset}; tabs.setAppearance(appearance, pinned);
    const auto title = QString(30, QLatin1Char('W'));
    QPixmap icon(16, 16); icon.fill(Qt::white);
    const auto id = QUuid::createUuid(); tabs.addTab(new QLabel("Content"), QIcon(icon), title, id);
    TabRecord record; record.id = id; record.title = title; record.colorId = preset.id; tabs.setTabMetadata(0, record);
    tabs.resize(900, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    // Regular tabs: 7 px at each end, a 16 px close target and single 4 px gaps.
    const int decorationWidth = decorated ? bar->iconSize().width() + 4 + bar->fontMetrics().horizontalAdvance("1") + 4 : 0;
    const int textLeft = 7 + decorationWidth;
    const int naturalWidth = bar->displayTabRect(0).width();
    QCOMPARE(naturalWidth, bar->fontMetrics().size(Qt::TextShowMnemonic, title).width() + decorationWidth + 34);
    const auto full = renderAtRatio(bar, 1);
    // Pick a boundary through actual glyph ink, so the reference distinguishes
    // geometric clipping from truncating whole characters or adding an ellipsis.
    int limit = 0;
    for (int x = textLeft + 32; x < textLeft + 120 && !limit; ++x)
        for (int y = 2; y < bar->height() - 3; ++y)
            if (full.pixelColor(x, y) != QColor(Qt::white) && full.pixelColor(x + 1, y) != QColor(Qt::white)) {
                limit = x - textLeft + 1; break;
            }
    QVERIFY(limit >= 16);
    QVector<QImage> references;
    for (const qreal ratio : {1.0, 2.0}) references.append(renderAtRatio(bar, ratio));
    appearance.clipTitles = true; appearance.maximumTitleWidth = limit;
    tabs.setAppearance(appearance, pinned);
    QTRY_COMPARE(bar->displayTabRect(0).width(), limit + decorationWidth + 34);
    QCOMPARE(bar->tabText(0), title); QCOMPARE(bar->tabToolTip(0), title);
    const QRect textRect(textLeft, 2, limit, bar->height() - 5);
    for (int index = 0; index < references.size(); ++index) {
        const auto actual = renderAtRatio(bar, index ? 2 : 1);
        QCOMPARE(logicalCrop(actual, textRect), logicalCrop(references[index], textRect));
        // Decorations and the close target remain outside the clipped text.
        const auto blank = logicalCrop(actual, QRect(textLeft + limit, 2, 5, bar->height() - 5));
        QVERIFY(!containsColor(blank, Qt::black));
    }
    const auto clippedWidth = bar->displayTabRect(0).width();
    tabs.setInsertionPreview(1, clippedWidth);
    QCOMPARE(bar->insertionPreviewRect().width(), clippedWidth);
    QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(bar->insertionPreviewRect().center())), 1);
    tabs.setInsertionPreview(-1, 0);
    appearance.clipTitles = false; tabs.setAppearance(appearance, pinned);
    QTRY_COMPARE(bar->displayTabRect(0).width(), naturalWidth);
}

void TabStripLayoutTest::zeroTitleLimitsKeepLabelsAndDecorationSpace()
{
    const auto title = QString(30, QLatin1Char('W'));
    auto appearance = TabAppearance{};
    appearance.showIcons = false; appearance.showNewTabButton = false;
    auto preset = appearance.colors.front(); preset.id = "zero-title-limit";
    preset.groupActive = preset.groupInactive = preset.autoHideActive = preset.autoHideInactive
        = {Qt::white, Qt::red, Qt::blue, Qt::green};
    appearance.colors = {preset};
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs);
    tabs.setAppearance(appearance);
    const auto id = QUuid::createUuid(); tabs.addTab(new QLabel("Content"), {}, title, id);
    TabRecord record; record.id = id; record.title = title; record.colorId = preset.id;
    tabs.setTabMetadata(0, record); tabs.resize(900, 200); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const auto naturalWidth = bar->displayTabRect(0).width();
    QVERIFY(containsColor(renderAtRatio(bar, 1), Qt::red));
    appearance.clipTitles = true; appearance.maximumTitleWidth = 0; tabs.setAppearance(appearance);
    QVERIFY(!containsColor(renderAtRatio(bar, 1), Qt::red));
    QVERIFY(bar->displayTabRect(0).width() > 0);
    QCOMPARE(bar->tabText(0), title); QCOMPARE(bar->tabToolTip(0), title);
    appearance.maximumTitleWidth = maximumTabTitleWidth; tabs.setAppearance(appearance);
    QCOMPARE(bar->displayTabRect(0).width(), naturalWidth);
    QVERIFY(containsColor(renderAtRatio(bar, 1), Qt::red));
    for (const bool vertical : {false, true}) {
        EdgeTabButton button(title, vertical, 24, &tabs);
        button.setPreset(preset); button.resize(button.sizeHint());
        QVERIFY(containsColor(renderAtRatio(&button, 1), Qt::red));
        button.setTitleClipping(true, 0); button.resize(button.sizeHint());
        QVERIFY(!containsColor(renderAtRatio(&button, 1), Qt::red));
        QVERIFY(!button.size().isEmpty()); QCOMPARE(button.text(), title);
        button.setTitleClipping(true, maximumTabTitleWidth); button.resize(button.sizeHint());
        QVERIFY(containsColor(renderAtRatio(&button, 1), Qt::red));
    }
}

namespace {
// First and last position along a label's text axis that has the exact color.
std::pair<int, int> colorSpan(const QImage& image, QColor color, bool alongY, int from, int to)
{
    int first = -1, last = -1;
    const int length = alongY ? image.height() : image.width();
    for (int major = 0; major < length; ++major)
        for (int minor = from; minor < to; ++minor) {
            const auto pixel = alongY ? image.pixelColor(minor, major) : image.pixelColor(major, minor);
            if (pixel.rgb() != color.rgb()) continue;
            if (first < 0) first = major;
            last = major; break;
        }
    return {first, last};
}
TabColorPreset solidPreset(const QString& id)
{
    auto preset = defaultTabColorPresets().front(); preset.id = id;
    preset.groupActive = preset.groupInactive = preset.autoHideActive = preset.autoHideInactive
        = {Qt::white, Qt::black, QColor("#123456"), QColor("#2468ac")};
    return preset;
}
void sendBarMouse(QWidget* bar, QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, point, bar->mapToGlobal(point), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(bar, &event);
}
}

void TabStripLayoutTest::zeroTitleLeavesOneGapBetweenIconAndClose()
{
    auto appearance = TabAppearance{}; appearance.showNewTabButton = false;
    appearance.clipTitles = true; appearance.maximumTitleWidth = 0;
    const auto preset = solidPreset("zero-title-gap"); appearance.colors = {preset};
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs); tabs.setAppearance(appearance);
    QPixmap icon(16, 16); icon.fill(Qt::white);
    const auto title = QString(30, QLatin1Char('W')); const auto id = QUuid::createUuid();
    tabs.addTab(new QLabel("Content"), QIcon(icon), title, id);
    TabRecord record; record.id = id; record.title = title; record.colorId = preset.id;
    tabs.setTabMetadata(0, record); tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const auto tab = bar->displayTabRect(0);
    // 7 padding, 16 icon, one 4 px gap, 16 close target, 7 padding.
    QCOMPARE(tab.width(), 7 + 16 + 4 + 16 + 7);
    const auto image = logicalCrop(renderAtRatio(bar, 1), tab);
    const auto iconSpan = colorSpan(image, preset.groupActive.icon, false, 2, tab.height() - 4);
    QCOMPARE(iconSpan.first, 7); QCOMPARE(iconSpan.second, 7 + 16 - 1);
    // The close target appears on hover, one gap after the icon; its wash
    // extends one pixel around the 16 px target.
    sendBarMouse(bar, QEvent::MouseMove, QPoint(tab.right() - 7 - 8 + 1, tab.center().y()), Qt::NoButton, Qt::NoButton);
    const auto hovered = logicalCrop(renderAtRatio(bar, 1), tab);
    int closeLeft = -1;
    for (int x = iconSpan.second + 1; x < tab.width() && closeLeft < 0; ++x)
        for (int y = 2; y < tab.height() - 4; ++y)
            if (hovered.pixelColor(x, y).rgb() != QColor(Qt::white).rgb()) { closeLeft = x; break; }
    QCOMPARE(closeLeft, 7 + 16 + 4 - 1);
}

void TabStripLayoutTest::zeroTitleRailLabelKeepsEqualEndPadding_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::newRow("horizontal") << false;
    QTest::newRow("vertical") << true;
}

void TabStripLayoutTest::zeroTitleRailLabelKeepsEqualEndPadding()
{
    QFETCH(bool, vertical);
    QWidget root; applyTheme(&root); root.resize(200, 200);
    const int thickness = defaultTheme().metrics.edgeThickness;
    EdgeTabButton button(QString(30, QLatin1Char('W')), vertical, thickness, &root);
    QPixmap icon(16, 16); icon.fill(Qt::white);
    const auto preset = solidPreset("zero-title-rail");
    button.setPreset(preset); button.setPresentation(QIcon(icon), 0);
    for (const int limit : {0, 1, 40}) {
        button.setTitleClipping(true, limit); button.resize(button.sizeHint());
        const int extent = vertical ? button.height() : button.width();
        // 8 px at each end; a title only adds itself and one gap.
        QCOMPARE(extent, 8 + 16 + (limit ? 4 + limit : 0) + 8);
        const auto span = colorSpan(renderAtRatio(&button, 1), preset.autoHideInactive.icon, vertical, 2, thickness - 3);
        QCOMPARE(span.first, 8); QCOMPARE(span.second, 8 + 16 - 1);
        if (!limit) QCOMPARE(extent - 1 - span.second, 8);
    }
}

void TabStripLayoutTest::closeTargetShowsHoverAndPressFeedback()
{
    auto appearance = TabAppearance{}; appearance.showNewTabButton = false; appearance.showIcons = false;
    const auto preset = solidPreset("close-feedback"); appearance.colors = {preset};
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs); tabs.setAppearance(appearance);
    const auto id = QUuid::createUuid(); tabs.addTab(new QLabel("Content"), {}, "Feedback", id);
    TabRecord record; record.id = id; record.title = "Feedback"; record.colorId = preset.id;
    tabs.setTabMetadata(0, record);
    int closed = 0; tabs.closeRequested = [&closed](int) { ++closed; };
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const auto tab = bar->displayTabRect(0);
    const QRect target(tab.right() - 7 - 16 + 1, tab.top() + (tab.height() - 16) / 2, 16, 16);
    const QPoint body(tab.left() + 10, tab.center().y());
    sendBarMouse(bar, QEvent::MouseMove, body, Qt::NoButton, Qt::NoButton);
    const auto plain = renderAtRatio(bar, 1);
    sendBarMouse(bar, QEvent::MouseMove, target.center(), Qt::NoButton, Qt::NoButton);
    const auto hovered = renderAtRatio(bar, 1);
    sendBarMouse(bar, QEvent::MouseButtonPress, target.center(), Qt::LeftButton, Qt::LeftButton);
    const auto pressed = renderAtRatio(bar, 1);
    // Only the target gains a wash, stronger while pressed; the label is unchanged.
    const QRect label(tab.left(), tab.top(), target.left() - 2 - tab.left(), tab.height());
    QCOMPARE(logicalCrop(hovered, label), logicalCrop(plain, label));
    const QPoint corner = target.topLeft();
    QCOMPARE(plain.pixelColor(corner).rgb(), QColor(Qt::white).rgb());
    QVERIFY(hovered.pixelColor(corner).rgb() != QColor(Qt::white).rgb());
    QVERIFY(qGray(pressed.pixelColor(corner).rgb()) < qGray(hovered.pixelColor(corner).rgb()));
    // Releasing elsewhere cancels and clears the feedback; releasing on it closes.
    sendBarMouse(bar, QEvent::MouseMove, body, Qt::NoButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, body, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(closed, 0);
    QCOMPARE(renderAtRatio(bar, 1).pixelColor(corner).rgb(), QColor(Qt::white).rgb());
    sendBarMouse(bar, QEvent::MouseButtonPress, target.center(), Qt::LeftButton, Qt::LeftButton);
    sendBarMouse(bar, QEvent::MouseButtonRelease, target.center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(closed, 1);
}

void TabStripLayoutTest::narrowMultilineTitlesUseHardClippingWithoutAWidthLimit()
{
    GroupTabs tabs(defaultTheme(), nullptr); applyTheme(&tabs);
    auto appearance = TabAppearance{}; appearance.showIcons = false; appearance.showNewTabButton = false;
    auto preset = appearance.colors.front(); preset.id = "multiline-reference";
    preset.groupActive = {Qt::white, Qt::black, QColor("#123456"), Qt::black}; appearance.colors = {preset};
    tabs.setAppearance(appearance);
    const auto title = QString(30, QLatin1Char('W')); const auto id = QUuid::createUuid();
    tabs.addTab(new QLabel("Content"), {}, title, id);
    TabRecord record; record.id = id; record.title = title; record.colorId = preset.id; tabs.setTabMetadata(0, record);
    tabs.resize(900, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); const auto full = renderAtRatio(bar, 1);
    appearance.multiline = true; tabs.setAppearance(appearance); tabs.resize(113, 250);
    auto* viewport = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(viewport);
    QTRY_COMPARE(bar->displayTabRect(0).width(), viewport->viewport()->width());
    const auto area = bar->displayTabRect(0);
    const QRect textRect(7, 2, area.width() - 34, area.height() - 5);
    QVERIFY(textRect.width() > 16);
    QCOMPARE(logicalCrop(renderAtRatio(bar, 1), textRect), logicalCrop(full, textRect));
    QCOMPARE(bar->tabText(0), title); QCOMPARE(bar->tabToolTip(0), title);
    QCOMPARE(viewport->horizontalScrollBar()->maximum(), 0);
}

void TabStripLayoutTest::tabShapeOptionsPreserveStyleStates_data()
{
    QTest::addColumn<bool>("bottom");
    QTest::addColumn<bool>("rtl");
    QTest::addColumn<bool>("multiline");
    for (const bool bottom : {false, true}) for (const bool rtl : {false, true})
        for (const bool multiline : {false, true}) {
            const auto name = QByteArray(bottom ? "bottom" : "top") + (rtl ? "-rtl" : "-ltr")
                + (multiline ? "-multiline" : "-single-row");
            QTest::newRow(name.constData()) << bottom << rtl << multiline;
        }
}

void TabStripLayoutTest::tabShapeOptionsPreserveStyleStates()
{
    QFETCH(bool, bottom); QFETCH(bool, rtl); QFETCH(bool, multiline);
    const auto cleanup = qScopeGuard([] { test::authoredMouseInput().finish(); });
    QWidget host;
    DragTabBar bar(&host);
    auto* capture = new CapturingTabShapeStyle;
    capture->setParent(&bar); bar.setStyle(capture);
    bar.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
    bar.setShape(bottom ? QTabBar::RoundedSouth : QTabBar::RoundedNorth);
    auto appearance = TabAppearance{}; appearance.multiline = multiline; appearance.showNewTabButton = false;
    bar.setAppearance(appearance);
    for (int index = 0; index < 5; ++index) bar.addTab(QStringLiteral("Tab %1").arg(index));
    bar.setTabVisible(0, false); bar.setTabVisible(4, false);
    bar.setCurrentIndex(2); bar.setTabEnabled(3, false);
    QPixmap image(16, 16); image.fill(Qt::red); bar.setTabIcon(1, QIcon(image));
    bar.setIconSize({16, 16}); bar.setTabTextColor(1, QColor("#345678"));
    auto* left = new QLabel(QStringLiteral("L")); left->setFixedSize(13, 15);
    auto* right = new QLabel(QStringLiteral("R")); right->setFixedSize(17, 19);
    bar.setTabButton(1, QTabBar::LeftSide, left); bar.setTabButton(3, QTabBar::RightSide, right);
    const int width = multiline ? 130 : 480;
    bar.layoutTabs(width, true); bar.resize(width, bar.displayedSize().height());
    QToolButton other(&host); other.setText(QStringLiteral("Other focus")); other.setGeometry(0, bar.height() + 4, 100, 24);
    host.resize(500, bar.height() + 32); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host)); QVERIFY(QTest::qWaitForWindowActive(&host));
    bar.setFocus(); QTRY_VERIFY(bar.hasFocus());
    if (multiline) QVERIFY(bar.rowCount() > 1);
    const auto paint = [&] {
        capture->tabs.clear();
        renderAtRatio(&bar, 1);
    };
    QEvent leave(QEvent::Leave); QApplication::sendEvent(&bar, &leave);
    paint();
    QCOMPARE(capture->tabs.size(), 3);
    QVERIFY(!capture->tabs.contains(0)); QVERIFY(!capture->tabs.contains(4));
    for (const int index : {1, 2, 3}) {
        QVERIFY(capture->tabs.contains(index));
        const auto option = capture->tabs.value(index);
        QCOMPARE(option.rect, bar.displayTabRect(index)); QCOMPARE(option.tabIndex, index);
        QCOMPARE(option.shape, bottom ? QTabBar::RoundedSouth : QTabBar::RoundedNorth);
        QCOMPARE(option.direction, rtl ? Qt::RightToLeft : Qt::LeftToRight);
        QVERIFY(option.documentMode);
        QCOMPARE(option.state.testFlag(QStyle::State_Selected), index == 2);
        QCOMPARE(option.state.testFlag(QStyle::State_HasFocus), index == 2);
        QCOMPARE(option.state.testFlag(QStyle::State_Enabled), index != 3);
        QCOMPARE(option.state.testFlag(QStyle::State_Active), bar.isActiveWindow());
        QVERIFY(!option.state.testFlag(QStyle::State_MouseOver));
        QVERIFY(!option.state.testFlag(QStyle::State_Sunken));
    }
    QCOMPARE(capture->tabs.value(1).position, QStyleOptionTab::Beginning);
    QCOMPARE(capture->tabs.value(2).position, QStyleOptionTab::Middle);
    QCOMPARE(capture->tabs.value(3).position, QStyleOptionTab::End);
    QCOMPARE(capture->tabs.value(1).selectedPosition, QStyleOptionTab::NextIsSelected);
    QCOMPARE(capture->tabs.value(2).selectedPosition, QStyleOptionTab::NotAdjacent);
    QCOMPARE(capture->tabs.value(3).selectedPosition, QStyleOptionTab::PreviousIsSelected);
    QVERIFY(!capture->tabs.value(1).icon.isNull()); QCOMPARE(capture->tabs.value(1).iconSize, QSize(16, 16));
    QCOMPARE(capture->tabs.value(1).palette.color(bar.foregroundRole()), QColor("#345678"));
    QCOMPARE(capture->tabs.value(1).leftButtonSize, left->size());
    QCOMPARE(capture->tabs.value(3).rightButtonSize, right->size());

    other.setFocus(); QTRY_VERIFY(other.hasFocus()); paint();
    for (const auto& option : capture->tabs) QVERIFY(!option.state.testFlag(QStyle::State_HasFocus));
    sendMouse(&bar, QEvent::MouseMove, bar.mapToGlobal(bar.displayTabRect(1).center()), Qt::NoButton, Qt::NoButton);
    paint();
    for (const int index : {1, 2, 3})
        QCOMPARE(capture->tabs.value(index).state.testFlag(QStyle::State_MouseOver), index == 1);
    QApplication::sendEvent(&bar, &leave); paint();
    for (const auto& option : capture->tabs) QVERIFY(!option.state.testFlag(QStyle::State_MouseOver));
    bar.setTabVisible(1, false); bar.setTabVisible(3, false);
    bar.layoutTabs(width, true); paint();
    QCOMPARE(capture->tabs.size(), 1);
    QCOMPARE(capture->tabs.value(2).position, QStyleOptionTab::OnlyOneTab);
    QCOMPARE(capture->tabs.value(2).selectedPosition, QStyleOptionTab::NotAdjacent);
    QVERIFY(capture->tabs.value(2).state.testFlag(QStyle::State_Selected));
}

void TabStripLayoutTest::paletteRendersSelectedAndUnselectedStatesAndActiveIcon()
{
    GroupTabs tabs(defaultTheme(), nullptr); const auto ids = populate(tabs, 2, true);
    auto appearance = TabAppearance{};
    auto preset = appearance.colors.front(); preset.id = "test";
    preset.groupActive = {QColor("#d4e5f6"), QColor("#172839"), QColor("#112233"), QColor("#cc55aa")};
    preset.groupInactive = {QColor("#f5dfc9"), QColor("#334455"), QColor("#556677"), QColor("#887766")};
    appearance.colors = {preset}; tabs.setAppearance(appearance);
    for (int index = 0; index < ids.size(); ++index) { TabRecord record; record.id = ids[index]; record.colorId = "test"; tabs.setTabMetadata(index, record); }
    tabs.setCurrentIndex(0); tabs.resize(640, 250); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    for (int index = 0; index < 2; ++index) {
        const auto rect = bar->displayTabRect(index); const auto colors = index == 0 ? preset.groupActive : preset.groupInactive;
        QCOMPARE(sample(bar, {rect.left() + 3, rect.center().y()}), colors.background);
        QCOMPARE(sample(bar, {rect.left() + 3, rect.bottom()}), colors.line);
        QVERIFY(containsColor(bar->grab(rect).toImage(), colors.text));
    }
    QVERIFY(containsColor(bar->grab(bar->displayTabRect(0)).toImage(), preset.groupActive.icon));
    tabs.setCurrentIndex(1);
    QCOMPARE(sample(bar, {bar->displayTabRect(0).left() + 3, bar->displayTabRect(0).center().y()}), preset.groupInactive.background);
    QVERIFY(containsColor(bar->grab(bar->displayTabRect(1)).toImage(), preset.groupActive.icon));
}

void TabStripLayoutTest::newTabButtonsHaveGapAndBothOpenTypeMenuOnContextPress()
{
    GroupTabs tabs(defaultTheme(), nullptr); populate(tabs, 2); tabs.resize(700, 250); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* add = tabs.findChild<QToolButton*>("NewTabButton"); auto* types = tabs.findChild<QToolButton*>("NewTabTypeButton");
    QVERIFY(add && types); QCOMPARE(types->x() - add->geometry().right() - 1, 1);
    QCOMPARE(add->height(), types->height()); QVERIFY(add->height() == tabs.tabBar()->height());
    int requested = 0; QPointer<QMenu> popup;
    tabs.newTabMenuRequested = [&](QWidget* parent) { ++requested; popup = new DismissOnlyMenu(parent); popup->addAction("Create"); applyTheme(popup); return popup.data(); };
    for (auto* button : {add, types}) {
        QContextMenuEvent event(QContextMenuEvent::Mouse, button->rect().center(), button->mapToGlobal(button->rect().center()));
        QApplication::sendEvent(button, &event); QVERIFY(popup); QTRY_VERIFY(popup->isVisible());
        QVERIFY(types->isDown()); popup->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!popup); QVERIFY(!types->isDown());
    }
    QCOMPARE(requested, 2);
}

void TabStripLayoutTest::insertionPreviewAndHitTestingUseThePointerRow_data()
{
    QTest::addColumn<bool>("bottom");
    QTest::newRow("top") << false;
    QTest::newRow("bottom") << true;
}

void TabStripLayoutTest::insertionPreviewAndHitTestingUseThePointerRow()
{
    QFETCH(bool, bottom);
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 9);
    tabs.resize(410, 320); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QTRY_VERIFY(bar->rowCount() >= 3);
    const int rowHeight = defaultTheme().metrics.tabHeight;
    for (int index = 0; index < bar->count(); ++index) {
        const auto label = bar->displayTabRect(index); QVERIFY(!label.isEmpty());
        QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(label.left() + 2, label.center().y()))), index);
        QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(label.right() - 2, label.center().y()))), index + 1);
    }
    // Insert at the beginning of a later row: this must not become an x-only
    // offset in the first row, and the gap remains one label high.
    int later = 0;
    while (later < bar->count() && bar->displayTabRect(later).top() == 0) ++later;
    QVERIFY(later < bar->count());
    const auto original = bar->displayTabRect(later);
    tabs.setInsertionPreview(later, original.width());
    const auto gap = tabs.insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.top(), bar->mapToGlobal(original.topLeft()).y());
    QCOMPARE(gap.height(), rowHeight); QCOMPARE(gap.width(), original.width());
    QVERIFY(tabs.viewportRectGlobal().contains(gap));
    QCOMPARE(tabs.insertionIndex(gap.center()), later);
    for (int index = 0; index < bar->count(); ++index) {
        const auto label = bar->displayTabRect(index).translated(bar->mapToGlobal(QPoint()));
        QVERIFY(!label.intersects(gap)); QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
    }
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
}

void TabStripLayoutTest::oversizedInsertionPreviewRemainsOneBoundedRow()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 3);
    tabs.resize(250, 300); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    tabs.setInsertionPreview(1, 2000);
    const auto gap = tabs.insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.width(), tabs.viewportRectGlobal().width());
    QCOMPARE(gap.height(), defaultTheme().metrics.tabHeight);
    QVERIFY(tabs.viewportRectGlobal().contains(gap)); QCOMPARE(tabs.insertionIndex(gap.center()), 1);
    auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    auto* bar = tabs.tabBar();
    for (int index = 0; index < bar->count(); ++index) {
        const auto label = bar->displayTabRect(index).translated(bar->mapToGlobal(QPoint()));
        QVERIFY(!label.intersects(gap)); QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
    }
}

void TabStripLayoutTest::collapsedRowInsertionKeepsItsOriginalTabOrdinals()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    auto appearance = TabAppearance{}; appearance.multiline = true;
    appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    tabs.setAppearance(appearance); const auto ids = populate(tabs, 10);
    tabs.resize(410, 320); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    tabs.scrollByWheel({0, -120}); auto* bar = tabs.tabBar(); QTRY_VERIFY(bar->rowCount() >= 3);
    int middle = 0;
    while (middle < bar->count() && bar->displayTabRect(middle).top() == 0) ++middle;
    QVERIFY(middle < bar->count()); const int rowTop = bar->displayTabRect(middle).top();
    int rowEnd = middle;
    while (rowEnd < bar->count() && bar->displayTabRect(rowEnd).top() == rowTop) ++rowEnd;
    QVERIFY(rowEnd < bar->count()); // Hidden later rows must not change the insertion after this row.
    tabs.setCurrentIndex(middle); tabs.scrollByWheel({0, 120});
    QTRY_COMPARE(bar->height(), defaultTheme().metrics.tabHeight);
    const auto first = bar->displayTabRect(middle), last = bar->displayTabRect(rowEnd - 1);
    QCOMPARE(tabs.insertionIndex(bar->mapToGlobal(QPoint(first.left() + 2, first.center().y()))), middle);
    const auto rowRight = tabs.viewportRectGlobal().topRight() + QPoint(-2, last.center().y());
    QCOMPARE(tabs.insertionIndex(rowRight), rowEnd);
    for (int index = 0; index < bar->count(); ++index) {
        QCOMPARE(bar->displayTabRect(index).isEmpty(), index < middle || index >= rowEnd);
        QCOMPARE(bar->tabData(index).toUuid(), ids[index]);
    }
}

void TabStripLayoutTest::multilineDragReordersInPlaceBeforeFirstExit_data()
{
    insertionPreviewAndHitTestingUseThePointerRow_data();
}

void TabStripLayoutTest::multilineDragReordersInPlaceBeforeFirstExit()
{
    QFETCH(bool, bottom);
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = manager.tabAppearance(); appearance.multiline = true;
    appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const auto window = manager.createWindow(WindowKind::Workbench, {"Multiline drag", {480, qMin(420, available.height() - 24)}});
    QVector<TabId> ids; QVector<QPointer<QWidget>> contents;
    for (int index = 0; index < 9; ++index) {
        auto* content = new QLabel(QString("Content %1").arg(index)); contents.append(content);
        ids.append(manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}}));
    }
    const auto source = ids[1], group = manager.model().groupForTab(source);
    manager.windowWidget(window)->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    auto* bar = view->tabBar(); QTRY_VERIFY(bar->rowCount() >= 3);
    const auto start = bar->mapToGlobal(bar->displayTabRect(1).center() - QPoint(12, 0));
    const auto threshold = start + QPoint(QApplication::startDragDistance() + 3, 0);
    QVERIFY(view->viewportRectGlobal().contains(threshold));
    sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(bar, QEvent::MouseMove, threshold);
    QTRY_VERIFY(labelPreview()); QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().groupForTab(source), group); QVERIFY(contents[1]->isVisible());
    auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    const auto later = bar->displayTabRect(6); QVERIFY(later.top() > 0);
    const auto destination = bar->mapToGlobal(QPoint(later.left() + 2, later.center().y()));
    QVERIFY(view->viewportRectGlobal().contains(destination));
    sendMouse(receiver, QEvent::MouseMove, destination);
    QCOMPARE(manager.model().windows().size(), 1); QVERIFY(labelPreview());
    QCOMPARE(manager.model().groupForTab(source), group); QVERIFY(contents[1]->isVisible());
    const int insertion = view->insertionIndex(destination);
    QVERIFY(insertion > 1); QVERIFY(insertion < ids.size());
    const auto gap = view->insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.height(), defaultTheme().metrics.tabHeight);
    QVERIFY(view->viewportRectGlobal().contains(gap));
    auto expected = ids; expected.removeOne(source); expected.insert(insertion, source);
    sendMouse(receiver, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QTRY_VERIFY(!labelPreview()); QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().group(group)->tabs, expected);
    for (int index = 0; index < ids.size(); ++index)
        QCOMPARE(manager.paneWidget(manager.model().tab(ids[index])->activePane), contents[index].data());
}

void TabStripLayoutTest::multilineDragCreatesAWindowOnlyAfterLeavingEveryVisibleRow_data()
{
    insertionPreviewAndHitTestingUseThePointerRow_data();
}

void TabStripLayoutTest::multilineDragCreatesAWindowOnlyAfterLeavingEveryVisibleRow()
{
    QFETCH(bool, bottom);
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = manager.tabAppearance(); appearance.multiline = true;
    appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const auto window = manager.createWindow(WindowKind::Workbench, {"Multiline transition", {480, qMin(420, available.height() - 24)}});
    QVector<TabId> ids; QPointer<QWidget> sourceContent;
    for (int index = 0; index < 9; ++index) {
        auto* content = new QLabel(QString("Content %1").arg(index));
        ids.append(manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}}));
        if (index == 1) sourceContent = content;
    }
    const auto source = ids[1], group = manager.model().groupForTab(source);
    manager.windowWidget(window)->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    auto* bar = view->tabBar(); QTRY_VERIFY(bar->rowCount() >= 3);
    const auto start = bar->mapToGlobal(bar->displayTabRect(1).center() - QPoint(12, 0));
    sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(bar, QEvent::MouseMove, start + QPoint(QApplication::startDragDistance() + 3, 0));
    QTRY_VERIFY(labelPreview()); auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    const auto last = bar->displayTabRect(bar->count() - 1);
    sendMouse(receiver, QEvent::MouseMove, bar->mapToGlobal(last.center()));
    QCOMPARE(manager.model().windows().size(), 1); QVERIFY(labelPreview()); QVERIFY(sourceContent->isVisible());
    const auto bounds = view->viewportRectGlobal();
    const QPoint outside(bounds.center().x(), bottom ? bounds.top() - 30 : bounds.bottom() + 30);
    sendMouse(receiver, QEvent::MouseMove, outside);
    QTRY_COMPARE(manager.model().windows().size(), 2); QTRY_VERIFY(!labelPreview());
    const auto floatingGroup = manager.model().groupForTab(source); QVERIFY(floatingGroup != group);
    const auto floating = manager.model().group(floatingGroup)->windowId;
    QCOMPARE(manager.model().window(floating)->kind, WindowKind::Floating);
    QCOMPARE(manager.paneWidget(manager.model().tab(source)->activePane), sourceContent.data());
    receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    // Cancel must restore the exact original order and content, not construct
    // another widget or create a third window during the stage transition.
    QTest::keyClick(receiver, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(manager.model().windows().size(), 1); QCOMPARE(manager.model().group(group)->tabs, ids);
    QCOMPARE(manager.paneWidget(manager.model().tab(source)->activePane), sourceContent.data());
    QVERIFY(sourceContent); QVERIFY(!QWidget::mouseGrabber());
}

void TabStripLayoutTest::detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow_data()
{
    QTest::addColumn<bool>("bottom"); QTest::addColumn<bool>("activeOnly"); QTest::addColumn<bool>("expandByWheel");
    QTest::addColumn<bool>("shortLabel");
    QTest::newRow("top-expanded") << false << false << false << false;
    QTest::newRow("bottom-expanded") << true << false << false << false;
    QTest::newRow("top-active-row") << false << true << false << false;
    QTest::newRow("bottom-active-row") << true << true << false << false;
    QTest::newRow("top-wheel-expanded") << false << true << true << false;
    QTest::newRow("bottom-wheel-expanded") << true << true << true << false;
    QTest::newRow("top-short-expanded") << false << false << false << true;
    QTest::newRow("bottom-short-expanded") << true << false << false << true;
    QTest::newRow("top-short-active-row") << false << true << false << true;
    QTest::newRow("bottom-short-active-row") << true << true << false << true;
}

void TabStripLayoutTest::detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow()
{
    QFETCH(bool, bottom); QFETCH(bool, activeOnly); QFETCH(bool, expandByWheel);
    QFETCH(bool, shortLabel);
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.multiline = true; appearance.wheelExpandRows = activeOnly; appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const QSize size(qMin(1000, available.width() - 24), qMin(500, available.height() - 24));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Multiline drop", size});
    QPointer<QWidget> sourceContent = new QLabel("Source content");
    const auto source = manager.addTab(window, sourceContent, {shortLabel ? "x" : "MMMM tab 0", "label", {}});
    const auto sourceGroup = manager.model().groupForTab(source);
    const auto sourcePane = manager.model().tab(source)->activePane;
    QVector<TabId> targets; QVector<PaneId> targetPanes; QVector<QPointer<QWidget>> targetContents;
    GroupId targetGroup;
    for (int index = 0; index < 13; ++index) {
        QPointer<QWidget> content = new QLabel(QString("Target content %1").arg(index));
        const auto tab = manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}}, targetGroup);
        if (index == 0) {
            QVERIFY(manager.moveTab(tab, sourceGroup, Placement::Right));
            targetGroup = manager.model().groupForTab(tab); QVERIFY(targetGroup != sourceGroup);
        }
        targets.append(tab); targetPanes.append(manager.model().tab(tab)->activePane); targetContents.append(content);
    }
    QCOMPARE(manager.model().group(sourceGroup)->tabs, QVector<TabId>{source});
    QCOMPARE(manager.model().group(targetGroup)->tabs, targets);
    // Keep a later original row current even while the source group owns
    // input focus. Active-only mode must retain this target row on rebuild.
    QVERIFY(manager.activateTab(targets.last())); QVERIFY(manager.activateTab(source));
    auto* main = manager.windowWidget(window); main->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(main));
    if (expandByWheel) {
        auto* targetView = groupView(main, targetGroup); QVERIFY(targetView);
        QCOMPARE(targetView->tabBar()->height(), defaultTheme().metrics.tabHeight);
        targetView->scrollByWheel({0, -120});
        QTRY_VERIFY(targetView->tabBar()->height() > defaultTheme().metrics.tabHeight);
        for (int index = 0; index < targetView->count(); ++index)
            QVERIFY(!targetView->tabBar()->displayTabRect(index).isEmpty());
    }
    auto* sourceView = groupView(main, sourceGroup); QVERIFY(sourceView);
    auto* sourceBar = sourceView->tabBar(); const auto sourceRect = sourceBar->displayTabRect(0); QVERIFY(!sourceRect.isEmpty());
    const auto start = sourceBar->mapToGlobal(sourceRect.center() - QPoint(12, 0));
    const auto threshold = start + QPoint(QApplication::startDragDistance() + 3, 0);
    QVERIFY(sourceView->viewportRectGlobal().contains(threshold));
    sendMouse(sourceBar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(sourceBar, QEvent::MouseMove, threshold);
    QTRY_VERIFY(labelPreview()); QCOMPARE(manager.model().windows().size(), 1);
    auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    const auto strip = sourceView->viewportRectGlobal();
    const QPoint outside(strip.center().x(), bottom ? strip.top() - 30 : strip.bottom() + 30);
    sendMouse(receiver, QEvent::MouseMove, outside);
    QTRY_COMPARE(manager.model().windows().size(), 2); QTRY_VERIFY(!labelPreview());
    QVERIFY(!manager.model().group(sourceGroup)); // The emptied split is removed immediately.
    QCOMPARE(manager.model().centralGroups(window), QVector<GroupId>{targetGroup});
    const auto carrierGroup = manager.model().groupForTab(source); QVERIFY(carrierGroup != targetGroup);
    const auto carrierId = manager.model().group(carrierGroup)->windowId;
    QPointer<WorkspaceWindow> carrier = static_cast<WorkspaceWindow*>(manager.windowWidget(carrierId)); QVERIFY(carrier);
    QCOMPARE(manager.model().window(carrierId)->kind, WindowKind::Floating);
    QCOMPARE(QWidget::mouseGrabber(), carrier.data());
    QCOMPARE(carrier->surface()->dragLabelExtent(source, false), sourceRect.width());
    QCOMPARE(manager.model().tab(source)->activePane, sourcePane); QCOMPARE(manager.paneWidget(sourcePane), sourceContent.data());

    // Detaching collapsed a split, so use the target's newly mounted view.
    // Determine the expected insertion from original tab IDs and rectangles
    // before asking any insertion hit-test helper for its result.
    auto* targetView = groupView(main, targetGroup); QVERIFY(targetView);
    auto* targetBar = targetView->tabBar(); QTRY_VERIFY(targetBar->rowCount() > 1);
    QVector<int> visibleIndices;
    int bottomRow = 0;
    for (int index = 0; index < targetBar->count(); ++index)
        if (!targetBar->displayTabRect(index).isEmpty()) bottomRow = qMax(bottomRow, targetBar->displayTabRect(index).top());
    for (int index = 0; index < targetBar->count(); ++index)
        if (!targetBar->displayTabRect(index).isEmpty() && targetBar->displayTabRect(index).top() == bottomRow)
            visibleIndices.append(index);
    QVERIFY(!visibleIndices.isEmpty()); QVERIFY(visibleIndices.first() > 0);
    if (activeOnly && !expandByWheel) QCOMPARE(targetBar->height(), defaultTheme().metrics.tabHeight);
    else QVERIFY(bottomRow > 0);
    const int expectedIndex = visibleIndices.size() > 1 ? visibleIndices[1] : visibleIndices[0];
    const auto destinationLabel = targetBar->displayTabRect(expectedIndex);
    const auto destination = targetBar->mapToGlobal(QPoint(destinationLabel.left() + 2, destinationLabel.center().y()));
    const int expectedRowTop = targetBar->mapToGlobal(destinationLabel.topLeft()).y();
    QVERIFY(targetView->viewportRectGlobal().contains(destination));
    sendMouse(carrier, QEvent::MouseMove, destination);
    targetView = groupView(main, targetGroup); QVERIFY(targetView); targetBar = targetView->tabBar();
    const auto gap = targetView->insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.height(), defaultTheme().metrics.tabHeight); QCOMPARE(gap.top(), expectedRowTop);
    QVERIFY(targetView->viewportRectGlobal().contains(gap)); QCOMPARE(targetView->insertionIndex(destination), expectedIndex);
    for (int index = 0; index < targetBar->count(); ++index) {
        const auto rect = targetBar->displayTabRect(index);
        if (!rect.isEmpty()) QVERIFY(!rect.translated(targetBar->mapToGlobal(QPoint())).intersects(gap));
        QCOMPARE(targetBar->tabData(index).toUuid(), targets[index]);
    }
    QVERIFY(!labelPreview()); QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Tab);
    QCOMPARE(manager.model().group(targetGroup)->tabs, targets);
    auto expected = targets; expected.insert(expectedIndex, source);
    sendMouse(carrier, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QTRY_COMPARE(manager.model().windows().size(), 1);
    QVERIFY(!manager.model().window(carrierId)); QVERIFY(!manager.model().group(carrierGroup));
    QCOMPARE(manager.model().groupForTab(source), targetGroup); QCOMPARE(manager.model().group(targetGroup)->tabs, expected);
    QCOMPARE(manager.model().centralGroups(window), QVector<GroupId>{targetGroup});
    QCOMPARE(manager.model().tab(source)->activePane, sourcePane); QCOMPARE(manager.paneWidget(sourcePane), sourceContent.data());
    QVERIFY(sourceContent); QVERIFY(sourceContent->isVisible());
    for (int index = 0; index < targets.size(); ++index) {
        QCOMPARE(manager.model().tab(targets[index])->activePane, targetPanes[index]);
        QCOMPARE(manager.paneWidget(targetPanes[index]), targetContents[index].data());
    }
    if (expandByWheel) {
        targetView = groupView(main, targetGroup); QVERIFY(targetView);
        QVERIFY(targetView->tabBar()->height() > defaultTheme().metrics.tabHeight);
        for (int index = 0; index < targetView->count(); ++index)
            QVERIFY(!targetView->tabBar()->displayTabRect(index).isEmpty());
        targetView->scrollByWheel({0, 120});
        QTRY_COMPARE(targetView->tabBar()->height(), defaultTheme().metrics.tabHeight);
        QCOMPARE(manager.paneWidget(sourcePane), sourceContent.data());
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!carrier); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate());
}

void TabStripLayoutTest::wheelExpandedRowsSurviveApiReconstructionUntilExplicitCollapse_data()
{
    insertionPreviewAndHitTestingUseThePointerRow_data();
}

void TabStripLayoutTest::wheelExpandedRowsSurviveApiReconstructionUntilExplicitCollapse()
{
    QFETCH(bool, bottom);
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.multiline = true; appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Rows reconstruction", {480, 340}});
    QVector<TabId> ids; QVector<PaneId> panes; QVector<QPointer<QWidget>> contents;
    for (int index = 0; index < 9; ++index) {
        QPointer<QWidget> content = new QLabel(QString("Content %1").arg(index)); contents.append(content);
        const auto tab = manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}}); ids.append(tab);
        panes.append(manager.model().tab(tab)->activePane);
    }
    manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto group = manager.model().groupForTab(ids.first());
    auto* view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QCOMPARE(view->tabBar()->height(), defaultTheme().metrics.tabHeight);
    view->scrollByWheel({0, -120}); QTRY_VERIFY(view->tabBar()->height() > defaultTheme().metrics.tabHeight);
    const auto created = manager.addTab(window, new QLabel("Created while expanded"), {"MMMM new tab", "label", {}}, group);
    QVERIFY(!created.isNull()); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QTRY_VERIFY(view->tabBar()->height() > defaultTheme().metrics.tabHeight);
    QCOMPARE(view->count(), 10);
    for (int index = 0; index < view->count(); ++index) QVERIFY(!view->tabBar()->displayTabRect(index).isEmpty());
    // A second, independent API mutation replaces the presentation again.
    QVERIFY(manager.setTabIconVisible(ids.first(), false)); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QVERIFY(view->tabBar()->height() > defaultTheme().metrics.tabHeight);
    for (int index = 0; index < ids.size(); ++index) {
        QCOMPARE(manager.model().tab(ids[index])->activePane, panes[index]);
        QCOMPARE(manager.paneWidget(panes[index]), contents[index].data());
    }
    view->scrollByWheel({0, 120}); QTRY_COMPARE(view->tabBar()->height(), defaultTheme().metrics.tabHeight);
    const auto collapsedTab = manager.addTab(window, new QLabel("Created while collapsed"), {"MMMM final tab", "label", {}}, group);
    QVERIFY(!collapsedTab.isNull()); view = groupView(manager.windowWidget(window), group); QVERIFY(view);
    QCOMPARE(view->tabBar()->height(), defaultTheme().metrics.tabHeight);
    QCOMPARE(view->count(), 11); QVERIFY(manager.model().validate());
}

void TabStripLayoutTest::activeOnlyLaterRowDragNeverFallsBackToFirstRow_data()
{
    QTest::addColumn<bool>("bottom"); QTest::addColumn<bool>("cancel");
    QTest::newRow("top-reorder") << false << false;
    QTest::newRow("bottom-reorder") << true << false;
    QTest::newRow("top-cancel") << false << true;
    QTest::newRow("bottom-cancel") << true << true;
}

void TabStripLayoutTest::activeOnlyLaterRowDragNeverFallsBackToFirstRow()
{
    QFETCH(bool, bottom); QFETCH(bool, cancel);
    const auto cleanup = qScopeGuard([] {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.multiline = true; appearance.wheelExpandRows = true; appearance.showNewTabButton = false;
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Active row drag", {480, 340}});
    QVector<TabId> ids; QVector<PaneId> panes; QVector<QPointer<QWidget>> contents;
    for (int index = 0; index < 12; ++index) {
        QPointer<QWidget> content = new QLabel(QString("Content %1").arg(index)); contents.append(content);
        const auto tab = manager.addTab(window, content, {QString("MMMM tab %1").arg(index + 1), "label", {}});
        ids.append(tab); panes.append(manager.model().tab(tab)->activePane);
    }
    auto* main = manager.windowWidget(window); manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(main));
    const auto group = manager.model().groupForTab(ids.first()); auto* view = groupView(main, group); QVERIFY(view);
    view->scrollByWheel({0, -120}); auto* bar = view->tabBar(); QTRY_VERIFY(bar->rowCount() >= 3);
    int firstLater = 0;
    while (firstLater < bar->count() && bar->displayTabRect(firstLater).top() == 0) ++firstLater;
    QVERIFY(firstLater + 1 < bar->count());
    const int sourceIndex = firstLater + 1;
    QCOMPARE(bar->displayTabRect(sourceIndex).top(), bar->displayTabRect(firstLater).top());
    const auto source = ids[sourceIndex]; QVERIFY(manager.activateTab(source));
    view = groupView(main, group); QVERIFY(view); view->scrollByWheel({0, 120}); bar = view->tabBar();
    QTRY_COMPARE(bar->height(), defaultTheme().metrics.tabHeight);
    QVERIFY(bar->displayTabRect(0).isEmpty()); QVERIFY(!bar->displayTabRect(sourceIndex).isEmpty());
    QVector<TabId> originalVisible;
    for (int index = 0; index < bar->count(); ++index)
        if (!bar->displayTabRect(index).isEmpty()) originalVisible.append(bar->tabData(index).toUuid());
    QVERIFY(originalVisible.contains(source)); QVERIFY(originalVisible.contains(ids[firstLater]));
    auto withoutSource = originalVisible; withoutSource.removeOne(source);
    const auto before = manager.model().toJson();
    const auto start = bar->mapToGlobal(bar->displayTabRect(sourceIndex).center() - QPoint(12, 0));
    sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(bar, QEvent::MouseMove, start + QPoint(QApplication::startDragDistance() + 3, 0));
    QTRY_VERIFY(labelPreview()); QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().groupForTab(source), group); QVERIFY(contents[sourceIndex]->isVisible());
    view = groupView(main, group); QVERIFY(view); bar = view->tabBar();
    QVector<TabId> visibleDuringDrag;
    for (int index = 0; index < bar->count(); ++index)
        if (!bar->displayTabRect(index).isEmpty()) visibleDuringDrag.append(bar->tabData(index).toUuid());
    QCOMPARE(visibleDuringDrag, withoutSource);
    QVERIFY(bar->displayTabRect(sourceIndex).isEmpty()); QVERIFY(bar->displayTabRect(0).isEmpty());
    auto gap = view->insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty());
    QCOMPARE(gap.height(), defaultTheme().metrics.tabHeight); QVERIFY(view->viewportRectGlobal().contains(gap));
    const auto firstLabel = bar->displayTabRect(firstLater); QVERIFY(!firstLabel.isEmpty());
    const auto destination = bar->mapToGlobal(QPoint(firstLabel.left() + 2, firstLabel.center().y()));
    auto* receiver = QWidget::mouseGrabber(); QVERIFY(receiver);
    sendMouse(receiver, QEvent::MouseMove, destination);
    view = groupView(main, group); QVERIFY(view); bar = view->tabBar();
    visibleDuringDrag.clear();
    for (int index = 0; index < bar->count(); ++index)
        if (!bar->displayTabRect(index).isEmpty()) visibleDuringDrag.append(bar->tabData(index).toUuid());
    QCOMPARE(visibleDuringDrag, withoutSource); QCOMPARE(view->insertionIndex(destination), firstLater);
    QCOMPARE(manager.model().toJson(), before); QCOMPARE(manager.model().windows().size(), 1);
    if (cancel) {
        QTest::keyClick(receiver, Qt::Key_Escape);
        QCOMPARE(manager.model().toJson(), before); QCOMPARE(manager.model().group(group)->tabs, ids);
    } else {
        auto expected = ids; expected.removeOne(source); expected.insert(firstLater, source);
        sendMouse(receiver, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(manager.model().group(group)->tabs, expected);
    }
    QTRY_VERIFY(!labelPreview()); QVERIFY(!QWidget::mouseGrabber()); QCOMPARE(manager.model().windows().size(), 1);
    view = groupView(main, group); QVERIFY(view); bar = view->tabBar();
    QCOMPARE(bar->height(), defaultTheme().metrics.tabHeight); QVERIFY(bar->displayTabRect(0).isEmpty());
    if (cancel) {
        QVector<TabId> restoredVisible;
        for (int index = 0; index < bar->count(); ++index)
            if (!bar->displayTabRect(index).isEmpty()) restoredVisible.append(bar->tabData(index).toUuid());
        QCOMPARE(restoredVisible, originalVisible);
    }
    for (int index = 0; index < ids.size(); ++index) {
        QCOMPARE(manager.model().tab(ids[index])->activePane, panes[index]);
        QCOMPARE(manager.paneWidget(panes[index]), contents[index].data());
    }
    QVERIFY(contents[sourceIndex]->isVisible()); QVERIFY(manager.model().validate());
}

QTEST_MAIN(TabStripLayoutTest)
#include "tab_strip_layout_test.moc"
