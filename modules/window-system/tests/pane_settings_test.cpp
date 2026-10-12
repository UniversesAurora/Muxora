// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QFocusEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QScrollArea>
#include <QTest>
#include <QTreeWidget>

using namespace window_system;

namespace {

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

} // namespace

class PaneSettingsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void paneBarsShareCompactSlotsAndFollowFocus();
    void extractedPaneCarriesItsOwnBarsAndPreservesTabFallback();
    void paneBarAdoptionRejectsInvalidAndAlreadyManagedWidgets();
    void settingsOpenAsOneMovableWorkspaceTab();
    void settingsSearchAndCategoryFilterRealRows();
    void settingControlsUpdateImmediatelyAndReflectExternalChanges();
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

void PaneSettingsTest::settingsSearchAndCategoryFilterRealRows()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.showSettings(window);
    auto* page = pageFor(manager, QStringLiteral("window_system.settings"));
    QVERIFY(page);
    auto* search = page->findChild<QLineEdit*>(QStringLiteral("SettingsSearch"));
    auto* categories = page->findChild<QListWidget*>(QStringLiteral("SettingsCategories"));
    QVERIFY(search);
    QVERIFY(categories);
    QCOMPARE(visibleSettingRows(page), 4);
    categories->setCurrentRow(1);
    QCOMPARE(visibleSettingRows(page), 2);
    search->setText(QStringLiteral("window.revealDelayMs"));
    QCOMPARE(visibleSettingRows(page), 0);
    categories->setCurrentRow(0);
    QCOMPARE(visibleSettingRows(page), 1);
    search->clear();
    QCOMPARE(visibleSettingRows(page), 4);
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
    QVERIFY(kind && empty && reveal && delay);
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    kind->setCurrentIndex(1);
    empty->setCurrentIndex(1);
    delay->setValue(900);
    reveal->setChecked(false);
    QCOMPARE(manager.behavior().dragOutsideKind, WindowKind::Workbench);
    QVERIFY(manager.behavior().closeEmptyWorkbench);
    QCOMPARE(manager.behavior().revealDelayMs, 900);
    QVERIFY(!manager.behavior().revealDuringDrag);
    QVERIFY(!delay->isEnabled());
    QVERIFY(changes.count() >= 4);
    auto behavior = manager.behavior();
    behavior.dragOutsideKind = WindowKind::Floating;
    behavior.closeEmptyWorkbench = false;
    behavior.revealDuringDrag = true;
    behavior.revealDelayMs = 700;
    manager.setBehavior(behavior);
    QCOMPARE(kind->currentIndex(), 0);
    QCOMPARE(empty->currentIndex(), 0);
    QVERIFY(reveal->isChecked());
    QCOMPARE(delay->value(), 700);
    QVERIFY(delay->isEnabled());
}

void PaneSettingsTest::internalSettingsRestoreWithoutHostContentFactory()
{
    WindowManager original;
    const auto window = original.createWindow();
    original.showSettings(window);
    auto behavior = original.behavior();
    behavior.revealDelayMs = 1100;
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

QTEST_MAIN(PaneSettingsTest)
#include "pane_settings_test.moc"
