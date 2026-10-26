// SPDX-License-Identifier: MIT
// Surface callbacks must not outlive a window's derived member storage.
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QPointer>
#include <QTest>
#include <QWidget>
#include <memory>
using namespace window_system;

class SurfaceLifecycleTest final : public QObject {
    Q_OBJECT
private slots:
    void destroyingAWindowReleasesItsSurfaceBeforeItsGroups()
    {
        auto manager = std::make_unique<WindowManager>();
        const auto window = manager->createWindow();
        manager->addTab(window, new QWidget, {"Workspace"});
        QPointer<QWidget> host = manager->windowWidget(window);
        QVERIFY(host && !host->isVisible());
        manager.reset();
        QVERIFY(!host);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

};
QTEST_MAIN(SurfaceLifecycleTest)
#include "surface_lifecycle_test.moc"
