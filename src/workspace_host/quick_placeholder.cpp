#include "quick_placeholder.hpp"
#include "placeholder_content.hpp"
#include <window_system/interface_appearance.hpp>
#include <window_system/ui_scale.hpp>
#include <window_system/window_manager.hpp>

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <memory>

namespace muxora {
using namespace window_system;
namespace {

class PlaceholderActions final : public QObject {
    Q_OBJECT
public:
    PlaceholderActions(WindowManager& windows, QObject* parent) : QObject(parent), windows_(windows) {}
    Q_INVOKABLE void run(const QString& command) { windows_.execute(command); }

private:
    WindowManager& windows_;
};

QVariantMap texts(const QString& kind)
{
    if (kind == "notes") return {{"notes", notesText()}};
    if (kind == "sessions") return {{"heading", sessionsHeading()}, {"names", sessionNames()}};
    if (kind == "files") {
        QVariantList rows;
        for (const auto& row : fileRows()) rows.append(row);
        return {{"columns", fileColumns()}, {"rows", rows}};
    }
    if (kind == "transfers") return {{"text", transfersText()}};
    QVariantList actions;
    for (const auto& [label, command] : welcomeActions()) actions.append(QVariantMap{{"label", label}, {"command", command}});
    return {{"subtitle", welcomeSubtitle()}, {"description", welcomeDescription()}, {"tip", welcomeTip()}, {"actions", actions}};
}

// Colors and fonts follow the interface appearance, as the Widgets pages do.
QVariantMap style(const WindowManager& windows)
{
    const auto theme = windows.interfaceTheme();
    const auto* controller = InterfaceAppearanceController::applicationInstance();
    const double scale = interfaceScale();
    auto body = controller ? controller->font() : QGuiApplication::font();
    auto title = body; title.setPointSizeF(26 * scale); title.setBold(true);
    auto subtitle = body; subtitle.setPointSizeF(13 * scale);
    const QVariantMap colors{{"text", theme.text}, {"mutedText", theme.mutedText}, {"border", theme.border},
        {"raisedSurface", theme.raisedSurface}, {"buttonHover", theme.buttonHover},
        {"selection", theme.selection}, {"selectionText", theme.selectionText}};
    return {{"colors", colors}, {"bodyFont", body}, {"titleFont", title}, {"subtitleFont", subtitle},
        {"monoFont", notesFont()}, {"scale", scale}};
}

} // namespace

QuickWorkspace::ContentFactory quickPlaceholderFactory(WindowManager& windows, QQmlEngine& engine)
{
    auto component = std::make_shared<QQmlComponent>(&engine);
    component->loadFromModule("Muxora.WorkspaceHost", "PlaceholderPage", QQmlComponent::PreferSynchronous);
    if (component->isError()) qWarning().noquote() << component->errorString();
    auto* actions = new PlaceholderActions(windows, &engine);
    return [&windows, component, actions](const TabOptions& options, PaneId) -> QQuickItem* {
        auto properties = style(windows);
        properties.insert("kind", options.contentType);
        properties.insert("texts", texts(options.contentType));
        properties.insert("actions", QVariant::fromValue<QObject*>(actions));
        auto* object = component->createWithInitialProperties(properties);
        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item) { delete object; return nullptr; }
        if (auto* controller = InterfaceAppearanceController::applicationInstance())
            QObject::connect(controller, &InterfaceAppearanceController::appearanceChanged, item, [item, &windows] {
                const auto restyled = style(windows);
                for (auto it = restyled.cbegin(); it != restyled.cend(); ++it) item->setProperty(qPrintable(it.key()), it.value());
            });
        return item;
    };
}

} // namespace muxora

#include "quick_placeholder.moc"
