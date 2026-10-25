// SPDX-License-Identifier: MIT
#include "platform/input_method_state.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QScopeGuard>
#include "docking/tab_bar.hpp"
#include <QTest>
#include <iterator>
#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>
#include <msctf.h>
#include <oleauto.h>
#include <ole2.h>
#endif

using namespace window_system;

#ifdef Q_OS_WIN
namespace {
class TsfTestSession {
public:
    TsfTestSession()
    {
        CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&manager_));
    }

    ~TsfTestSession() { close(); }

    bool open()
    {
        if (!manager_ || failed_) return false;
        const auto activateResult = manager_->Activate(&client_);
        const auto activated = SUCCEEDED(activateResult);
        const auto profilesResult = activated ? CoCreateInstance(
            CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&profiles_)) : E_FAIL;
        const auto compartmentsResult = activated
            ? manager_->QueryInterface(IID_PPV_ARGS(&compartments_)) : E_FAIL;
        failed_ = !activated
            || FAILED(profilesResult) || FAILED(compartmentsResult);
        return !failed_;
    }

    bool activeProfile(TF_INPUTPROCESSORPROFILE& value)
    {
        return !failed_
            && SUCCEEDED(profiles_->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &value))
            && (value.dwFlags & TF_IPP_FLAG_ACTIVE);
    }

    bool activate(const TF_INPUTPROCESSORPROFILE& value)
    {
        return !failed_ && SUCCEEDED(profiles_->ActivateProfile(
            value.dwProfileType, value.langid, value.clsid, value.guidProfile, value.hkl,
            TF_IPPMF_FORPROCESS));
    }

    QVector<TF_INPUTPROCESSORPROFILE> inputProcessors()
    {
        IEnumTfInputProcessorProfiles* profiles = nullptr;
        if (failed_ || FAILED(profiles_->EnumProfiles(0, &profiles)))
            return {};
        TF_INPUTPROCESSORPROFILE candidates[128]{};
        ULONG count = 0;
        const auto next = profiles->Next(ULONG(std::size(candidates)), candidates, &count);
        profiles->Release();
        if (FAILED(next)) return {};
        QVector<TF_INPUTPROCESSORPROFILE> result;
        for (ULONG index = 0; index < count; ++index) {
            const auto& candidate = candidates[index];
            if (candidate.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR)
                result.append(candidate);
        }
        return result;
    }

private:
    void close()
    {
        if (profiles_) profiles_->Release();
        profiles_ = nullptr;
        if (compartments_) compartments_->Release();
        compartments_ = nullptr;
        if (manager_ && client_ != TF_CLIENTID_NULL) manager_->Deactivate();
        if (manager_) manager_->Release();
        manager_ = nullptr;
    }

    ITfThreadMgr* manager_ = nullptr;
    ITfInputProcessorProfileMgr* profiles_ = nullptr;
    ITfCompartmentMgr* compartments_ = nullptr;
    TfClientId client_ = TF_CLIENTID_NULL;
    bool failed_ = false;
};
} // namespace
#endif

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
        for (const auto layout : layouts) {
            QVERIFY(ActivateKeyboardLayout(layout, 0));
            QCOMPARE(GetKeyboardLayout(0), layout);
            const auto saved = backend->capture(first);
            backend->restore(original, first);
            backend->restore(saved, first);
            QCOMPARE(GetKeyboardLayout(0), layout);
        }
#endif
    }
    void inputProcessorModeFollowsTabScope() {
#ifdef Q_OS_WIN
        WindowManager manager;
        const auto window = manager.createWindow();
        auto* first = new QLineEdit;
        const auto firstTab = manager.addTab(window, first, {"First", "test.input", {}});
        auto* second = new QLineEdit;
        const auto secondTab = manager.addTab(window, second, {"Second", "test.input", {}});
        manager.showWindow(window);
        QVERIFY(QTest::qWaitForWindowActive(manager.windowWidget(window)));
        manager.activateTab(firstTab);
        QTRY_COMPARE(QApplication::focusWidget(), first);

        TsfTestSession tsf;
        QVERIFY(tsf.open());
        TF_INPUTPROCESSORPROFILE originalProfile{};
        const bool hadOriginalProfile = tsf.activeProfile(originalProfile);
        const auto profileRestore = qScopeGuard([&] {
            if (hadOriginalProfile) tsf.activate(originalProfile);
        });
        const auto windowHandle = reinterpret_cast<HWND>(first->window()->effectiveWinId());
        const auto context = ImmGetContext(windowHandle);
        QVERIFY(context);
        const auto releaseContext = qScopeGuard([&] { ImmReleaseContext(windowHandle, context); });
        DWORD originalConversion = 0;
        DWORD originalSentence = 0;
        QVERIFY(ImmGetConversionStatus(context, &originalConversion, &originalSentence));
        const auto originalRestore = qScopeGuard([&] {
            ImmSetConversionStatus(context, originalConversion, originalSentence);
        });
        bool foundWritableProcessor = false;
        DWORD testedConversion = originalConversion;
        DWORD testedSentence = originalSentence;
        const auto candidates = tsf.inputProcessors();
        for (const auto& candidate : candidates) {
            if (!tsf.activate(candidate)) continue;
            QCoreApplication::processEvents();
            DWORD probeConversion = 0;
            DWORD probeSentence = 0;
            if (!ImmGetConversionStatus(context, &probeConversion, &probeSentence)) continue;
            if (ImmSetConversionStatus(
                    context, probeConversion & ~DWORD(IME_CMODE_NATIVE), probeSentence)
                && ImmSetConversionStatus(
                    context, probeConversion | DWORD(IME_CMODE_NATIVE), probeSentence)) {
                foundWritableProcessor = true;
                testedConversion = probeConversion;
                testedSentence = probeSentence;
                break;
            }
        }
        QVERIFY2(foundWritableProcessor,
            "No installed TSF input processor exposes writable conversion mode.");
        const auto alpha = testedConversion & ~DWORD(IME_CMODE_NATIVE);
        const auto native = testedConversion | DWORD(IME_CMODE_NATIVE);
        QVERIFY(ImmSetConversionStatus(context, alpha, testedSentence));

        auto* tabs = manager.windowWidget(window)->findChild<TabBar*>("TabBar");
        QVERIFY(tabs);
        const auto clickTab = [tabs](TabId id) {
            for (int index = 0; index < tabs->count(); ++index) {
                if (tabs->tabData(index).toUuid() == id) {
                    QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(index).center());
                    return true;
                }
            }
            return false;
        };
        QVERIFY(clickTab(secondTab));
        QTRY_VERIFY(second->isVisible());
        QTest::mouseClick(second, Qt::LeftButton);
        QTRY_COMPARE(QApplication::focusWidget(), second);
        QVERIFY(ImmSetConversionStatus(context, native, testedSentence));

        QVERIFY(clickTab(firstTab));
        QTRY_VERIFY(first->isVisible());
        QTest::mouseClick(first, Qt::LeftButton);
        QTRY_COMPARE(QApplication::focusWidget(), first);
        DWORD restoredConversion = 0;
        DWORD restoredSentence = 0;
        QTRY_VERIFY(ImmGetConversionStatus(context, &restoredConversion, &restoredSentence)
            && restoredConversion == alpha);

        QVERIFY(clickTab(secondTab));
        QTRY_VERIFY(second->isVisible());
        QTest::mouseClick(second, Qt::LeftButton);
        QTRY_COMPARE(QApplication::focusWidget(), second);
        QTRY_VERIFY(ImmGetConversionStatus(context, &restoredConversion, &restoredSentence)
            && restoredConversion == native);
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
