// SPDX-License-Identifier: MIT
#include "platform/input_method_state.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QScopeGuard>
#include <QTest>
#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>
#endif

using namespace window_system;

class InputMethodNativeTest final : public QObject {
    Q_OBJECT
private slots:
    void installedLayoutsRoundTripAndTabsRestore() {
#ifdef Q_OS_WIN
        WindowManager manager;
        const auto window = manager.createWindow();
        auto* first = new QLineEdit;
        const auto tab = manager.addTab(window, first, {"First", "test.input", {}});
        auto* second = new QLineEdit;
        const auto other = manager.addTab(window, second, {"Second", "test.input", {}});
        manager.showWindow(window);
        QVERIFY(QTest::qWaitForWindowActive(manager.windowWidget(window)));
        manager.activateTab(tab);
        QTRY_COMPARE(QApplication::focusWidget(), first);
        auto backend = createInputMethodBackend();
        QVERIFY(!first->testAttribute(Qt::WA_NativeWindow));
        const auto original = backend->capture(first);
        QVERIFY(!first->testAttribute(Qt::WA_NativeWindow));
        const auto restore = qScopeGuard([&] { backend->restore(original, QApplication::focusWidget()); });
        const auto count = GetKeyboardLayoutList(0, nullptr);
        QVERIFY(count > 0);
        QVector<HKL> layouts(count);
        QCOMPARE(GetKeyboardLayoutList(count, layouts.data()), count);
        qInfo() << "Installed keyboard layouts tested:" << count;
        int modeQueries = 0;
        int modeChanges = 0;
        for (const auto layout : layouts) {
            QVERIFY(ActivateKeyboardLayout(layout, 0));
            QCOMPARE(GetKeyboardLayout(0), layout);
            const auto saved = backend->capture(first);
            const auto native = reinterpret_cast<HWND>(first->window()->effectiveWinId());
            if (const auto context = ImmGetContext(native)) {
                DWORD conversion = 0, sentence = 0;
                const auto open = ImmGetOpenStatus(context);
                if (ImmGetConversionStatus(context, &conversion, &sentence)) {
                    ++modeQueries;
                    // Do not assume an IME permits mode changes. When it does,
                    // restoring must preserve all bits, not just native/Latin.
                    if (ImmSetConversionStatus(context, conversion ^ IME_CMODE_FULLSHAPE, sentence))
                        ++modeChanges;
                    ImmReleaseContext(native, context);
                    backend->restore(saved, first);
                    const auto check = ImmGetContext(native);
                    QVERIFY(check);
                    DWORD restoredConversion = 0, restoredSentence = 0;
                    const auto read = ImmGetConversionStatus(check, &restoredConversion, &restoredSentence);
                    const auto restoredOpen = ImmGetOpenStatus(check);
                    ImmReleaseContext(native, check);
                    QVERIFY(read);
                    QCOMPARE(restoredConversion, conversion);
                    QCOMPARE(restoredSentence, sentence);
                    QCOMPARE(restoredOpen, open);
                } else {
                    ImmReleaseContext(native, context);
                }
            }
            backend->restore(original, first);
            backend->restore(saved, first);
            QCOMPARE(GetKeyboardLayout(0), layout);
        }
        qInfo() << "Readable IMM mode sets:" << modeQueries << "accepted mode changes:" << modeChanges;
        // Distinct installed layouts exercise the real manager focusChanged
        // connection; no layouts are installed or removed by this test.
        QVERIFY(ActivateKeyboardLayout(layouts.first(), 0));
        manager.activateTab(other);
        QTRY_COMPARE(QApplication::focusWidget(), second);
        QVERIFY(ActivateKeyboardLayout(layouts.last(), 0));
        manager.activateTab(tab);
        QTRY_COMPARE(QApplication::focusWidget(), first);
        QCOMPARE(GetKeyboardLayout(0), layouts.first());
        manager.activateTab(other);
        QTRY_COMPARE(QApplication::focusWidget(), second);
        QCOMPARE(GetKeyboardLayout(0), layouts.last());
#endif
    }
    void settingsComboUsesExistingKeyboardPath() {
        WindowManager manager;
        const auto window = manager.createWindow();
        manager.showSettings(window);
        manager.showWindow(window);
        auto* host = manager.windowWidget(window);
        QVERIFY(QTest::qWaitForWindowActive(host));
        auto* combo = host->findChild<QComboBox*>("InputMethodScope");
        QVERIFY(combo);
        combo->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), combo);
        QTest::keyClick(combo, Qt::Key_Home);
        QCOMPARE(manager.behavior().inputMethodScope, InputMethodScope::Application);
        QTest::keyClick(combo, Qt::Key_End);
        QCOMPARE(manager.behavior().inputMethodScope, InputMethodScope::Pane);
        QCOMPARE(QApplication::focusWidget(), combo);
        QTest::keyClick(combo, Qt::Key_Tab);
        QVERIFY(QApplication::focusWidget() != combo);
    }
};
QTEST_MAIN(InputMethodNativeTest)
#include "input_method_native_test.moc"
