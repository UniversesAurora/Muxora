// SPDX-License-Identifier: MIT
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

class Overlay final : public QWidget {
public:
    explicit Overlay(bool opaque)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
              | Qt::WindowDoesNotAcceptFocus
              | (opaque ? Qt::WindowFlags{} : Qt::WindowTransparentForInput)), opaque_(opaque)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground, !opaque);
        setWindowTitle(QStringLiteral("WindowSystem test overlay"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), opaque_ ? QColor(70, 45, 100) : QColor(130, 90, 180, 40));
    }

private:
    bool opaque_;
};

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {QStringLiteral("x"), "Qt logical left coordinate", "x"},
        {QStringLiteral("y"), "Qt logical top coordinate", "y"},
        {QStringLiteral("width"), "Qt logical width", "width"},
        {QStringLiteral("height"), "Qt logical height", "height"},
        {QStringLiteral("ready"), "Path to the fixture readiness report", "path"},
        {QStringLiteral("opaque"), "Block input instead of providing a click-through layer"}
    });
    parser.process(application);
    bool xValid = false, yValid = false, widthValid = false, heightValid = false;
    const int x = parser.value("x").toInt(&xValid);
    const int y = parser.value("y").toInt(&yValid);
    const int width = parser.value("width").toInt(&widthValid);
    const int height = parser.value("height").toInt(&heightValid);
    if (!xValid || !yValid || !widthValid || !heightValid || width < 1 || height < 1
        || width > 10000 || height > 10000 || parser.value("ready").isEmpty()) return 2;

    Overlay overlay(parser.isSet("opaque"));
    overlay.setGeometry(x, y, width, height);
    overlay.show();
    QElapsedTimer readinessDeadline;
    readinessDeadline.start();
    QTimer readiness;
    readiness.setInterval(10);
    QObject::connect(&readiness, &QTimer::timeout, &application, [&] {
        if (readinessDeadline.elapsed() > 3000) {
            application.exit(3);
            return;
        }
        if (!overlay.windowHandle() || !overlay.windowHandle()->isExposed()) return;
        QJsonObject report{{"pid", qint64(QCoreApplication::applicationPid())},
                           {"opaque", parser.isSet("opaque")}};
#ifdef Q_OS_WIN
        const auto handle = reinterpret_cast<HWND>(overlay.winId());
        const auto style = GetWindowLongPtr(handle, GWL_EXSTYLE);
        report.insert("layered", bool(style & WS_EX_LAYERED));
        report.insert("transparentForInput", bool(style & WS_EX_TRANSPARENT));
        report.insert("topmost", bool(style & WS_EX_TOPMOST));
#endif
        QSaveFile ready(parser.value("ready"));
        const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Compact);
        if (!ready.open(QIODevice::WriteOnly) || ready.write(bytes) != bytes.size() || !ready.commit()) {
            application.exit(4);
            return;
        }
        readiness.stop();
    });
    readiness.start();
    // A failed parent assertion cannot leave a fixture window on the desktop.
    QTimer::singleShot(8000, &application, &QCoreApplication::quit);
    return application.exec();
}
