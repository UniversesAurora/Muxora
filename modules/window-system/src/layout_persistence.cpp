#include "window_manager_p.hpp"
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "commands/command_dialogs.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "panes/tab_content.hpp"
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
QJsonObject WindowManager::saveLayout() const
{
    QJsonArray windows, panes;
    for (const auto id : d->model.windowOrder()) {
        const auto widget = d->windows.value(id); if (!widget) continue;
        const auto geometry = widget->isMaximized() ? widget->normalGeometry() : widget->geometry();
        QJsonObject record{{"id", id.toString(QUuid::WithoutBraces)}, {"title", d->options.value(id).title},
            {"x", geometry.x()}, {"y", geometry.y()}, {"width", geometry.width()}, {"height", geometry.height()},
            {"maximized", widget->isMaximized()}};
        record.insert("strips", widget->saveStripLayout());
        windows.append(record);
    }
    for (auto it = d->descriptors.cbegin(); it != d->descriptors.cend(); ++it)
        panes.append(QJsonObject{{"id", it.key().toString(QUuid::WithoutBraces)}, {"title", it->title},
            {"type", it->contentType}, {"state", QJsonObject::fromVariantMap(it->state)}, {"showIcon", it->showIcon}});
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
    QHash<PaneId, TabOptions> descriptors;
    for (const auto& entry : snapshot.value("panes").toArray()) {
        const auto object = entry.toObject(); const PaneId id(object.value("id").toString());
        if (id.isNull() || descriptors.contains(id)) return {false, QStringLiteral("重复或非法内容 ID")};
        descriptors.insert(id, {object.value("title").toString(), object.value("type").toString(), object.value("state").toObject().toVariantMap(), object.value("showIcon").toBool(true)});
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
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject(); const WindowId id(object.value("id").toString());
        if (!d->model.window(id)) continue;
        WindowOptions options;
        options.title = object.value("title").toString(options.title);
        options.size = QSize(qBound(480, object.value("width").toInt(1000), 5000),
                             qBound(320, object.value("height").toInt(700), 4000));
        d->options.insert(id, options);
    }
    for (auto content : d->panes) if (content) content->setParent(d->storage);
    refresh();
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject(); const WindowId id(object.value("id").toString());
        auto widget = d->windows.value(id); if (!widget) continue;
        const QSize size(qBound(480, object.value("width").toInt(1000), 5000), qBound(320, object.value("height").toInt(700), 4000));
        QRect desired(object.value("x").toInt(), object.value("y").toInt(), size.width(), size.height());
        bool visible = false;
        for (auto* screen : qApp->screens()) if (screen->availableGeometry().intersects(desired)) visible = true;
        if (!visible && qApp->primaryScreen()) desired.moveCenter(qApp->primaryScreen()->availableGeometry().center());
        widget->setGeometry(desired);
        widget->restoreStripLayout(object.value("strips").toObject());
        object.value("maximized").toBool() ? widget->showMaximized() : widget->show();
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
