// SPDX-License-Identifier: MIT
#include <window_system/interface_appearance.hpp>

#include <QApplication>
#include <QFontDatabase>
#include <QPointer>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace window_system {
namespace {
QPointer<InterfaceAppearanceController> instance;

// Native Qt controls use the same scale as custom-painted controls. Screen
// DPR remains Qt's responsibility; no private DPI API or bitmap scaling.
class ScaledStyle final : public QProxyStyle {
public:
    explicit ScaledStyle(QStyle* base) : QProxyStyle(base) {}
    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        const int value = QProxyStyle::pixelMetric(metric, option, widget);
        switch (metric) {
        // These values are enum/boolean policies, counts or physical native
        // frame metrics, not authored content dimensions.
        case PM_MaximumDragDistance: case PM_SpinBoxSliderHeight:
        case PM_MenuBarPanelWidth: case PM_DefaultFrameWidth: return value;
        default: return value <= 0 ? value : uiPixels(value);
        }
    }
};

QStringList defaults()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei"), QStringLiteral("sans-serif")};
#else
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).families();
#endif
}
}

QStringList interfaceFontFamilies(const QString& value)
{
    QStringList families;
    QString family; QChar quote;
    const auto append = [&] { const auto name = family.trimmed(); if (!name.isEmpty() && !families.contains(name, Qt::CaseInsensitive)) families.append(name); family.clear(); };
    for (const auto ch : value) {
        if (!quote.isNull()) { if (ch == quote) quote = {}; else family += ch; }
        else if (ch == '\'' || ch == '"') quote = ch;
        else if (ch == ',') append();
        else family += ch;
    }
    if (!quote.isNull()) return {};
    append();
    return families;
}

bool validInterfaceAppearance(const InterfaceAppearance& value)
{
    if (value.fontSize < InterfaceAppearance::minimumFontSize || value.fontSize > InterfaceAppearance::maximumFontSize || !std::isfinite(value.zoomLevel)
        || value.zoomLevel < InterfaceAppearance::minimumZoomLevel || value.zoomLevel > InterfaceAppearance::maximumZoomLevel
        || value.fontFamily.size() > InterfaceAppearance::maximumFamilyText) return false;
    const auto families = interfaceFontFamilies(value.fontFamily);
    return value.fontFamily.trimmed().isEmpty() || (!families.isEmpty() && families.size() <= 32);
}
double interfaceScaleForZoom(double level) { return std::pow(1.2, level); }

struct InterfaceAppearanceController::Private {
    InterfaceAppearance value;
    QFont originalFont;
    QString originalStyle;
};

InterfaceAppearanceController::InterfaceAppearanceController(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    Q_ASSERT(qApp && !instance);
    d->originalFont = qApp->font(); d->originalStyle = qApp->style()->objectName();
    instance = this;
    qApp->setStyle(new ScaledStyle(QStyleFactory::create(d->originalStyle)));
    qApp->setFont(font());
}
InterfaceAppearanceController::~InterfaceAppearanceController()
{
    instance.clear(); detail::interfaceScaleValue = 1;
    qApp->setFont(d->originalFont);
    if (auto* original = QStyleFactory::create(d->originalStyle)) qApp->setStyle(original);
}
InterfaceAppearanceController* InterfaceAppearanceController::applicationInstance() { return instance; }
InterfaceAppearance InterfaceAppearanceController::appearance() const { return d->value; }
QFont InterfaceAppearanceController::font() const
{
    auto font = d->originalFont;
    font.setFamilies(d->value.fontFamily.trimmed().isEmpty() ? defaults() : interfaceFontFamilies(d->value.fontFamily));
    font.setStyleHint(QFont::SansSerif);
    font.setPointSizeF(d->value.fontSize * interfaceScale() * 72.0 / 96.0);
    return font;
}
bool InterfaceAppearanceController::setAppearance(const InterfaceAppearance& value)
{
    if (!validInterfaceAppearance(value)) return false;
    if (value == d->value) return true;
    d->value = value; detail::interfaceScaleValue = interfaceScaleForZoom(value.zoomLevel);
    qApp->setFont(font());
    emit appearanceChanged();
    // Propagate style metric changes without replacing the style object (or
    // the widget tree). Content instances and keyboard focus stay in place.
    QList<QPointer<QWidget>> widgets;
    for (auto* widget : QApplication::allWidgets()) widgets.append(widget);
    for (const auto& widget : widgets) if (widget) {
        QEvent changed(QEvent::StyleChange); QCoreApplication::sendEvent(widget, &changed);
        if (widget) { widget->updateGeometry(); widget->update(); }
    }
    return true;
}
} // namespace window_system
