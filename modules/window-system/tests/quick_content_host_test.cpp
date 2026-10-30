// SPDX-License-Identifier: MIT
// Phase 10.3: content ownership, window-bound resources and input identity.
// Dimensions: same/cross-window mounting, visible/hidden, accepted/rejected
// ownership, and switching tab-scoped IME state after a window move.
#include "quick/content_host.hpp"
#include "input_method_scopes.hpp"
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

namespace {
class WindowInputBackend final : public InputMethodBackend {
public:
    int state = 7;
    QPointer<QWindow> captured, restored;
    InputMethodState capture(QWindow* window) override { captured = window; return state; }
    void restore(const InputMethodState& value, QWindow* window) override {
        state = std::any_cast<int>(value); restored = window;
    }
};
}

class QuickContentHostTest final : public QObject {
    Q_OBJECT
private slots:
    void movingContent_preservesIdentityAndReportsHiddenBeforeWindowChange()
    {
        QQuickWindow first;
        QQuickWindow second;
        first.resize(320, 240);
        second.resize(320, 240);
        first.show();
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        ContentHost host;
        const auto pane = QUuid::createUuid();
        QQuickItem* content = nullptr;
        QQuickItem* editor = nullptr;
        int creations = 0;
        host.setFactory([&](const TabOptions&, PaneId) {
            ++creations;
            content = new QQuickItem;
            content->setFlag(QQuickItem::ItemIsFocusScope);
            editor = new QQuickItem(content);
            editor->setFocusPolicy(Qt::StrongFocus);
            return content;
        });
        QVERIFY(host.create(pane, {QStringLiteral("Editor")}));
        QVector<ContentVisibilityChange> visibility;
        QStringList crossing;
        bool observeCrossing = false;
        host.setVisibilityHandler([&](const QVector<ContentVisibilityChange>& changes) {
            for (const auto& change : changes) {
                visibility.append(change);
                if (observeCrossing && !change.visible) {
                    crossing.append(QStringLiteral("hidden"));
                    QCOMPARE(content->window(), &first);
                }
            }
        });
        QTRY_COMPARE(visibility.size(), 1);
        QVERIFY(!visibility.last().visible);
        QVERIFY(host.mount(pane, first.contentItem()));
        QTRY_VERIFY(visibility.last().visible);
        first.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&first));
        editor->forceActiveFocus();
        QTRY_VERIFY(editor->hasActiveFocus());
        QCOMPARE(host.focusItem(pane), editor);
        QCOMPARE(host.paneForItem(editor), pane);

        auto* sameWindowTarget = new QQuickItem(first.contentItem());
        QVERIFY(host.mount(pane, sameWindowTarget));
        QCOMPARE(content->parent(), &host);
        QCOMPARE(host.focusItem(pane), editor);
        QVERIFY(host.focus(pane));
        connect(content, &QQuickItem::windowChanged, &host, [&](QQuickWindow*) {
            if (observeCrossing) crossing.append(QStringLiteral("window"));
        });
        observeCrossing = true;
        QVERIFY(host.mount(pane, second.contentItem()));
        observeCrossing = false;
        QVERIFY(crossing.size() >= 2);
        QCOMPARE(crossing.first(), QStringLiteral("hidden"));
        QCOMPARE(content->window(), &second);
        QCOMPARE(content->parent(), &host);
        QCOMPARE(host.item(pane), content);
        QCOMPARE(host.focusItem(pane), editor);
        QCOMPARE(creations, 1);
        QVERIFY(host.focus(pane));
        second.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&second));
        QTRY_VERIFY(editor->hasActiveFocus());
        QTRY_VERIFY(visibility.last().visible);
        const auto beforeHide = visibility.size();
        host.unmount(pane);
        host.unmount(pane);
        QCoreApplication::processEvents();
        QCOMPARE(visibility.size(), beforeHide + 1);
        QCOMPARE(visibility.last().pane, pane);
        QVERIFY(!visibility.last().visible);
    }

    void ownership_rejectedInputsSurviveAndAcceptedItemsFollowHostLifetime()
    {
        auto owner = std::make_unique<ContentHost>();
        const auto pane = QUuid::createUuid();
        QPointer<QQuickItem> accepted = new QQuickItem;
        QVERIFY(owner->adopt(pane, accepted));
        ContentHost other;
        other.setFactory([&](const TabOptions&, PaneId) { return accepted.data(); });
        QVERIFY(!other.create(QUuid::createUuid(), {}));
        QVERIFY(accepted);
        QCOMPARE(accepted->parent(), owner.get());
        auto rejected = std::make_unique<QQuickItem>();
        QVERIFY(!owner->adopt(pane, rejected.get()));
        QVERIFY(!rejected->parent());
        owner.reset();
        QVERIFY(accepted.isNull());

        QPointer<QQuickItem> abandoned;
        auto* temporary = new ContentHost;
        temporary->setFactory([&](const TabOptions&, PaneId) {
            delete temporary;
            abandoned = new QQuickItem;
            return abandoned.data();
        });
        QVERIFY(!temporary->create(QUuid::createUuid(), {}));
        QVERIFY(abandoned.isNull());
    }


    void imeState_windowMoveKeepsTabIdentityAndUsesDestinationNativeTarget()
    {
        LayoutModel model;
        const auto firstWindow = model.createWindow();
        const auto firstTab = model.addTab(firstWindow, QStringLiteral("First"));
        const auto secondTab = model.addTab(firstWindow, QStringLiteral("Second"));
        const auto firstPane = model.tab(firstTab)->activePane;
        const auto secondPane = model.tab(secondTab)->activePane;
        const auto destination = model.createWindow();
        QWindow first;
        QWindow second;
        auto backend = std::make_unique<WindowInputBackend>();
        auto* observed = backend.get();
        InputMethodScopes scopes(std::move(backend));
        scopes.synchronize(model);
        scopes.focus(model, firstPane, &first);
        observed->state = 17;
        QVERIFY(model.moveTabToWindow(firstTab, destination));
        scopes.synchronize(model);
        scopes.focus(model, firstPane, &second);
        QCOMPARE(observed->state, 77);
        QCOMPARE(observed->captured.data(), &first);
        QCOMPARE(observed->restored.data(), &second);
        scopes.focus(model, secondPane, &first);
        QCOMPARE(observed->state, 7);
        observed->state = 42;
        scopes.focus(model, firstPane, &second);
        QCOMPARE(observed->state, 77);
        QCOMPARE(observed->captured.data(), &first);
        QCOMPARE(observed->restored.data(), &second);
        scopes.focus(model, secondPane, &first);
        QCOMPARE(observed->state, 42);
        QCOMPARE(observed->captured.data(), &second);
        QCOMPARE(observed->restored.data(), &first);
    }
};

QTEST_MAIN(QuickContentHostTest)
#include "quick_content_host_test.moc"
