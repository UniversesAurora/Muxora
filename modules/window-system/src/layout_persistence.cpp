#include "window_manager_p.hpp"
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "commands/command_dialogs.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "panes/tab_content.hpp"
#include "platform/window_placement.hpp"
#include <QApplication>
#include <QEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <algorithm>
#include <limits>

namespace window_system {
namespace {

QRect savedWindowGeometry(const QJsonObject& object)
{
    // Bound untrusted JSON before QRect arithmetic. Qt's widget size limit is
    // large enough for desktop layouts without the old 5000-pixel ceiling.
    return {qBound(-QWIDGETSIZE_MAX, object.value("x").toInt(), QWIDGETSIZE_MAX),
            qBound(-QWIDGETSIZE_MAX, object.value("y").toInt(), QWIDGETSIZE_MAX),
            qBound(480, object.value("width").toInt(1000), QWIDGETSIZE_MAX),
            qBound(320, object.value("height").toInt(700), QWIDGETSIZE_MAX)};
}

QList<QRect> availableScreenGeometries()
{
    QList<QRect> available;
    for (const auto* screen : qApp->screens()) available.append(screen->availableGeometry());
    return available;
}

} // namespace

QJsonObject WindowManager::saveLayout() const
{
    QJsonArray windows, panes;
    for (const auto id : d->model.windowOrder()) {
        const auto widget = d->windows.value(id); if (!widget) continue;
        const auto geometry = widget->normalPlacementGeometry();
        QJsonObject record{{"id", id.toString(QUuid::WithoutBraces)}, {"title", d->options.value(id).title},
            {"x", geometry.x()}, {"y", geometry.y()}, {"width", geometry.width()}, {"height", geometry.height()},
            {"maximized", widget->placementMaximized()},
            {"tabBarsVisible", d->options.value(id).tabBarsVisible}};
        record.insert("strips", widget->saveStripLayout());
        windows.append(record);
    }
    for (auto it = d->descriptors.cbegin(); it != d->descriptors.cend(); ++it)
        panes.append(QJsonObject{{"id", it.key().toString(QUuid::WithoutBraces)}, {"title", it->title},
            {"type", it->contentType}, {"state", QJsonObject::fromVariantMap(it->state)}, {"showIcon", it->showIcon},
            {"topBarSharing", it->topBarSharing == PaneBarSharing::Shared ? "shared" : "perPane"},
            {"bottomBarSharing", it->bottomBarSharing == PaneBarSharing::Shared ? "shared" : "perPane"}});
    return {{"schemaVersion", 1}, {"model", d->model.toJson()}, {"windows", windows}, {"panes", panes},
        {"behavior", QJsonObject{{"dragOutsideFloating", d->behavior.dragOutsideKind == WindowKind::Floating},
            {"closeEmptyWorkbench", d->behavior.closeEmptyWorkbench}, {"revealDuringDrag", d->behavior.revealDuringDrag},
            {"revealDelayMs", d->behavior.revealDelayMs}}}};
}
OperationResult WindowManager::restoreLayout(const QJsonObject& snapshot)
{
    if (snapshot.value("schemaVersion").toInt() != 1) return {false, QStringLiteral("布局版本无效")};
    LayoutModel candidate;
    const auto parsed = candidate.fromJson(snapshot.value("model").toObject()); if (!parsed) return parsed;
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject();
        if (!candidate.window(WindowId(object.value("id").toString()))) continue;
        if (object.contains("tabBarsVisible") && !object.value("tabBarsVisible").isBool())
            return {false, QStringLiteral("标签栏显示设置无效")};
    }
    QHash<PaneId, TabOptions> descriptors;
    for (const auto& entry : snapshot.value("panes").toArray()) {
        const auto object = entry.toObject(); const PaneId id(object.value("id").toString());
        if (id.isNull() || descriptors.contains(id)) return {false, QStringLiteral("重复或非法内容 ID")};
        TabOptions options{object.value("title").toString(), object.value("type").toString(),
            object.value("state").toObject().toVariantMap(), object.value("showIcon").toBool(true)};
        for (const bool top : {true, false}) {
            const auto key = top ? "topBarSharing" : "bottomBarSharing";
            const auto value = object.value(key);
            if (!value.isUndefined() && (!value.isString() || (value.toString() != "shared" && value.toString() != "perPane")))
                return {false, QStringLiteral("窗格栏共享设置无效")};
            (top ? options.topBarSharing : options.bottomBarSharing) = value.toString() == "perPane"
                ? PaneBarSharing::PerPane : PaneBarSharing::Shared;
        }
        descriptors.insert(id, options);
    }
    for (const auto& tab : candidate.tabs()) {
        const auto options = descriptors.value(tab.activePane);
        for (const auto pane : candidate.panes(tab.id)) {
            const auto other = descriptors.value(pane);
            if (other.topBarSharing != options.topBarSharing || other.bottomBarSharing != options.bottomBarSharing)
                return {false, QStringLiteral("同一标签的窗格栏共享设置不一致")};
        }
    }
    QHash<PaneId, QPointer<QWidget>> created;
    auto cleanup = [&] { for (auto widget : created) delete widget; };
    for (const auto& tab : candidate.tabs()) for (const auto id : candidate.panes(tab.id)) {
        if (!descriptors.contains(id)) { cleanup(); return {false, QStringLiteral("缺少内容恢复描述")}; }
        auto* content = createContent(descriptors.value(id), id);
        if (!content || content->property("windowSystemManaged").toBool()) { cleanup(); return {false, QStringLiteral("内容恢复失败")}; }
        content->setProperty("windowSystemManaged", true); content->setProperty("paneId", id); created.insert(id, content);
        if (content->focusPolicy() == Qt::NoFocus) content->setFocusPolicy(Qt::StrongFocus);
    }
    d->drag->releaseForMutation();
    for (auto window : d->windows) if (window) window->surface()->closePeek();
    for (auto pane : d->panes) if (pane) { pane->setParent(d->storage); pane->hide(); pane->deleteLater(); }
    for (auto* bars : {&d->paneTopBars, &d->paneBottomBars}) {
        for (auto bar : *bars) if (bar) { bar->setParent(d->storage); bar->hide(); bar->deleteLater(); }
        bars->clear();
    }
    for (auto tab : d->tabs) if (tab) { tab->setParent(d->storage); tab->deleteLater(); }
    d->tabs.clear(); d->panes = created; d->descriptors = descriptors; d->model = std::move(candidate);
    d->options.clear();
    const auto available = availableScreenGeometries();
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject(); const WindowId id(object.value("id").toString());
        if (!d->model.window(id)) continue;
        WindowOptions options;
        options.title = object.value("title").toString(options.title);
        options.size = normalizeWindowPlacement(savedWindowGeometry(object), available, {480, 320}).size();
        options.tabBarsVisible = object.value("tabBarsVisible").toBool(true);
        d->options.insert(id, options);
    }
    for (auto content : d->panes) if (content) content->setParent(d->storage);
    refresh();
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject(); const WindowId id(object.value("id").toString());
        auto widget = d->windows.value(id); if (!widget) continue;
        widget->restoreStripLayout(object.value("strips").toObject());
        const auto minimum = widget->minimumSize().expandedTo(widget->minimumSizeHint());
        const auto desired = normalizeWindowPlacement(savedWindowGeometry(object), available, minimum);
        widget->restorePlacement(desired, object.value("maximized").toBool());
    }
    const auto settings = snapshot.value("behavior").toObject();
    WindowBehavior behavior;
    behavior.dragOutsideKind = settings.value("dragOutsideFloating").toBool(true) ? WindowKind::Floating : WindowKind::Workbench;
    behavior.closeEmptyWorkbench = settings.value("closeEmptyWorkbench").toBool();
    behavior.revealDuringDrag = settings.value("revealDuringDrag").toBool(true);
    behavior.revealDelayMs = settings.value("revealDelayMs").toInt(500); setBehavior(behavior);
    return {true, {}};
}
} // namespace window_system
