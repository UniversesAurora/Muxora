// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QPointer>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    window_system::WindowManager windows;
    const auto root = windows.createWindow();
    auto* text = new QLabel("External installed-package contract");
    QPointer<QLabel> observer(text);
    const auto tab = windows.addTab(root, text, {"External content", "contract.text", {}});
    if (tab.isNull()) { delete text; return 1; }
    const auto floating = windows.detachTab(tab);
    if (floating.isNull() || !observer || !windows.promoteWindow(floating)) return 2;
    if (!windows.demoteWindow(floating, root)) return 3;
    if (!windows.model().validate()) return 4;
    if (!windows.requestCloseWindow(root)) return 5;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return observer.isNull() && windows.model().windows().isEmpty() ? 0 : 6;
}
