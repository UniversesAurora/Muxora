// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>

#include <QCoreApplication>
#include <QLabel>
#include <QPointer>
#include <QSplitter>
#include <QTest>

using namespace window_system;

namespace {
TabId add(WindowManager& manager, WindowId window, const QString& title, GroupId group = {})
{
    return manager.addTab(window, new QLabel(title), {title, "test.text", {}}, group);
}
PaneId activePane(const WindowManager& manager, TabId tab)
{
    return manager.model().tab(tab)->activePane;
}
QWidget* ancestorNamed(QWidget* widget, const QString& name)
{
    for (; widget; widget = widget->parentWidget())
        if (widget->objectName() == name) return widget;
    return nullptr;
}
QWidget* frameOf(const WindowManager& manager, PaneId pane)
{
    return ancestorNamed(manager.paneWidget(pane), QStringLiteral("PaneHost"));
}
QWidget* pageOf(const WindowManager& manager, TabId tab)
{
    return ancestorNamed(manager.paneWidget(activePane(manager, tab)), QStringLiteral("TabPage"));
}
QSet<QWidget*> framesOf(const WindowManager& manager, TabId tab)
{
    QSet<QWidget*> result;
    for (const auto pane : manager.model().panes(tab)) result.insert(frameOf(manager, pane));
    return result;
}
void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
// Records every parent a watched widget receives.
class ParentRecorder final : public QObject {
public:
    void watch(QWidget* widget) { widget->installEventFilter(this); }
    int changes(QObject* widget) const { return int(parents_.value(widget).size()); }
    bool everParkedIn(QObject* widget, const QString& name) const
    {
        for (const auto& parent : parents_.value(widget))
            if (parent && parent->objectName() == name) return true;
        return false;
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::ParentChange)
            parents_[watched].append(static_cast<QWidget*>(watched)->parentWidget());
        return false;
    }
private:
    QHash<QObject*, QVector<QPointer<QWidget>>> parents_;
};
} // namespace

class ViewReuseTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { settle(); }
    void unrelatedTabsKeepTheirViewWidgets();
    void changedTreeRebuildsOnlyThatTab();
    void themeMetricChangeRebuilds();
};

void ViewReuseTest::unrelatedTabsKeepTheirViewWidgets()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto first = add(manager, window, "first");
    const auto group = manager.model().groupForTab(first);
    const auto second = add(manager, window, "second", group);
    const auto third = add(manager, window, "third", group);
    const auto thirdFirst = activePane(manager, third);
    const auto thirdSecond = manager.splitPane(third, thirdFirst, new QLabel("third-split"), Placement::Right);
    QVERIFY(!thirdSecond.isNull());
    settle();
    const auto secondPane = activePane(manager, second);
    const QVector<PaneId> panes{secondPane, thirdFirst, thirdSecond};
    QVector<QPointer<QWidget>> contents, frames, parents;
    for (const auto pane : panes) {
        contents << manager.paneWidget(pane);
        frames << frameOf(manager, pane);
        parents << manager.paneWidget(pane)->parentWidget();
        QVERIFY(frames.last());
    }
    const QPointer<QWidget> splitter = frames[1]->parentWidget();
    QVERIFY(qobject_cast<QSplitter*>(splitter.data()));
    QCOMPARE(splitter->objectName(), QStringLiteral("PaneSplitter"));
    QCOMPARE(frames[2]->parentWidget(), splitter.data());
    ParentRecorder recorder;
    for (const auto& content : contents) recorder.watch(content);

    const auto verify = [&](const char* step) {
        settle();
        for (int i = 0; i < panes.size(); ++i) {
            QVERIFY2(contents[i] && manager.paneWidget(panes[i]) == contents[i], step);
            QVERIFY2(frames[i] && frameOf(manager, panes[i]) == frames[i], step);
            QVERIFY2(contents[i]->parentWidget() == parents[i], step);
            QVERIFY2(recorder.changes(contents[i]) == 0, step);
        }
        QVERIFY2(splitter && frames[1]->parentWidget() == splitter, step);
    };
    QVERIFY(manager.closeTab(first));
    verify("close");
    const auto other = add(manager, window, "other");
    QVERIFY(manager.moveTab(other, group, Placement::Right));
    const auto moved = add(manager, window, "moved", manager.model().groupForTab(other));
    QVERIFY(manager.moveTab(moved, group, Placement::Center));
    verify("move");
    QVERIFY(manager.renameTab(second, "renamed"));
    verify("rename");
    QCOMPARE(manager.model().tab(second)->title, QStringLiteral("renamed"));
}

void ViewReuseTest::changedTreeRebuildsOnlyThatTab()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto first = add(manager, window, "first");
    const auto group = manager.model().groupForTab(first);
    const auto second = add(manager, window, "second", group);
    const auto third = add(manager, window, "third", group);
    QVERIFY(!manager.splitPane(third, activePane(manager, third), new QLabel("split"), Placement::Bottom).isNull());
    settle();
    const auto secondFrames = framesOf(manager, second);
    const auto thirdFrames = framesOf(manager, third);
    QCOMPARE(secondFrames.size(), 1);
    QCOMPARE(thirdFrames.size(), 2);
    QVector<QPointer<QWidget>> watchedThird;
    for (auto* frame : thirdFrames) watchedThird << frame;
    const QPointer<QWidget> oldSecondFrame = *secondFrames.cbegin();
    const auto secondContent = manager.paneWidget(activePane(manager, second));

    const auto created = manager.splitPane(second, activePane(manager, second), new QLabel("new"), Placement::Right);
    QVERIFY(!created.isNull());
    settle();
    const auto rebuilt = framesOf(manager, second);
    QCOMPARE(rebuilt.size(), 2);
    QVERIFY(!rebuilt.contains(oldSecondFrame.data()));
    QCOMPARE(manager.paneWidget(manager.model().panes(second).first()), secondContent);
    // Compare through QPointer: a rebuilt view would leave dangling pointers.
    const auto current = framesOf(manager, third);
    QCOMPARE(current.size(), thirdFrames.size());
    for (const auto& frame : watchedThird) {
        QVERIFY(frame);
        QVERIFY(current.contains(frame.data()));
    }
}

void ViewReuseTest::themeMetricChangeRebuilds()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = add(manager, window, "tab");
    const auto other = add(manager, window, "other", manager.model().groupForTab(tab));
    QVERIFY(!manager.splitPane(tab, activePane(manager, tab), new QLabel("split"), Placement::Right).isNull());
    settle();
    const auto splitterOf = [&] {
        return qobject_cast<QSplitter*>(frameOf(manager, activePane(manager, tab))->parentWidget());
    };
    QVERIFY(splitterOf());
    auto theme = manager.theme();
    const int width = qMax(1, theme.metrics.splitterWidth) + 5;
    QCOMPARE_NE(splitterOf()->handleWidth(), width);
    theme.metrics.splitterWidth = width;
    manager.setTheme(theme);
    // An unrelated change refreshes the views; reuse must still honour the
    // new metric instead of keeping the splitter built for the old theme.
    QVERIFY(manager.renameTab(other, "renamed"));
    settle();
    QVERIFY(splitterOf());
    QCOMPARE(splitterOf()->handleWidth(), width);
}

QTEST_MAIN(ViewReuseTest)
#include "view_reuse_test.moc"
