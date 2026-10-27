// SPDX-License-Identifier: MIT
// Render real tab widgets into bounded images without showing any windows.
// Timing is descriptive: this executable imposes no machine-dependent limit.
#include "docking/edge_tab_button.hpp"
#include "docking/tab_strip.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStyle>
#include <QSysInfo>

#include <cstdio>
#include <cstdlib>

using namespace window_system;

namespace {

constexpr int titleWidth = 160;
constexpr int canvasExtent = 256;
constexpr int canvasThickness = 32;

struct Options {
    QString commit;
    QString out;
    int iterations = 200;
    int warmup = 20;
};

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "title benchmark failed: %s\n", message);
    std::exit(2);
}

void require(bool condition, const char* message)
{
    if (!condition) fail(message);
}

Options parseOptions(const QApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Offscreen tab title painting benchmark"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("commit"), QStringLiteral("Source revision label"), QStringLiteral("revision")});
    parser.addOption({QStringLiteral("out"), QStringLiteral("Output JSON file (stdout if omitted)"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("iterations"), QStringLiteral("Measured warm paints per case"), QStringLiteral("count"), QStringLiteral("200")});
    parser.addOption({QStringLiteral("warmup"), QStringLiteral("Unmeasured paints after the first paint"), QStringLiteral("count"), QStringLiteral("20")});
    parser.process(app);
    Options options;
    options.commit = parser.value(QStringLiteral("commit"));
    options.out = parser.value(QStringLiteral("out"));
    require(!options.commit.isEmpty(), "--commit is required");
    bool valid = false;
    options.iterations = parser.value(QStringLiteral("iterations")).toInt(&valid);
    require(valid && options.iterations > 0, "--iterations must be a positive integer");
    options.warmup = parser.value(QStringLiteral("warmup")).toInt(&valid);
    require(valid && options.warmup >= 0, "--warmup must be a nonnegative integer");
    require(parser.positionalArguments().isEmpty(), "unexpected positional arguments");
    return options;
}

QString makeTitle(int characters)
{
    const auto pattern = QStringLiteral("Workspace 0123456789 / window title / ");
    return pattern.repeated((characters + pattern.size() - 1) / pattern.size()).first(characters);
}

double paintMilliseconds(QWidget& widget, QImage& image, int iterations)
{
    QElapsedTimer timer;
    timer.start();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        image.fill(Qt::transparent);
        widget.render(&image);
    }
    return double(timer.nsecsElapsed()) / 1.0e6;
}

QJsonObject measure(QWidget& widget, const QString& renderer, const QString& title,
                    const Options& options, int naturalTitleWidth, int labelExtent)
{
    widget.ensurePolished();
    require(!widget.isVisible(), "benchmark widgets must remain hidden");
    require(widget.width() <= canvasExtent && widget.height() <= canvasExtent,
            "title length must not allocate a large render target");
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    const double first = paintMilliseconds(widget, image, 1);
    paintMilliseconds(widget, image, options.warmup);
    const double warm = paintMilliseconds(widget, image, options.iterations);
    require(!widget.isVisible(), "painting must not show a benchmark window");
    const bool clipped = naturalTitleWidth > titleWidth;
    if (clipped)
        require(labelExtent < naturalTitleWidth && labelExtent < canvasExtent,
                "the long title must have a bounded label geometry");
    return {
        {QStringLiteral("renderer"), renderer},
        {QStringLiteral("utf16Characters"), int(title.size())},
        {QStringLiteral("naturalTitleWidth"), naturalTitleWidth},
        {QStringLiteral("maximumTitleWidth"), titleWidth},
        {QStringLiteral("titleClipped"), clipped},
        {QStringLiteral("labelExtent"), labelExtent},
        {QStringLiteral("canvasWidth"), image.width()},
        {QStringLiteral("canvasHeight"), image.height()},
        {QStringLiteral("iterations"), options.iterations},
        {QStringLiteral("warmupIterations"), options.warmup},
        {QStringLiteral("firstPaintMs"), first},
        {QStringLiteral("warmPaintTotalMs"), warm},
        {QStringLiteral("warmPaintMeanMs"), warm / options.iterations}};
}

QJsonObject measureTabBar(const QString& title, const Options& options)
{
    TabBar bar(defaultTheme(), nullptr);
    bar.resize(canvasExtent, canvasThickness);
    bar.setAvailableSize(bar.size());
    TabAppearance appearance;
    appearance.clipTitles = true;
    appearance.maximumTitleWidth = titleWidth;
    appearance.showIcons = false;
    appearance.showNumbers = false;
    appearance.showNewTabButton = false;
    bar.setAppearance(appearance, TabBarPlacement::Top);
    bar.addTab({}, title);
    bar.setCurrentIndex(0);
    bar.resize(canvasExtent, canvasThickness);
    const auto rect = bar.displayTabRect(0);
    require(!rect.isEmpty() && rect.width() <= titleWidth + 34,
            "the tab bar must apply the configured title limit");
    return measure(bar, QStringLiteral("tab-bar"), title, options,
                   bar.fontMetrics().size(Qt::TextShowMnemonic | Qt::TextSingleLine, title).width(),
                   rect.width());
}

QJsonObject measureSidebar(const QString& title, bool vertical, const Options& options)
{
    EdgeTabButton button(title, vertical, canvasThickness, nullptr);
    button.setTitleClipping(true, titleWidth);
    button.resize(vertical ? QSize(canvasThickness, canvasExtent) : QSize(canvasExtent, canvasThickness));
    const auto hint = button.sizeHint();
    const int extent = vertical ? hint.height() : hint.width();
    require(extent <= titleWidth + 24, "the sidebar must apply the configured title limit");
    return measure(button, vertical ? QStringLiteral("sidebar-vertical") : QStringLiteral("sidebar-horizontal"),
                   title, options, button.fontMetrics().horizontalAdvance(title), extent);
}

void writeReport(const Options& options, const QJsonObject& report)
{
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (options.out.isEmpty()) {
        require(std::fwrite(json.constData(), 1, size_t(json.size()), stdout) == size_t(json.size()),
                "write JSON to stdout");
        return;
    }
    QSaveFile file(options.out);
    require(file.open(QIODevice::WriteOnly), "open --out file");
    require(file.write(json) == json.size(), "write --out file");
    require(file.commit(), "commit --out file");
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("window-system-title-benchmark"));
    const auto options = parseOptions(app);
    QJsonArray results;
    for (const int characters : {12, 2000, 20000}) {
        const auto title = makeTitle(characters);
        results.append(measureTabBar(title, options));
        results.append(measureSidebar(title, false, options));
        results.append(measureSidebar(title, true, options));
    }
    writeReport(options, {
        {QStringLiteral("benchmark"), QStringLiteral("title-rendering")},
        {QStringLiteral("commit"), options.commit},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("fontFamily"), QApplication::font().family()},
        {QStringLiteral("fontPointSize"), QApplication::font().pointSizeF()},
        {QStringLiteral("style"), QApplication::style()->objectName()},
        {QStringLiteral("measurement"), QStringLiteral("Hidden QWidget::render into a fixed small image, including image clear; first paint excludes widget construction and geometry setup")},
        {QStringLiteral("results"), results}});
    return 0;
}
