// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "authored_mouse_input.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QFocusEvent>
#include <QImage>
#include <QJsonArray>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSlider>
#include <QAbstractItemView>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QSet>
#include <QTest>
#include <QtTest/qtestspontaneevent.h>
#include <QTreeWidget>
#include <QtMath>
#include <memory>

using namespace window_system;

namespace {

class MouseMoveProbe final : public QObject {
public:
    QPoint lastPosition{-1, -1};
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseMove)
            lastPosition = static_cast<QMouseEvent*>(event)->position().toPoint();
        return QObject::eventFilter(watched, event);
    }
};

void processDeletes()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QWidget* pageFor(WindowManager& manager, const QString& kind)
{
    for (const auto& tab : manager.model().tabs()) for (const auto pane : manager.model().panes(tab.id)) {
        auto* page = manager.paneWidget(pane);
        if (page && page->property("windowSystemSettingsKind").toString() == kind) return page;
    }
    return nullptr;
}

TabId tabFor(WindowManager& manager, QWidget* page)
{
    for (const auto& tab : manager.model().tabs()) for (const auto pane : manager.model().panes(tab.id))
        if (manager.paneWidget(pane) == page) return tab.id;
    return {};
}

int visibleSettingRows(QWidget* page)
{
    int count = 0;
    for (auto* row : page->findChildren<QWidget*>(QStringLiteral("SettingRow")))
        if (!row->isHidden()) ++count;
    return count;
}

QSet<QString> settingKeys(QWidget* page)
{
    QSet<QString> result;
    for (auto* row : page->findChildren<QWidget*>(QStringLiteral("SettingRow")))
        result.insert(row->property("settingId").toString());
    return result;
}

QSet<QString> allSettingKeys()
{
    return {"window.newTabPosition", "window.dragOutsideKind", "window.closeEmptyWorkbench", "window.revealDuringDrag", "window.revealDelayMs",
            "window.zenFullscreenMode", "window.hideToolbarOptionsWhenLocked", "window.captionButtonOrder", "window.visibility.toolbars", "window.visibility.statusBar",
            "window.visibility.tabBars", "window.visibility.sideBars", "window.hideSingleTabWorkbench", "window.hideSingleTabFloating",
            "tabs.centralPlacement", "tabs.pinnedPlacement", "tabs.multiline", "tabs.wheelExpandRows", "tabs.showIcons", "tabs.showNewTabButton", "tabs.showNumbers", "tabs.showSidebarIcons", "tabs.showSidebarNumbers", "tabs.clipTitles", "tabs.maximumTitleWidth", "tabs.clipSidebarTitles", "tabs.maximumSidebarTitleWidth", "tabs.colors"};
}

QRect physicalRect(const QRect& logical, qreal ratio)
{
    const int left = qFloor(logical.left() * ratio);
    const int top = qFloor(logical.top() * ratio);
    return QRect(left, top, qCeil((logical.right() + 1) * ratio) - left,
                 qCeil((logical.bottom() + 1) * ratio) - top);
}

QRect colorBounds(const QImage& image, const QRect& region, const QColor& color)
{
    QRect bounds;
    const auto clipped = region.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y)
        for (int x = clipped.left(); x <= clipped.right(); ++x)
            if (image.pixelColor(x, y) == color) bounds |= QRect(x, y, 1, 1);
    return bounds;
}

} // namespace

class PaneSettingsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void wheelOverValueControlsScrollsThePageWithoutEditing();
    void wheelNeverEditsAnyValueControl();
    void paneBarsShareCompactSlotsAndFollowFocus();
    void extractedPaneCarriesItsOwnBarsAndPreservesTabFallback();
    void paneBarAdoptionRejectsInvalidAndAlreadyManagedWidgets();
    void settingsOpenAsOneMovableWorkspaceTab();
    void settingsSearchAndCategoryNavigationKeepRealRows();
    void settingsCategoryNavigationScrollsAndFollowsTheViewport();
    void settingsCategoryTreeSupportsKeyboardExpansion();
    void settingsDisclosureHoverChangesOnlyTheChevron();
    void settingsCategoryIndicatorUsesItsOwnSpacedGutter();
    void settingsSearchKeepsMatchingCategoriesAndTheirAncestors();
    void settingControlsUpdateImmediatelyAndReflectExternalChanges();
    void titleWidthControlsKeepFocusDuringContinuousInput_data();
    void titleWidthControlsKeepFocusDuringContinuousInput();
    void newTabPositionComboSupportsKeyboardAndExternalSynchronization_data();
    void newTabPositionComboSupportsKeyboardAndExternalSynchronization();
    void newTabPositionSearchPreservesTheExistingSettingsAndNavigation();
    void omittedNewTabPositionUsesItsDefault();
    void settingNotificationsAllowSynchronousHostDestruction_data();
    void settingNotificationsAllowSynchronousHostDestruction();
    void singleTabPoliciesHaveIndependentSettingsAndUpdateTheirWindows();
    void fullscreenAndZenAreCommandsInsteadOfPersistentSettings();
    void internalSettingsRestoreWithoutHostContentFactory();
    void internalSettingsSplitAndExtractWithoutHostContentFactory();
    void shortcutsUseOneTabAndEditTheCommandRegistry();
    void settingsRowsUseTheWorkspaceSurfaceInsteadOfTheSystemPalette();
};

void PaneSettingsTest::initTestCase()
{
    qApp->setQuitOnLastWindowClosed(false);
}

void PaneSettingsTest::cleanup()
{
    processDeletes();
}

void PaneSettingsTest::paneBarsShareCompactSlotsAndFollowFocus()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* first = new QPlainTextEdit;
    const auto tab = manager.addTab(window, first, {QStringLiteral("compact"), QStringLiteral("test.editor"), {}});
    const auto firstPane = manager.model().tab(tab)->activePane;
    auto* second = new QPlainTextEdit;
    const auto secondPane = manager.splitPane(tab, firstPane, second, Placement::Right);
    QVERIFY(!secondPane.isNull());
    auto* fallback = new QLabel(QStringLiteral("tab fallback"));
    auto* firstTop = new QLabel(QStringLiteral("first navigation"));
    auto* firstBottom = new QLabel(QStringLiteral("first status"));
    auto* secondTop = new QLabel(QStringLiteral("second navigation"));
    auto* secondBottom = new QLabel(QStringLiteral("second status"));
    QVERIFY(manager.setTabTopBar(tab, fallback));
    QVERIFY(manager.setPaneTopBar(firstPane, firstTop));
    QVERIFY(manager.setPaneBottomBar(firstPane, firstBottom));
    QVERIFY(manager.setPaneTopBar(secondPane, secondTop));
    QVERIFY(manager.setPaneBottomBar(secondPane, secondBottom));
    manager.showWindow(window);
    QVERIFY(manager.focusPane(tab, firstPane));
    QCoreApplication::processEvents();
    QVERIFY(firstTop->isVisible());
    QVERIFY(firstBottom->isVisible());
    QVERIFY(!secondTop->isVisible());
    QVERIFY(!secondBottom->isVisible());
    QVERIFY(!fallback->isVisible());
    QCOMPARE(firstTop->parentWidget()->objectName(), QStringLiteral("TabPage"));
    QCOMPARE(firstBottom->parentWidget(), firstTop->parentWidget());
    QVERIFY(manager.windowWidget(window)->findChildren<QWidget*>(QStringLiteral("PaneHeader")).isEmpty());
    for (auto* frame : manager.windowWidget(window)->findChildren<QWidget*>(QStringLiteral("PaneHost")))
        QVERIFY(frame->findChildren<QPushButton*>().isEmpty());

    // Direct content focus uses the manager's input routing, not only the
    // explicit API path; both shared slots must switch to the focused pane.
    QFocusEvent focus(QEvent::FocusIn, Qt::MouseFocusReason);
    QApplication::sendEvent(second, &focus);
    QCOMPARE(manager.model().tab(tab)->activePane, secondPane);
    QVERIFY(secondTop->isVisible());
    QVERIFY(secondBottom->isVisible());
    QVERIFY(!firstTop->isVisible());
    QVERIFY(!firstBottom->isVisible());
    QCOMPARE(secondTop->parentWidget(), firstTop->parentWidget());
    QVERIFY(manager.setPaneTopBar(secondPane, nullptr));
    QVERIFY(fallback->isVisible());
    QVERIFY(secondBottom->isVisible());
}

void PaneSettingsTest::extractedPaneCarriesItsOwnBarsAndPreservesTabFallback()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QPlainTextEdit,
                                   {QStringLiteral("source"), QStringLiteral("test.editor"), {}});
    const auto first = manager.model().tab(tab)->activePane;
    auto* content = new QPlainTextEdit;
    const auto second = manager.splitPane(tab, first, content, Placement::Right);
    QPointer<QLabel> top = new QLabel(QStringLiteral("pane-specific navigation"));
    QPointer<QLabel> bottom = new QLabel(QStringLiteral("pane-specific status"));
    QPointer<QLabel> fallback = new QLabel(QStringLiteral("original tab navigation"));
    QVERIFY(manager.setTabTopBar(tab, fallback));
    QVERIFY(manager.setPaneTopBar(second, top));
    QVERIFY(manager.setPaneBottomBar(second, bottom));
    manager.showWindow(window);
    const auto extracted = manager.extractPane(tab, second);
    QVERIFY(!extracted.isNull());
    QVERIFY(extracted != tab);
    QCOMPARE(manager.paneWidget(second), content);
    QVERIFY(top);
    QVERIFY(bottom);
    QVERIFY(top->isVisible());
    QVERIFY(bottom->isVisible());
    auto* target = manager.windowWidget(manager.model().group(manager.model().groupForTab(extracted))->windowId);
    QCOMPARE(top->window(), target);
    QCOMPARE(bottom->window(), target);
    QCOMPARE(fallback->window(), manager.windowWidget(window));
    QVERIFY(fallback->isVisible());
    QVERIFY(manager.closeTab(extracted));
    processDeletes();
    QVERIFY(top.isNull());
    QVERIFY(bottom.isNull());
    QVERIFY(fallback);
}

void PaneSettingsTest::paneBarAdoptionRejectsInvalidAndAlreadyManagedWidgets()
{
    WindowManager manager;
    auto* unadopted = new QLabel(QStringLiteral("caller owned"));
    QVERIFY(!manager.setPaneTopBar(QUuid::createUuid(), unadopted));
    QCOMPARE(unadopted->parentWidget(), nullptr);
    QVERIFY(!unadopted->property("windowSystemManaged").toBool());
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QPlainTextEdit, {QStringLiteral("owner"), {}, {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(manager.setPaneTopBar(pane, unadopted));
    const auto parent = unadopted->parentWidget();
    QVERIFY(!manager.setPaneBottomBar(pane, unadopted));
    QCOMPARE(unadopted->parentWidget(), parent);
}

void PaneSettingsTest::settingsOpenAsOneMovableWorkspaceTab()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showWindow(window);
    manager.showSettings(window);
    QCOMPARE(manager.model().tabs().size(), 1);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    QCOMPARE(page->objectName(), QStringLiteral("SettingsPage"));
    QVERIFY(!qobject_cast<QDialog*>(page));
    const auto tab = tabFor(manager, page);
    QVERIFY(!tab.isNull());
    manager.showSettings(window);
    QCOMPARE(manager.model().tabs().size(), 1);
    QCOMPARE(pageFor(manager, QStringLiteral("window_system.settings")), page);
    const auto floating = manager.detachTab(tab);
    QVERIFY(!floating.isNull());
    manager.showSettings(window);
    QCOMPARE(manager.model().tabs().size(), 1);
    QCOMPARE(page->window(), manager.windowWidget(floating));
    QCOMPARE(manager.context().tab, tab);
}

void PaneSettingsTest::settingsSearchAndCategoryNavigationKeepRealRows()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    auto* search = page->findChild<QLineEdit*>(QStringLiteral("SettingsSearch"));
    auto* categories = page->findChild<QTreeWidget*>(QStringLiteral("SettingsCategories"));
    QVERIFY(search);
    QVERIFY(categories);
    QCOMPARE(categories->topLevelItemCount(), 1);
    auto* windowCategory = categories->topLevelItem(0);
    QCOMPARE(windowCategory->text(0), QStringLiteral("窗口"));
    QCOMPARE(windowCategory->childCount(), 3);
    QCOMPARE(windowCategory->child(0)->text(0), QStringLiteral("拖拽"));
    QCOMPARE(windowCategory->child(1)->text(0), QStringLiteral("显示"));
    QCOMPARE(windowCategory->child(0)->data(0, Qt::UserRole).toString(), QStringLiteral("window/drag"));
    QCOMPARE(windowCategory->child(1)->data(0, Qt::UserRole).toString(), QStringLiteral("window/display"));
    QCOMPARE(windowCategory->child(2)->text(0), QStringLiteral("标签外观"));
    QCOMPARE(windowCategory->child(2)->data(0, Qt::UserRole).toString(), QStringLiteral("window/tabs"));
    QCOMPARE(settingKeys(page), allSettingKeys());
    QCOMPARE(page->findChildren<QWidget*>(QStringLiteral("SettingRow")).size(), allSettingKeys().size());
    QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
    categories->setCurrentItem(windowCategory->child(0));
    QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
    search->setText(QStringLiteral("window.revealDelayMs"));
    QCOMPARE(visibleSettingRows(page), 1);
    categories->setCurrentItem(windowCategory);
    QCOMPARE(visibleSettingRows(page), 1);
    search->setText(QStringLiteral("拖拽"));
    QCOMPARE(visibleSettingRows(page), 2);
    categories->setCurrentItem(windowCategory);
    QCOMPARE(visibleSettingRows(page), 2);
    search->setText(QStringLiteral("窗口"));
    QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
    search->clear();
    QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
}

void PaneSettingsTest::settingsCategoryNavigationScrollsAndFollowsTheViewport()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    auto* categories = page->findChild<QTreeWidget*>(QStringLiteral("SettingsCategories"));
    auto* scroll = page->findChild<QScrollArea*>(QStringLiteral("SettingsScrollArea"));
    QVERIFY(categories && scroll);
    auto* root = categories->topLevelItem(0);
    auto* drag = root->child(0);
    auto* display = root->child(1);
    manager.windowWidget(window)->resize(780, 480);
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    QTest::mouseClick(categories->viewport(), Qt::LeftButton, Qt::NoModifier,
                     categories->visualItemRect(drag).center());
    QVERIFY(scroll->verticalScrollBar()->value() > 0);
    QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
    QVERIFY(drag->font(0).bold());
    QVERIFY(!root->font(0).bold());
    scroll->verticalScrollBar()->setValue(0);
    QCOMPARE(categories->currentItem(), root);
    QVERIFY(root->font(0).bold());
    QVERIFY(!drag->font(0).bold());
    QTest::mouseClick(categories->viewport(), Qt::LeftButton, Qt::NoModifier,
                     categories->visualItemRect(display).center());
    QCOMPARE(categories->currentItem(), display);
    QVERIFY(display->font(0).bold());
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    auto* tabs = root->child(2);
    QCOMPARE(categories->currentItem(), tabs);
    QVERIFY(tabs->font(0).bold());
    QVERIFY(!display->font(0).bold());
    QVERIFY(!drag->font(0).bold());
    QVERIFY(!root->font(0).bold());
    QCOMPARE(categories->frameShape(), QFrame::NoFrame);
    auto* divider = page->findChild<QFrame*>(QStringLiteral("SettingsCategoryDivider"));
    QVERIFY(divider);
    QCOMPARE(divider->width(), 1);
}

void PaneSettingsTest::settingsCategoryTreeSupportsKeyboardExpansion()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    auto* categories = page->findChild<QTreeWidget*>(QStringLiteral("SettingsCategories"));
    QVERIFY(categories);
    auto* root = categories->topLevelItem(0);
    auto* drag = root->child(0);
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    categories->setCurrentItem(root);
    categories->setFocus();
    QVERIFY(root->isExpanded());
    QTest::keyClick(categories, Qt::Key_Left);
    QVERIFY(!root->isExpanded());
    QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
    QTest::keyClick(categories, Qt::Key_Right);
    QVERIFY(root->isExpanded());
    QTest::keyClick(categories, Qt::Key_Down);
    QCOMPARE(categories->currentItem(), drag);
    QVERIFY(drag->font(0).bold());
    QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
}

void PaneSettingsTest::settingsDisclosureHoverChangesOnlyTheChevron()
{
    WindowManager manager;
    auto theme = manager.theme();
    theme.surface = QColor("#F3F5F7");
    theme.mutedText = QColor("#8995A1");
    theme.text = QColor("#18212A");
    theme.accent = QColor("#6939C5");
    manager.setTheme(theme);
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    auto* categories = page->findChild<QTreeWidget*>(QStringLiteral("SettingsCategories"));
    QVERIFY(categories);
    auto* fixture = manager.windowWidget(window);
    fixture->setWindowFlag(Qt::WindowStaysOnTopHint);
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(fixture));
    fixture->raise(); fixture->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(fixture));
    auto* root = categories->topLevelItem(0);
    categories->setCurrentItem(root);
    const auto row = categories->visualItemRect(root);
    QVERIFY(!row.isEmpty());
    const QRect branch(0, row.top(), row.left(), row.height());
    const QPoint center(row.left() - categories->indentation() / 2, row.center().y());
    MouseMoveProbe movement;
    categories->viewport()->installEventFilter(&movement);
    const auto away = categories->viewport()->rect().bottomRight() - QPoint(8, 8);
    test::authoredMouseInput().takePriority();
    const auto releaseInput = qScopeGuard([] { test::authoredMouseInput().finish(); });
    const auto move = [&](QPoint local) {
        test::authoredMouseInput().takePriority();
        QMouseEvent event(QEvent::MouseMove, QPointF(local), QPointF(categories->viewport()->mapToGlobal(local)),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(categories->viewport(), &event);
        QCOMPARE(movement.lastPosition, local); // Delivery is synchronous.
        test::authoredMouseInput().takePriority();
        QApplication::processEvents();
    };
    move(away);
    QTRY_COMPARE(movement.lastPosition, away);
    const QImage normal = categories->viewport()->grab().toImage();
    const qreal ratio = normal.devicePixelRatio();
    const QRect pixels = physicalRect(branch, ratio);
    QVERIFY(!colorBounds(normal, pixels, theme.mutedText).isEmpty());
    const QRect glyphPixels = physicalRect(QRect(center - QPoint(5, 5), QSize(11, 11)), ratio);
    const QRect plainGutter = physicalRect(QRect(6, row.top(), row.left() - 6, row.height()), ratio);
    for (int y = plainGutter.top(); y <= plainGutter.bottom(); ++y)
        for (int x = plainGutter.left(); x <= plainGutter.right(); ++x)
            if (!glyphPixels.contains(x, y)) QCOMPARE(normal.pixelColor(x, y), theme.surface);
    move(center);
    // Exercise the real navigation widget with the authored component stream.
    QTRY_COMPARE(movement.lastPosition, center);
    QTRY_VERIFY(!colorBounds(categories->viewport()->grab().toImage(), pixels, theme.text).isEmpty());
    const QImage hovered = categories->viewport()->grab().toImage();
    QVERIFY(!colorBounds(hovered, pixels, theme.text).isEmpty());
    int changed = 0;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
        for (int x = pixels.left(); x <= pixels.right(); ++x) {
            const QColor before = normal.pixelColor(x, y);
            const QColor after = hovered.pixelColor(x, y);
            if (before == theme.surface) QCOMPARE(after, theme.surface);
            if (before != after) {
                ++changed;
                QVERIFY(glyphPixels.contains(x, y));
            }
        }
    }
    QVERIFY(changed > 0);
    move(away);
    QTRY_COMPARE(movement.lastPosition, away);
    QTRY_COMPARE(categories->viewport()->grab().toImage().copy(pixels), normal.copy(pixels));
}

void PaneSettingsTest::settingsCategoryIndicatorUsesItsOwnSpacedGutter()
{
    WindowManager manager;
    auto theme = manager.theme();
    theme.surface = QColor("#F3F5F7");
    theme.mutedText = QColor("#8995A1");
    theme.text = QColor("#18212A");
    theme.accent = QColor("#6939C5");
    manager.setTheme(theme);
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    auto* categories = page->findChild<QTreeWidget*>(QStringLiteral("SettingsCategories"));
    QVERIFY(categories);
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* root = categories->topLevelItem(0);
    auto* child = root->child(0);
    categories->setCurrentItem(root);
    QTest::mouseMove(categories->viewport(), categories->viewport()->rect().bottomRight() - QPoint(8, 8));
    const auto rootRow = categories->visualItemRect(root);
    const QImage image = categories->viewport()->grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    const QRect rootBranch = physicalRect(QRect(0, rootRow.top(), rootRow.left(), rootRow.height()), ratio);
    const QRect indicator = colorBounds(image, rootBranch, theme.accent);
    const QRect arrow = colorBounds(image, rootBranch, theme.mutedText);
    QVERIFY(!indicator.isEmpty());
    QVERIFY(!arrow.isEmpty());
    QVERIFY(arrow.left() - indicator.right() - 1 >= qCeil(8 * ratio));
    categories->setCurrentItem(child);
    const auto childRow = categories->visualItemRect(child);
    const QImage moved = categories->viewport()->grab().toImage();
    QVERIFY(colorBounds(moved, rootBranch, theme.accent).isEmpty());
    const QRect childBranch = physicalRect(QRect(0, childRow.top(), childRow.left(), childRow.height()), ratio);
    const QRect childIndicator = colorBounds(moved, childBranch, theme.accent);
    QVERIFY(!childIndicator.isEmpty());
    QCOMPARE(childIndicator.left(), indicator.left());
    QCOMPARE(childIndicator.width(), indicator.width());
    QVERIFY(child->font(0).bold());
    QVERIFY(!root->font(0).bold());
    const QPoint rootCenter(rootRow.left() - categories->indentation() / 2, rootRow.center().y());
    const QRect rootGlyph = physicalRect(QRect(rootCenter - QPoint(5, 5), QSize(11, 11)), ratio);
    for (int y = rootBranch.top(); y <= rootBranch.bottom(); ++y)
        for (int x = rootBranch.left(); x <= rootBranch.right(); ++x)
            if (!rootGlyph.contains(x, y)) QCOMPARE(moved.pixelColor(x, y), theme.surface);
    for (int y = childBranch.top(); y <= childBranch.bottom(); ++y)
        for (int x = childBranch.left(); x <= childBranch.right(); ++x)
            if (!childIndicator.contains(x, y)) QCOMPARE(moved.pixelColor(x, y), theme.surface);
}

void PaneSettingsTest::settingsSearchKeepsMatchingCategoriesAndTheirAncestors()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    auto* search = page->findChild<QLineEdit*>(QStringLiteral("SettingsSearch"));
    auto* categories = page->findChild<QTreeWidget*>(QStringLiteral("SettingsCategories"));
    QVERIFY(search && categories);
    auto* root = categories->topLevelItem(0);
    auto* drag = root->child(0);
    auto* display = root->child(1);
    const auto headings = page->findChildren<QLabel*>(QStringLiteral("SettingsCategoryHeading"));
    QCOMPARE(headings.size(), 4);
    QSet<QString> headingIds;
    for (auto* heading : headings) headingIds.insert(heading->property("categoryId").toString());
    QCOMPARE(headingIds, (QSet<QString>{"window", "window/drag", "window/display", "window/tabs"}));
    search->setText(QStringLiteral("window.revealDelayMs"));
    QCOMPARE(visibleSettingRows(page), 1);
    QVERIFY(!root->isHidden());
    QVERIFY(!drag->isHidden());
    QVERIFY(display->isHidden());
    for (auto* heading : headings)
        QCOMPARE(heading->isHidden(), heading->property("categoryId").toString() == QStringLiteral("window/display")
            || heading->property("categoryId").toString() == QStringLiteral("window/tabs"));
    search->setText(QStringLiteral("window.closeEmptyWorkbench"));
    QCOMPARE(visibleSettingRows(page), 1);
    QVERIFY(!root->isHidden());
    QVERIFY(drag->isHidden());
    QVERIFY(display->isHidden());
    for (auto* heading : headings)
        QCOMPARE(heading->isHidden(), heading->property("categoryId").toString() != QStringLiteral("window"));
    search->setText(QStringLiteral("no-such-setting"));
    QCOMPARE(visibleSettingRows(page), 0);
    QVERIFY(root->isHidden());
    QVERIFY(drag->isHidden());
    QVERIFY(display->isHidden());
    for (auto* heading : headings) QVERIFY(heading->isHidden());
    search->clear();
    QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
    QVERIFY(!root->isHidden());
    QVERIFY(!drag->isHidden());
    QVERIFY(!display->isHidden());
    QCOMPARE(settingKeys(page), allSettingKeys());
    for (auto* heading : headings) QVERIFY(!heading->isHidden());
}

void PaneSettingsTest::settingControlsUpdateImmediatelyAndReflectExternalChanges()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    auto* kind = page->findChild<QComboBox*>(QStringLiteral("DetachWindowKind"));
    auto* empty = page->findChild<QComboBox*>(QStringLiteral("EmptyWorkbenchPolicy"));
    auto* reveal = page->findChild<QCheckBox*>(QStringLiteral("RevealDuringDrag"));
    auto* delay = page->findChild<QSpinBox*>(QStringLiteral("RevealDelayMs"));
    auto* position = page->findChild<QComboBox*>(QStringLiteral("NewTabPosition"));
    QVERIFY(kind && empty && reveal && delay && position);
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    kind->setCurrentIndex(1);
    empty->setCurrentIndex(1);
    delay->setValue(900);
    reveal->setChecked(false);
    position->setCurrentIndex(position->findData(int(NewTabPosition::BeforeActive)));
    QCOMPARE(manager.behavior().dragOutsideKind, WindowKind::Workbench);
    QVERIFY(manager.behavior().closeEmptyWorkbench);
    QCOMPARE(manager.behavior().revealDelayMs, 900);
    QVERIFY(!manager.behavior().revealDuringDrag);
    QVERIFY(!delay->isEnabled());
    QCOMPARE(manager.behavior().newTabPosition, NewTabPosition::BeforeActive);
    QVERIFY(changes.count() >= 4);
    auto behavior = manager.behavior();
    behavior.dragOutsideKind = WindowKind::Floating;
    behavior.closeEmptyWorkbench = false;
    behavior.revealDuringDrag = true;
    behavior.revealDelayMs = 700;
    behavior.newTabPosition = NewTabPosition::End;
    manager.setBehavior(behavior);
    QCOMPARE(kind->currentIndex(), 0);
    QCOMPARE(empty->currentIndex(), 0);
    QVERIFY(reveal->isChecked());
    QCOMPARE(delay->value(), 700);
    QVERIFY(delay->isEnabled());
    QCOMPARE(position->currentData().toInt(), int(NewTabPosition::End));
}

void PaneSettingsTest::titleWidthControlsKeepFocusDuringContinuousInput_data()
{
    QTest::addColumn<bool>("floating");
    QTest::addColumn<bool>("sidebar");
    QTest::newRow("workbench-regular") << false << false;
    QTest::newRow("workbench-sidebar") << false << true;
    QTest::newRow("floating-regular") << true << false;
    QTest::newRow("floating-sidebar") << true << true;
}

void PaneSettingsTest::titleWidthControlsKeepFocusDuringContinuousInput()
{
    QFETCH(bool, floating);
    QFETCH(bool, sidebar);
    WindowManager manager;
    auto appearance = manager.tabAppearance();
    appearance.clipTitles = true;
    appearance.clipSidebarTitles = true;
    QVERIFY(manager.setTabAppearance(appearance));
    auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    if (floating) {
        window = manager.detachTab(tabFor(manager, page));
        QVERIFY(!window.isNull());
    }
    auto* control = page->findChild<QSpinBox*>(sidebar ? QStringLiteral("MaximumSidebarTabTitleWidth")
        : QStringLiteral("MaximumTabTitleWidth"));
    auto* search = page->findChild<QLineEdit*>(QStringLiteral("SettingsSearch"));
    auto* scroll = page->findChild<QScrollArea*>(QStringLiteral("SettingsScrollArea"));
    QVERIFY(control && search && scroll);
    manager.showWindow(window);
    auto* host = manager.windowWidget(window);
    QVERIFY(QTest::qWaitForWindowActive(host));
    scroll->ensureWidgetVisible(control);
    QCoreApplication::processEvents();
    control->setFocus();
    const QPointer<QWidget> editor = QApplication::focusWidget();
    QVERIFY(editor && (editor == control || control->isAncestorOf(editor)));
    const QString query = search->text();
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier);
    // Send each key to the actual focus owner: targeting the spin box after
    // every refresh would conceal focus being stolen between two digits.
    for (const auto digit : QStringLiteral("1732")) {
        QVERIFY(QApplication::focusWidget());
        QTest::keyClick(QApplication::focusWidget(), digit.toLatin1());
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::focusWidget(), editor.data());
        QCOMPARE(search->text(), query);
    }
    QCOMPARE(control->value(), 1732);
    QVERIFY(changes.size() >= 3);
    QCOMPARE(sidebar ? manager.tabAppearance().maximumSidebarTitleWidth
        : manager.tabAppearance().maximumTitleWidth, 1732);
    QCOMPARE(sidebar ? manager.tabAppearance().maximumTitleWidth
        : manager.tabAppearance().maximumSidebarTitleWidth, 160);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backspace);
    QCOMPARE(control->value(), 173);
    QCOMPARE(QApplication::focusWidget(), editor.data());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
    QCOMPARE(control->value(), 174);
    QCOMPARE(QApplication::focusWidget(), editor.data());
    const auto* line = control->findChild<QLineEdit*>();
    QVERIFY(line);
    const int cursor = line->cursorPosition();
    const auto text = line->text();
    appearance = manager.tabAppearance();
    appearance.showNumbers = !appearance.showNumbers;
    QVERIFY(manager.setTabAppearance(appearance));
    QCoreApplication::processEvents();
    QCOMPARE(QApplication::focusWidget(), editor.data());
    QCOMPARE(line->cursorPosition(), cursor);
    QCOMPARE(line->text(), text);
    QCOMPARE(search->text(), query);
}

void PaneSettingsTest::internalSettingsRestoreWithoutHostContentFactory()
{
    WindowManager original;
    const auto window = original.createWindow();
    original.showSettings(window);
    auto behavior = original.behavior();
    behavior.revealDelayMs = 1100;
    behavior.newTabPosition = NewTabPosition::Beginning;
    original.setBehavior(behavior);
    const auto snapshot = original.saveLayout();
    WindowManager restored;
    QVERIFY(restored.restoreLayout(snapshot));
    auto* page = pageFor(restored, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    QCOMPARE(restored.model().tabs().size(), 1);
    QCOMPARE(restored.behavior().revealDelayMs, 1100);
    auto* delay = page->findChild<QSpinBox*>(QStringLiteral("RevealDelayMs"));
    QVERIFY(delay);
    QCOMPARE(delay->value(), 1100);
    auto* position = page->findChild<QComboBox*>(QStringLiteral("NewTabPosition")); QVERIFY(position);
    QCOMPARE(restored.behavior().newTabPosition, NewTabPosition::Beginning);
    QCOMPARE(position->currentData().toInt(), int(NewTabPosition::Beginning));
    restored.showSettings(window);
    QCOMPARE(restored.model().tabs().size(), 1);
    QCOMPARE(pageFor(restored, QStringLiteral("window_system.settings")), page);
}

void PaneSettingsTest::shortcutsUseOneTabAndEditTheCommandRegistry()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    QVERIFY(manager.execute(QStringLiteral("preferences.openKeyboardShortcuts"), {}, window));
    auto* page = pageFor(manager, QStringLiteral("window_system.shortcuts"));
    QVERIFY(page);
    QVERIFY(!qobject_cast<QDialog*>(page));
    QVERIFY(manager.execute(QStringLiteral("preferences.openKeyboardShortcuts"), {}, window));
    QCOMPARE(manager.model().tabs().size(), 1);
    auto* search = page->findChild<QLineEdit*>(QStringLiteral("ShortcutSearch"));
    auto* tree = page->findChild<QTreeWidget*>(QStringLiteral("ShortcutCommands"));
    auto* capture = page->findChild<QKeySequenceEdit*>(QStringLiteral("ShortcutCapture"));
    auto* apply = page->findChild<QPushButton*>(QStringLiteral("ApplyShortcut"));
    QVERIFY(search && tree && capture && apply);
    search->setText(QStringLiteral("workspace.closeTab"));
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toString(), QStringLiteral("workspace.closeTab"));
    const QKeySequence binding(QStringLiteral("Ctrl+Alt+F12"));
    capture->setKeySequence(binding);
    QVERIFY(apply->isEnabled());
    apply->click();
    QCOMPARE(manager.commands().find(QStringLiteral("workspace.closeTab"))->shortcut, binding);
    capture->setKeySequence(manager.commands().find(QStringLiteral("window.select"))->shortcut);
    QVERIFY(!apply->isEnabled());
}

void PaneSettingsTest::internalSettingsSplitAndExtractWithoutHostContentFactory()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* original = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(original);
    const auto tab = tabFor(manager, original);
    const auto first = manager.model().tab(tab)->activePane;
    QVERIFY(manager.execute(QStringLiteral("pane.split.right"), {}, window));
    QCOMPARE(manager.model().panes(tab).size(), 2);
    const auto second = manager.model().tab(tab)->activePane;
    QVERIFY(second != first);
    auto* created = manager.paneWidget(second);
    QVERIFY(created);
    QCOMPARE(created->property("windowSystemSettingsKind").toString(), QStringLiteral("window_system.settings"));
    QVERIFY(created != original);
    const auto extracted = manager.extractPane(tab, second);
    QVERIFY(!extracted.isNull());
    QCOMPARE(manager.paneWidget(second), created);
    QCOMPARE(manager.paneWidget(first), original);
}

void PaneSettingsTest::settingsRowsUseTheWorkspaceSurfaceInsteadOfTheSystemPalette()
{
    WindowManager manager; const auto window = manager.createWindow();
    manager.showWindow(window); manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings")); QVERIFY(page);
    auto* scroll = page->findChild<QScrollArea*>(QStringLiteral("SettingsScrollArea")); QVERIFY(scroll);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* contents = scroll->widget(); QVERIFY(contents);
    QCOMPARE(contents->objectName(), QStringLiteral("SettingsRows"));
    const auto image = contents->grab().toImage(); QVERIFY(!image.isNull());
    QCOMPARE(image.pixelColor(0, 0), manager.theme().surface);
    auto changed = manager.theme(); changed.surface = QColor("#DDE5EA");
    manager.setTheme(changed);
    QCOMPARE(contents->grab().toImage().pixelColor(0, 0), changed.surface);
}

void PaneSettingsTest::newTabPositionComboSupportsKeyboardAndExternalSynchronization_data()
{
    QTest::addColumn<int>("choice"); QTest::addColumn<int>("policy");
    QTest::newRow("after-active-default") << 0 << int(NewTabPosition::AfterActive);
    QTest::newRow("before-active") << 1 << int(NewTabPosition::BeforeActive);
    QTest::newRow("beginning") << 2 << int(NewTabPosition::Beginning);
    QTest::newRow("end") << 3 << int(NewTabPosition::End);
}

void PaneSettingsTest::newTabPositionComboSupportsKeyboardAndExternalSynchronization()
{
    QFETCH(int, choice); QFETCH(int, policy);
    WindowManager manager; const auto window = manager.createWindow();
    QCOMPARE(manager.behavior().newTabPosition, NewTabPosition::AfterActive);
    auto behavior = manager.behavior();
    behavior.dragOutsideKind = WindowKind::Workbench; behavior.closeEmptyWorkbench = true;
    behavior.revealDuringDrag = false; behavior.revealDelayMs = 1100;
    manager.setBehavior(behavior); manager.showSettings(window); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* page = pageFor(manager, QStringLiteral("window_system.settings")); QVERIFY(page);
    auto* combo = page->findChild<QComboBox*>(QStringLiteral("NewTabPosition")); QVERIFY(combo);
    QCOMPARE(combo->count(), 4); QCOMPARE(combo->currentData().toInt(), int(NewTabPosition::AfterActive));
    QCOMPARE(combo->accessibleName(), QStringLiteral("新建标签的位置"));
    const QVector<int> ordered{int(NewTabPosition::AfterActive), int(NewTabPosition::BeforeActive),
                               int(NewTabPosition::Beginning), int(NewTabPosition::End)};
    for (int index = 0; index < ordered.size(); ++index) {
        QCOMPARE(combo->itemData(index).toInt(), ordered[index]); QVERIFY(!combo->itemText(index).isEmpty());
    }
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    combo->setFocus(); QTRY_VERIFY(combo->hasFocus());
    // Exercise QComboBox's actual keyboard handling, including a transition
    // away from the default, rather than assigning the selected index.
    QTest::keyClick(combo, Qt::Key_End); QTest::keyClick(combo, Qt::Key_Home);
    for (int index = 0; index < choice; ++index) QTest::keyClick(combo, Qt::Key_Down);
    QTest::keyClick(combo, Qt::Key_Return);
    QTRY_COMPARE(combo->currentData().toInt(), policy);
    QCOMPARE(manager.behavior().newTabPosition, NewTabPosition(policy)); QVERIFY(changes.size() >= 2);
    QCOMPARE(manager.behavior().dragOutsideKind, WindowKind::Workbench);
    QVERIFY(manager.behavior().closeEmptyWorkbench); QVERIFY(!manager.behavior().revealDuringDrag);
    QCOMPARE(manager.behavior().revealDelayMs, 1100);
    const auto snapshot = manager.saveLayout();
    QCOMPARE(snapshot.value("behavior").toObject().value("newTabPosition").toInt(), policy);
    WindowManager restored; QVERIFY(restored.restoreLayout(snapshot));
    auto* restoredPage = pageFor(restored, QStringLiteral("window_system.settings")); QVERIFY(restoredPage);
    auto* restoredCombo = restoredPage->findChild<QComboBox*>(QStringLiteral("NewTabPosition")); QVERIFY(restoredCombo);
    QCOMPARE(restored.behavior().newTabPosition, NewTabPosition(policy)); QCOMPARE(restoredCombo->currentData().toInt(), policy);
    QSignalSpy echoed(combo, &QComboBox::currentIndexChanged); changes.clear();
    const auto next = policy == int(NewTabPosition::Beginning) ? NewTabPosition::End : NewTabPosition::Beginning;
    behavior = manager.behavior(); behavior.newTabPosition = next; manager.setBehavior(behavior);
    QCOMPARE(combo->currentData().toInt(), int(next)); QCOMPARE(manager.behavior().newTabPosition, next);
    QCOMPARE(echoed.size(), 0); QCOMPARE(changes.size(), 1);
    QCOMPARE(manager.behavior().dragOutsideKind, WindowKind::Workbench);
    QVERIFY(manager.behavior().closeEmptyWorkbench); QVERIFY(!manager.behavior().revealDuringDrag);
    QCOMPARE(manager.behavior().revealDelayMs, 1100);
}

void PaneSettingsTest::newTabPositionSearchPreservesTheExistingSettingsAndNavigation()
{
    WindowManager manager; const auto window = manager.createWindow(); manager.showSettings(window); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* page = pageFor(manager, QStringLiteral("window_system.settings")); QVERIFY(page);
    auto* search = page->findChild<QLineEdit*>(QStringLiteral("SettingsSearch"));
    auto* categories = page->findChild<QTreeWidget*>(QStringLiteral("SettingsCategories"));
    auto* combo = page->findChild<QComboBox*>(QStringLiteral("NewTabPosition"));
    QVERIFY(search && categories && combo); QCOMPARE(settingKeys(page), allSettingKeys());
    const auto tab = tabFor(manager, page); const auto originalCount = manager.model().tabs().size();
    search->setFocus(); QTest::keyClicks(search, "window.newTabPosition");
    QTRY_COMPARE(visibleSettingRows(page), 1);
    for (auto* row : page->findChildren<QWidget*>(QStringLiteral("SettingRow")))
        QCOMPARE(row->isHidden(), row->property("settingId").toString() != QStringLiteral("window.newTabPosition"));
    QVERIFY(combo->isVisible()); QVERIFY(!categories->topLevelItem(0)->isHidden());
    QVERIFY(categories->topLevelItem(0)->child(0)->isHidden()); QVERIFY(categories->topLevelItem(0)->child(1)->isHidden());
    QTest::keyClick(search, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(search, Qt::Key_Backspace);
    QTRY_COMPARE(visibleSettingRows(page), allSettingKeys().size()); QCOMPARE(settingKeys(page), allSettingKeys());
    QCOMPARE(manager.model().tabs().size(), originalCount); QCOMPARE(tabFor(manager, page), tab);
    QVERIFY(!categories->topLevelItem(0)->child(0)->isHidden()); QVERIFY(!categories->topLevelItem(0)->child(1)->isHidden());
}

void PaneSettingsTest::omittedNewTabPositionUsesItsDefault()
{
    WindowManager original; const auto window = original.createWindow(); original.showSettings(window);
    auto behavior = original.behavior(); behavior.newTabPosition = NewTabPosition::End;
    behavior.revealDelayMs = 1100; original.setBehavior(behavior);
    auto snapshot = original.saveLayout(); auto settings = snapshot.value("behavior").toObject();
    settings.remove("newTabPosition"); snapshot.insert("behavior", settings);
    WindowManager restored; QVERIFY(restored.restoreLayout(snapshot));
    auto* page = pageFor(restored, QStringLiteral("window_system.settings")); QVERIFY(page);
    auto* combo = page->findChild<QComboBox*>(QStringLiteral("NewTabPosition")); QVERIFY(combo);
    QCOMPARE(restored.behavior().newTabPosition, NewTabPosition::AfterActive);
    QCOMPARE(combo->currentData().toInt(), int(NewTabPosition::AfterActive));
    QCOMPARE(restored.behavior().revealDelayMs, 1100);
    QCOMPARE(settingKeys(page), allSettingKeys());
}

void PaneSettingsTest::settingNotificationsAllowSynchronousHostDestruction_data()
{
    QTest::addColumn<QString>("control"); QTest::addColumn<bool>("deleteManager");
    for (const auto* control : {"NewTabPosition", "DetachWindowKind", "EmptyWorkbenchPolicy", "RevealDuringDrag", "RevealDelayMs",
                                "HideSingleTabWorkbench", "HideSingleTabFloating",
                                "ZenFullscreenMode", "HideToolbarOptionsWhenLocked", "CaptionButtonOrder", "window.visibility.toolbars", "window.visibility.statusBar",
                                "window.visibility.tabBars", "window.visibility.sideBars", "CentralTabPlacement", "PinnedTabPlacement",
                                "MultilineTabs", "WheelExpandTabRows", "ShowTabIcons", "ShowNewTabButton", "ShowTabNumbers",
                                "ClipTabTitles", "MaximumTabTitleWidth", "ClipSidebarTabTitles", "MaximumSidebarTabTitleWidth"}) {
        for (const bool deleteManager : {false, true}) {
            const auto row = QByteArray(control) + (deleteManager ? "-manager" : "-page");
            QTest::newRow(row.constData()) << QString::fromLatin1(control) << deleteManager;
        }
    }
}

void PaneSettingsTest::settingNotificationsAllowSynchronousHostDestruction()
{
    QFETCH(QString, control); QFETCH(bool, deleteManager);
    auto manager = std::make_unique<WindowManager>(); const QPointer<WindowManager> owner(manager.get());
    const auto window = manager->createWindow(); manager->showSettings(window);
    QPointer<QWidget> page = pageFor(*manager, QStringLiteral("window_system.settings")); QVERIFY(page);
    const auto tab = tabFor(*manager, page); const auto pane = manager->model().tab(tab)->activePane;
    if (control == "WheelExpandTabRows") {
        auto appearance = manager->tabAppearance(); appearance.multiline = true;
        QVERIFY(manager->setTabAppearance(appearance));
    }
    auto* target = page->findChild<QWidget*>(control); QVERIFY(target);
    QPointer<QWidget> windowObserver(manager->windowWidget(window)), targetObserver(target);
    int callbacks = 0; QJsonObject saved;
    connect(manager.get(), &WindowManager::layoutChanged, this, [&] {
        ++callbacks; saved = manager->saveLayout();
        if (deleteManager) manager.reset();
        else delete page.data();
    });
    // Qt setters can update accessibility after emitting their change signal;
    // deleting that same control inside the setter is outside this callback's
    // lifetime contract. Finish the real setter first, then emit the same real
    // signal directly so destruction still happens inside the production slot.
    // End each blocker before emission: its destructor must not touch a deleted
    // control. Keyboard-driven selection is covered separately above.
    if (auto* combo = qobject_cast<QComboBox*>(target)) {
        const int selected = combo->count() - 1;
        { const QSignalBlocker block(combo); combo->setCurrentIndex(selected); }
        QCOMPARE(combo->currentIndex(), selected);
        QVERIFY(QMetaObject::invokeMethod(combo, "currentIndexChanged", Qt::DirectConnection, Q_ARG(int, selected)));
    } else if (auto* check = qobject_cast<QCheckBox*>(target)) {
        const bool checked = !check->isChecked();
        { const QSignalBlocker block(check); check->setChecked(checked); }
        QCOMPARE(check->isChecked(), checked);
        QVERIFY(QMetaObject::invokeMethod(check, "toggled", Qt::DirectConnection, Q_ARG(bool, checked)));
    } else if (auto* spin = qobject_cast<QSpinBox*>(target)) {
        { const QSignalBlocker block(spin); spin->setValue(900); }
        QCOMPARE(spin->value(), 900);
        QVERIFY(QMetaObject::invokeMethod(spin, "valueChanged", Qt::DirectConnection, Q_ARG(int, 900)));
    } else QFAIL("Unknown setting control in lifetime fixture");
    QCOMPARE(callbacks, 1); QVERIFY(!saved.isEmpty());
    const auto settings = saved.value("behavior").toObject();
    if (control == "NewTabPosition") QCOMPARE(settings.value("newTabPosition").toInt(), int(NewTabPosition::End));
    else if (control == "DetachWindowKind") QCOMPARE(settings.value("dragOutsideFloating").toBool(), false);
    else if (control == "EmptyWorkbenchPolicy") QCOMPARE(settings.value("closeEmptyWorkbench").toBool(), true);
    else if (control == "RevealDuringDrag") QCOMPARE(settings.value("revealDuringDrag").toBool(), false);
    else if (control == "RevealDelayMs") QCOMPARE(settings.value("revealDelayMs").toInt(), 900);
    else if (control == "HideSingleTabWorkbench") QVERIFY(settings.value("hideSingleTabWorkbench").toBool());
    else if (control == "HideSingleTabFloating") QVERIFY(!settings.value("hideSingleTabFloating").toBool(true));
    else if (control == "ZenFullscreenMode") QCOMPARE(settings.value("zenFullscreenMode").toInt(), int(ViewMode::PaneFullscreen));
    else if (control == "HideToolbarOptionsWhenLocked") QVERIFY(settings.value("hideToolbarOptionsWhenLocked").toBool());
    else if (control == "CaptionButtonOrder") QCOMPARE(settings.value("captionButtonOrder").toString(), QStringLiteral("macos"));
    const auto appearance = saved.value("tabAppearance").toObject();
    if (control == "CentralTabPlacement") QCOMPARE(appearance.value("centralPlacement").toString(), QStringLiteral("bottom"));
    else if (control == "PinnedTabPlacement") QCOMPARE(appearance.value("pinnedPlacement").toString(), QStringLiteral("bottom"));
    else if (control == "MultilineTabs") QVERIFY(appearance.value("multiline").toBool());
    else if (control == "WheelExpandTabRows") QVERIFY(appearance.value("multiline").toBool() && appearance.value("wheelExpandRows").toBool());
    else if (control == "ShowTabIcons") QVERIFY(!appearance.value("showIcons").toBool(true));
    else if (control == "ShowNewTabButton") QVERIFY(!appearance.value("showNewTabButton").toBool(true));
    else if (control == "ShowTabNumbers") QVERIFY(appearance.value("showNumbers").toBool());
    else if (control == "ClipTabTitles") QVERIFY(appearance.value("clipTitles").toBool());
    else if (control == "MaximumTabTitleWidth") QCOMPARE(appearance.value("maximumTitleWidth").toInt(), 900);
    else if (control == "ClipSidebarTabTitles") QVERIFY(appearance.value("clipSidebarTitles").toBool());
    else if (control == "MaximumSidebarTabTitleWidth") QCOMPARE(appearance.value("maximumSidebarTitleWidth").toInt(), 900);
    const auto savedWindow = saved.value("windows").toArray().first().toObject();
    QVERIFY(!savedWindow.contains("viewState"));
    if (control.startsWith("window.visibility.")) {
        const auto field = control.mid(QStringLiteral("window.visibility.").size()) + QStringLiteral("Visible");
        QVERIFY(savedWindow.contains(field)); QCOMPARE(savedWindow.value(field).toBool(), false);
    }
    if (deleteManager) {
        QVERIFY(!owner); QVERIFY(!manager);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!page); QVERIFY(!targetObserver); QVERIFY(!windowObserver);
    } else {
        QVERIFY(owner); QVERIFY(manager); QVERIFY(!page); QVERIFY(!targetObserver); QVERIFY(windowObserver);
        QCOMPARE(manager->paneWidget(pane), nullptr); QVERIFY(manager->model().tab(tab));
    }
}

void PaneSettingsTest::fullscreenAndZenAreCommandsInsteadOfPersistentSettings()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings")); QVERIFY(page);
    QVERIFY(!page->findChild<QComboBox*>(QStringLiteral("WindowViewMode")));
    QVERIFY(!page->findChild<QCheckBox*>(QStringLiteral("WindowZenMode")));
    QVERIFY(!settingKeys(page).contains(QStringLiteral("window.viewMode")));
    QVERIFY(!settingKeys(page).contains(QStringLiteral("window.zenMode")));
    auto* preferred = page->findChild<QComboBox*>(QStringLiteral("ZenFullscreenMode")); QVERIFY(preferred);
    preferred->setCurrentIndex(preferred->findData(int(ViewMode::PaneFullscreen)));
    QCOMPARE(manager.behavior().zenFullscreenMode, ViewMode::PaneFullscreen);
    QCOMPARE(manager.viewMode(window), ViewMode::Normal); QVERIFY(!manager.zenMode(window));
    QVERIFY(manager.execute(QStringLiteral("window.zen.toggle"), {}, window));
    QCOMPARE(manager.viewMode(window), ViewMode::PaneFullscreen); QVERIFY(manager.zenMode(window));
    QVERIFY(!manager.saveLayout().value("windows").toArray().first().toObject().contains("viewState"));
    QVERIFY(manager.execute(QStringLiteral("window.fullscreen.exit"), {}, window));
    QCOMPARE(manager.viewMode(window), ViewMode::Normal); QVERIFY(!manager.zenMode(window));
    QCOMPARE(manager.behavior().zenFullscreenMode, ViewMode::PaneFullscreen);
    QCOMPARE(settingKeys(page), allSettingKeys());
}

void PaneSettingsTest::singleTabPoliciesHaveIndependentSettingsAndUpdateTheirWindows()
{
    WindowManager manager; const auto workbench = manager.createWindow();
    const auto floating = manager.createWindow(WindowKind::Floating, {}, workbench);
    const auto tab = manager.addTab(floating, new QLabel("Floating"), {"Floating", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane; auto* content = manager.paneWidget(pane);
    manager.showSettings(workbench); manager.showWindow(workbench); manager.showWindow(floating);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(workbench)));
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(floating)));
    auto* page = pageFor(manager, QStringLiteral("window_system.settings")); QVERIFY(page);
    auto* mainPolicy = page->findChild<QCheckBox*>(QStringLiteral("HideSingleTabWorkbench"));
    auto* floatPolicy = page->findChild<QCheckBox*>(QStringLiteral("HideSingleTabFloating"));
    QVERIFY(mainPolicy && floatPolicy); QVERIFY(!mainPolicy->isChecked()); QVERIFY(floatPolicy->isChecked());
    auto* mainSurface = static_cast<WorkspaceWindow*>(manager.windowWidget(workbench))->surface();
    auto* floatSurface = static_cast<WorkspaceWindow*>(manager.windowWidget(floating))->surface();
    QCOMPARE(mainSurface->tabBarRects().size(), 1); QCOMPARE(floatSurface->tabBarRects().size(), 0);
    mainPolicy->click();
    QVERIFY(manager.behavior().hideSingleTabWorkbench); QVERIFY(manager.behavior().hideSingleTabFloating);
    QTRY_COMPARE(mainSurface->tabBarRects().size(), 0); QCOMPARE(floatSurface->tabBarRects().size(), 0);
    floatPolicy->click();
    QVERIFY(manager.behavior().hideSingleTabWorkbench); QVERIFY(!manager.behavior().hideSingleTabFloating);
    QTRY_COMPARE(floatSurface->tabBarRects().size(), 1); QCOMPARE(mainSurface->tabBarRects().size(), 0);
    QSignalSpy echoedMain(mainPolicy, &QCheckBox::toggled), echoedFloat(floatPolicy, &QCheckBox::toggled);
    auto behavior = manager.behavior(); behavior.hideSingleTabWorkbench = false; behavior.hideSingleTabFloating = true;
    manager.setBehavior(behavior);
    QVERIFY(!mainPolicy->isChecked()); QVERIFY(floatPolicy->isChecked());
    QCOMPARE(echoedMain.size(), 0); QCOMPARE(echoedFloat.size(), 0);
    QTRY_COMPARE(mainSurface->tabBarRects().size(), 1); QCOMPARE(floatSurface->tabBarRects().size(), 0);
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(content->isVisible());
    QCOMPARE(settingKeys(page), allSettingKeys()); QCOMPARE(visibleSettingRows(page), allSettingKeys().size());
}

QTEST_MAIN(PaneSettingsTest)
namespace {
// A real wheel notch takes the platform path: it reaches the top-level
// QWindow, and Qt finds the widget under the cursor, gives Qt::WheelFocus
// widgets focus and propagates the event, as for a physical mouse.
bool sendWheel(QWidget* target, int notches = -1)
{
    auto* window = target->window()->windowHandle();
    if (!window) return false;
    const QPoint local = target->rect().center();
    const QPointF inWindow = QPointF(target->mapTo(target->window(), local));
    QWheelEvent wheel(inWindow, QPointF(target->mapToGlobal(local)), QPoint(),
        QPoint(0, 120 * notches), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QSpontaneKeyEvent::setSpontaneous(&wheel);
    return QApplication::sendEvent(window, &wheel);
}
} // namespace

void PaneSettingsTest::wheelOverValueControlsScrollsThePageWithoutEditing()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings")); QVERIFY(page);
    auto* scroll = page->findChild<QScrollArea*>(QStringLiteral("SettingsScrollArea")); QVERIFY(scroll);
    manager.windowWidget(window)->resize(780, 480);
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    auto* zen = page->findChild<QComboBox*>(QStringLiteral("ZenFullscreenMode")); QVERIFY(zen);
    auto* delay = page->findChild<QSpinBox*>(QStringLiteral("RevealDelayMs")); QVERIFY(delay);
    auto* order = page->findChild<QComboBox*>(QStringLiteral("CaptionButtonOrder")); QVERIFY(order);
    const auto before = manager.saveLayout().value("behavior");
    for (QWidget* control : {static_cast<QWidget*>(zen), static_cast<QWidget*>(delay), static_cast<QWidget*>(order)}) {
        scroll->ensureWidgetVisible(control);
        QCoreApplication::processEvents();
        const int zenIndex = zen->currentIndex(), delayValue = delay->value(), orderIndex = order->currentIndex();
        // Scroll down, then up, over the control: the page moves both ways.
        const int top = scroll->verticalScrollBar()->value();
        auto* target = control->childAt(control->rect().center()) ? control->childAt(control->rect().center()) : control;
        sendWheel(target, -1);
        const int down = scroll->verticalScrollBar()->value();
        sendWheel(target, 1);
        const int up = scroll->verticalScrollBar()->value();
        QVERIFY2(down != top || up != down, qPrintable(control->objectName()));
        QCOMPARE(zen->currentIndex(), zenIndex);
        QCOMPARE(delay->value(), delayValue);
        QCOMPARE(order->currentIndex(), orderIndex);
    }
    QCOMPARE(manager.saveLayout().value("behavior"), before);
    // The keyboard still changes values: only the wheel is redirected.
    delay->setFocus(); const int value = delay->value();
    QTest::keyClick(delay, Qt::Key_Up);
    QVERIFY(delay->value() > value);
}

void PaneSettingsTest::wheelNeverEditsAnyValueControl()
{
    WindowManager manager; // Installs the application-wide wheel rule.
    QScrollArea area; area.resize(300, 200);
    auto* content = new QWidget; auto* column = new QVBoxLayout(content);
    auto* combo = new QComboBox; combo->addItems({"a", "b", "c"}); combo->setCurrentIndex(1);
    auto* editable = new QComboBox; editable->setEditable(true); editable->addItems({"x", "y", "z"}); editable->setCurrentIndex(1);
    auto* spin = new QSpinBox; spin->setRange(0, 100); spin->setValue(50);
    auto* real = new QDoubleSpinBox; real->setRange(0, 10); real->setValue(5);
    auto* time = new QDateTimeEdit(QDateTime(QDate(2026, 10, 20), QTime(12, 0)));
    auto* slider = new QSlider(Qt::Horizontal); slider->setRange(0, 100); slider->setValue(50);
    auto* bar = new QScrollBar(Qt::Horizontal); bar->setRange(0, 100); bar->setValue(50);
    for (QWidget* widget : std::initializer_list<QWidget*>{combo, editable, spin, real, time, slider, bar}) column->addWidget(widget);
    column->addSpacing(1200);
    area.setWidget(content); area.show();
    QVERIFY(QTest::qWaitForWindowExposed(&area));
    // Qt only gives wheel focus inside the active window, as for the user.
    area.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&area));
    // Activation focuses the first control; start from the scroll area.
    area.setFocus(); QCOMPARE(area.focusWidget(), &area);
    for (QWidget* control : std::initializer_list<QWidget*>{combo, editable, spin, real, time, slider}) {
        area.verticalScrollBar()->setValue(0);
        auto* target = control->childAt(control->rect().center()) ? control->childAt(control->rect().center()) : control;
        sendWheel(target, -1);
        QVERIFY2(area.verticalScrollBar()->value() > 0, control->metaObject()->className());
        // The wheel neither focuses the control nor its inner editor (no
        // text cursor appears); a click or Tab still can.
        QVERIFY2(control->focusPolicy() != Qt::WheelFocus, control->metaObject()->className());
        QVERIFY2(area.focusWidget() == &area, control->metaObject()->className());
    }
    spin->setFocus(); QCOMPARE(area.focusWidget(), spin->focusProxy() ? spin->focusProxy() : spin);
    QCOMPARE(combo->currentIndex(), 1); QCOMPARE(editable->currentIndex(), 1);
    QCOMPARE(spin->value(), 50); QCOMPARE(real->value(), 5.0);
    QCOMPARE(time->dateTime(), QDateTime(QDate(2026, 10, 20), QTime(12, 0)));
    QCOMPARE(slider->value(), 50);
    // Scroll bars are for scrolling: the wheel still moves them.
    sendWheel(bar, -1);
    QVERIFY(bar->value() != 50);
    // An open combo popup is its own window: its list keeps scrolling.
    auto* longCombo = new QComboBox(content);
    for (int i = 0; i < 200; ++i) longCombo->addItem(QString::number(i));
    longCombo->show(); longCombo->showPopup();
    auto* view = longCombo->view(); QVERIFY(view);
    QTRY_VERIFY(view->isVisible());
    QTRY_VERIFY(view->verticalScrollBar()->maximum() > 0);
    const int listTop = view->verticalScrollBar()->value();
    sendWheel(view->viewport(), -1);
    QVERIFY(view->verticalScrollBar()->value() > listTop);
    longCombo->hidePopup();
}

#include "pane_settings_test.moc"
