// SPDX-License-Identifier: MIT
// Phase 10.4i: dialogs over Quick windows. Dimensions: field kind (text,
// several lines, number with an empty value), accepting by key or button,
// cancelling, a dialog that stops applying, one that closes by itself,
// raising an open one, commands that ask (rename, auto-hide panel size).
#include "quick_menu_test_support.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using namespace window_system;
using namespace quick_menu_test;

namespace {
QQuickWindow* dialogWindow(const QString& name)
{
    for (auto* window : QGuiApplication::topLevelWindows())
        if (window->objectName() == name && window->isVisible()) return qobject_cast<QQuickWindow*>(window);
    return nullptr;
}
QPoint centre(const QQuickItem* item) { return item->mapToScene(item->boundingRect().center()).toPoint(); }

struct Workspace {
    WindowManager manager{WindowManager::Presentation::External};
    QuickWorkspace workspace{manager};
    WindowId window;
    TabId tab;
    QQuickWindow* view = nullptr;
    Workspace()
    {
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(900, 700)});
        tab = manager.addTab(window, {QStringLiteral("First")});
        manager.addTab(window, {QStringLiteral("Second")});
    }
    bool show()
    {
        if (!QTest::qWaitFor([this] { return (view = workspace.window(window)); })) return false;
        view->requestActivate();
        return QTest::qWaitForWindowActive(view);
    }
};
} // namespace

class QuickDialogsTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*(Unable to assign|TypeError|ReferenceError|Binding loop|No such method).*"));
    }

    // Renaming asks for the name with the current one selected; Enter
    // applies it, Esc leaves the tab as it was.
    void renamingAsksForTheName()
    {
        Workspace w;
        QVERIFY(w.show());
        QVERIFY(w.manager.execute(QStringLiteral("tab.rename"), {}, w.window));
        QQuickWindow* dialog = nullptr;
        QTRY_VERIFY((dialog = dialogWindow(QStringLiteral("RenameTabDialog"))));
        QVERIFY(QTest::qWaitForWindowActive(dialog));
        QCOMPARE(dialog->transientParent(), w.view);
        QCOMPARE(dialog->modality(), Qt::WindowModal);
        auto* field = namedItem(dialog->contentItem(), QStringLiteral("text"));
        QVERIFY(field);
        QTRY_VERIFY(field->hasActiveFocus());
        QCOMPARE(field->property("selectedText").toString(), QStringLiteral("Second"));
        for (const char letter : QByteArrayLiteral("Notes")) QTest::keyClick(dialog, letter);
        QTest::keyClick(dialog, Qt::Key_Return);
        QTRY_COMPARE(w.manager.model().tab(w.manager.model().orderedTabs(w.window).last())->title, QStringLiteral("Notes"));
        QTRY_VERIFY(!dialogWindow(QStringLiteral("RenameTabDialog")));

        QVERIFY(w.manager.execute(QStringLiteral("tab.rename"), {}, w.window));
        QTRY_VERIFY((dialog = dialogWindow(QStringLiteral("RenameTabDialog"))));
        QVERIFY(QTest::qWaitForWindowActive(dialog));
        QTest::keyClick(dialog, 'x');
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialogWindow(QStringLiteral("RenameTabDialog")));
        QCOMPARE(w.manager.model().tab(w.manager.model().orderedTabs(w.window).last())->title, QStringLiteral("Notes"));
    }

    // A dialog about a tab closes with the tab.
    void aDialogClosesOnceItNoLongerApplies()
    {
        Workspace w;
        QVERIFY(w.show());
        QVERIFY(w.manager.activateTab(w.tab));
        QVERIFY(w.manager.execute(QStringLiteral("tab.rename"), {}, w.window));
        QTRY_VERIFY(dialogWindow(QStringLiteral("RenameTabDialog")));
        QVERIFY(w.manager.closeTab(w.tab));
        QTRY_VERIFY(!dialogWindow(QStringLiteral("RenameTabDialog")));
    }

    // The panel size takes no size below the minimum; zero means the default
    // and shows as such.
    void panelSizeAcceptsOnlyUsableSizes()
    {
        Workspace w;
        QVERIFY(w.manager.moveTabToEdge(w.tab, Edge::Left));
        QVERIFY(w.show());
        CommandContext context = w.manager.context(w.window);
        context.tab = w.tab;
        QVERIFY(w.manager.commands().execute(QStringLiteral("tab.peek.setSize"), context));
        QQuickWindow* dialog = nullptr;
        QTRY_VERIFY((dialog = dialogWindow(QStringLiteral("PeekSizeDialog"))));
        QVERIFY(QTest::qWaitForWindowActive(dialog));
        auto* width = namedItem(dialog->contentItem(), QStringLiteral("PeekWidthInput"));
        auto* accept = namedItem(dialog->contentItem(), QStringLiteral("DialogAccept"));
        QVERIFY(width && accept);
        QTRY_VERIFY(width->hasActiveFocus());
        QCOMPARE(width->property("displayText").toString(), QStringLiteral("Default width"));
        for (const char letter : QByteArrayLiteral("90")) QTest::keyClick(dialog, letter);
        QTest::keyClick(dialog, Qt::Key_Tab);
        QTRY_VERIFY(!accept->isEnabled());
        QTest::keyClick(dialog, Qt::Key_Return);
        QVERIFY(dialogWindow(QStringLiteral("PeekSizeDialog")));
        QTest::mouseDClick(dialog, Qt::LeftButton, Qt::NoModifier, centre(width));
        QTest::keyClick(dialog, Qt::Key_A, Qt::ControlModifier);
        for (const char letter : QByteArrayLiteral("320")) QTest::keyClick(dialog, letter);
        QTest::keyClick(dialog, Qt::Key_Return);
        QTRY_VERIFY(!dialogWindow(QStringLiteral("PeekSizeDialog")));
        QCOMPARE(w.manager.peekSizePolicy(w.tab, Edge::Left).width, std::optional<int>(320));
        QCOMPARE(w.manager.peekSizePolicy(w.tab, Edge::Left).height, std::optional<int>());
    }

    // Several lines take the height left over; Enter starts a line and
    // Ctrl+Enter accepts. Raising works while it is open only.
    void severalLinesAndRaising()
    {
        Workspace w;
        QVERIFY(w.show());
        QString accepted;
        int finished = 0;
        DialogRequest request;
        request.name = QStringLiteral("LinesDialog");
        request.title = QStringLiteral("Lines");
        request.fields = {{QStringLiteral("source"), QStringLiteral("Source"), DialogField::Kind::Lines, QString()}};
        request.acceptText = QStringLiteral("Run");
        request.minimumSize = QSize(640, 360);
        request.accepted = [&](const QVariantMap& values) { accepted = values.value("source").toString(); };
        request.finished = [&] { ++finished; };
        const auto id = w.manager.openDialog(w.window, request);
        QVERIFY(id);
        QQuickWindow* dialog = nullptr;
        QTRY_VERIFY((dialog = dialogWindow(QStringLiteral("LinesDialog"))));
        QVERIFY(QTest::qWaitForWindowActive(dialog));
        QVERIFY(dialog->width() >= 640 && dialog->height() >= 360);
        auto* area = namedItem(dialog->contentItem(), QStringLiteral("source"));
        QVERIFY(area && area->height() > 200);
        QCOMPARE(namedItem(dialog->contentItem(), QStringLiteral("DialogAccept"))->property("text").toString(), QStringLiteral("Run"));
        QTRY_VERIFY(area->property("editor").value<QQuickItem*>()->hasActiveFocus());
        QTest::keyClick(dialog, 'a');
        QTest::keyClick(dialog, Qt::Key_Return);
        QTest::keyClick(dialog, 'b');
        QVERIFY(w.manager.raiseDialog(id));
        QTest::keyClick(dialog, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE(finished, 1);
        QCOMPARE(accepted, QStringLiteral("a\nb"));
        QVERIFY(!w.manager.raiseDialog(id));
    }

    // A notice without a cancel button closes by itself or with Esc, and
    // does not block its window.
    void aNoticeClosesByItself()
    {
        Workspace w;
        QVERIFY(w.show());
        int finished = 0;
        DialogRequest notice;
        notice.name = QStringLiteral("Notice");
        notice.title = QStringLiteral("Notice");
        notice.text = QStringLiteral("<b>literal</b>");
        notice.cancellable = false;
        notice.modal = false;
        notice.timeoutMs = 300;
        notice.finished = [&] { ++finished; };
        QVERIFY(w.manager.openDialog(w.window, notice));
        QQuickWindow* dialog = nullptr;
        QTRY_VERIFY((dialog = dialogWindow(QStringLiteral("Notice"))));
        QCOMPARE(dialog->modality(), Qt::NonModal);
        QVERIFY(!namedItem(dialog->contentItem(), QStringLiteral("DialogCancel"))->isVisible());
        QTRY_COMPARE(finished, 1);
        QVERIFY(!dialogWindow(QStringLiteral("Notice")));

        notice.timeoutMs = 0;
        QVERIFY(w.manager.openDialog(w.window, notice));
        QTRY_VERIFY((dialog = dialogWindow(QStringLiteral("Notice"))));
        QVERIFY(QTest::qWaitForWindowActive(dialog));
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_COMPARE(finished, 2);
        // No window, no dialog.
        QCOMPARE(w.manager.openDialog(WindowId::createUuid(), notice), quint64(0));
    }
};

QTEST_MAIN(QuickDialogsTest)
#include "quick_dialogs_test.moc"
