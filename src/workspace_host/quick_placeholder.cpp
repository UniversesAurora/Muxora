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
        return {{"columns", fileColumns()}, {"rows", rows}, {"address", fileAddressText()},
            {"information", fileInformationText(rows.size())}};
    }
    if (kind == "transfers") return {{"text", transfersText()}};
    QVariantList actions;
    for (const auto& [label, command] : welcomeActions()) actions.append(QVariantMap{{"label", label}, {"command", command}});
    return {{"subtitle", welcomeSubtitle()}, {"description", welcomeDescription()}, {"tip", welcomeTip()}, {"actions", actions}};
}

// Colors and fonts follow the interface appearance.
QVariantMap style(const WindowManager& windows)
{
    const auto theme = windows.interfaceTheme();
    const auto* controller = InterfaceAppearanceController::applicationInstance();
    const double scale = interfaceScale();
    auto body = controller ? controller->font() : QGuiApplication::font();
    auto title = body; title.setPointSizeF(26 * scale); title.setBold(true);
    auto subtitle = body; subtitle.setPointSizeF(13 * scale);
    const QVariantMap colors{{"text", theme.text}, {"mutedText", theme.mutedText}, {"border", theme.border}, {"surface", theme.surface},
        {"raisedSurface", theme.raisedSurface}, {"buttonHover", theme.buttonHover},
        {"selection", theme.selection}, {"selectionText", theme.selectionText}};
    return {{"colors", colors}, {"bodyFont", body}, {"titleFont", title}, {"subtitleFont", subtitle},
        {"monoFont", notesFont()}, {"scale", scale}};
}

std::shared_ptr<QQmlComponent> loadComponent(QQmlEngine& engine, const char* name)
{
    auto component = std::make_shared<QQmlComponent>(&engine);
    component->loadFromModule("Muxora.WorkspaceHost", name, QQmlComponent::PreferSynchronous);
    if (component->isError()) qWarning().noquote() << component->errorString();
    return component;
}

// An item from the component with the current style and the given
// properties; it follows later appearance changes.
QQuickItem* createStyled(QQmlComponent& component, WindowManager& windows, QVariantMap properties)
{
    auto styled = style(windows);
    for (auto it = properties.cbegin(); it != properties.cend(); ++it) styled.insert(it.key(), it.value());
    auto* object = component.createWithInitialProperties(styled);
    auto* item = qobject_cast<QQuickItem*>(object);
    if (!item) { delete object; return nullptr; }
    if (auto* controller = InterfaceAppearanceController::applicationInstance())
        QObject::connect(controller, &InterfaceAppearanceController::appearanceChanged, item, [item, &windows] {
            const auto restyled = style(windows);
            for (auto it = restyled.cbegin(); it != restyled.cend(); ++it) item->setProperty(qPrintable(it.key()), it.value());
        });
    return item;
}

} // namespace

QuickWorkspace::ContentFactory quickPlaceholderFactory(WindowManager& windows, QQmlEngine& engine, QuickWorkspace* workspace)
{
    auto page = loadComponent(engine, "PlaceholderPage");
    auto address = loadComponent(engine, "FileAddressBar");
    auto information = loadComponent(engine, "FileInformationBar");
    auto* actions = new PlaceholderActions(windows, &engine);
    return [&windows, workspace, page, address, information, actions](const TabOptions& options, PaneId pane) -> QQuickItem* {
        const auto pageTexts = texts(options.contentType);
        auto* item = createStyled(*page, windows, {{"kind", options.contentType}, {"texts", pageTexts},
            {"actions", QVariant::fromValue<QObject*>(actions)}});
        if (!item || !workspace || options.contentType != QStringLiteral("files")) return item;
        // Set while the content is created, they show with the page.
        const auto attach = [&](QQmlComponent& component, const QString& name, const QString& text, bool top) {
            auto* bar = createStyled(component, windows, {{"text", text}});
            if (!bar) return;
            bar->setObjectName(name);
            if (!workspace->setPaneBar(pane, bar, top)) delete bar;
        };
        attach(*address, QStringLiteral("FileAddressBar"), pageTexts.value("address").toString(), true);
        attach(*information, QStringLiteral("FileInformationBar"), pageTexts.value("information").toString(), false);
        return item;
    };
}

} // namespace muxora

#include "quick_placeholder.moc"
