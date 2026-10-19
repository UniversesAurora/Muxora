#include <QCoreApplication>
#include "window_manager_p.hpp"
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "commands/command_dialogs.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "panes/tab_content.hpp"
#include "platform/window_placement.hpp"
#include "view_state_p.hpp"
#include "style/tab_appearance_p.hpp"
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
#include <QScopeGuard>
#include <QScreen>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <algorithm>
#include <limits>

namespace window_system {
namespace {

QJsonObject visibilityToJson(const BarVisibility& bars)
{
    return {{"toolbars", bars.toolbars}, {"statusBar", bars.statusBar}, {"tabBars", bars.tabBars}, {"sideBars", bars.sideBars}};
}
QJsonObject visibilityProfilesToJson(const QHash<WindowKind, BarVisibility>& profiles)
{
    QJsonObject object;
    for (const auto kind : {WindowKind::Workbench, WindowKind::Floating})
        if (profiles.contains(kind)) object.insert(kind == WindowKind::Workbench ? "workbench" : "floating", visibilityToJson(profiles.value(kind)));
    return object;
}
OperationResult parseVisibilityProfiles(const QJsonValue& value, QHash<WindowKind, BarVisibility>& result)
{
    if (value.isUndefined()) return {true, {}};
    if (!value.isObject()) return {false, QCoreApplication::translate("WindowSystem", "栏显示配置无效")};
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if ((it.key() != "workbench" && it.key() != "floating") || !it.value().isObject())
            return {false, QCoreApplication::translate("WindowSystem", "栏显示窗口类型无效")};
        const auto bars = it.value().toObject();
        if (bars.size() != 4) return {false, QCoreApplication::translate("WindowSystem", "栏显示字段不完整")};
        for (const auto key : {"toolbars", "statusBar", "tabBars", "sideBars"})
            if (!bars.value(key).isBool()) return {false, QCoreApplication::translate("WindowSystem", "栏显示字段类型无效")};
        result.insert(it.key() == "workbench" ? WindowKind::Workbench : WindowKind::Floating,
            {bars.value("toolbars").toBool(), bars.value("statusBar").toBool(), bars.value("tabBars").toBool(), bars.value("sideBars").toBool()});
    }
    return {true, {}};
}

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

OperationResult validateStripLayout(const QJsonValue& value)
{
    if (value.isUndefined()) return {true, {}};
    if (!value.isObject()) return {false, QCoreApplication::translate("WindowSystem", "工具条布局无效")};
    const auto object = value.toObject();
    if (object.isEmpty()) return {true, {}};
    if (object.contains("locked") && !object.value("locked").isBool())
        return {false, QCoreApplication::translate("WindowSystem", "工具条锁定设置无效")};
    QSet<QString> seen;
    for (const auto* field : {"rows", "bottomRows"}) {
        const auto rows = object.value(field);
        if (rows.isUndefined()) continue;
        if (!rows.isArray()) return {false, QCoreApplication::translate("WindowSystem", "工具条行布局无效")};
        for (const auto& row : rows.toArray()) {
            if (!row.isArray()) return {false, QCoreApplication::translate("WindowSystem", "工具条行布局无效")};
            for (const auto& entry : row.toArray()) {
                if (!entry.isString() || entry.toString().isEmpty() || seen.contains(entry.toString()))
                    return {false, QCoreApplication::translate("WindowSystem", "重复或非法工具条 ID")};
                seen.insert(entry.toString());
            }
        }
    }
    const auto settings = object.value("strips");
    if (settings.isUndefined()) return {true, {}};
    if (!settings.isObject()) return {false, QCoreApplication::translate("WindowSystem", "工具条设置无效")};
    const auto entries = settings.toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it.key().isEmpty() || !it->isObject()) return {false, QCoreApplication::translate("WindowSystem", "工具条设置无效")};
        const auto strip = it->toObject();
        for (const auto* flag : {"visible", "contentVisible", "naturalWidthPriority"})
            if (strip.contains(flag) && !strip.value(flag).isBool()) return {false, QCoreApplication::translate("WindowSystem", "工具条显示设置无效")};
        const auto width = strip.value("width");
        if (!width.isUndefined() && (!width.isDouble() || width.toDouble() != width.toInt(-2)
            || width.toInt(-2) < -1 || width.toInt(-2) > QWIDGETSIZE_MAX)) return {false, QCoreApplication::translate("WindowSystem", "工具条宽度无效")};
        const bool known = it.key() == "menu" || it.key() == "floating-menu" || it.key() == "workspace"
            || it.key() == "title" || it.key() == "caption-controls";
        const bool options = it.key() != "title" && it.key() != "caption-controls";
        if (known && !width.isUndefined() && width.toInt() != -1
            && width.toInt() < CommandStripHost::minimumSavedWidth(options, object.value("locked").toBool(true)))
            return {false, QCoreApplication::translate("WindowSystem", "工具条宽度小于可恢复的最小值")};
        const auto sizing = strip.value("sizing");
        if (!sizing.isUndefined() && sizing != "elastic" && sizing != "content") return {false, QCoreApplication::translate("WindowSystem", "工具条伸缩设置无效")};
        const auto excluded = strip.value("excluded");
        if (excluded.isUndefined()) continue;
        if (!excluded.isArray()) return {false, QCoreApplication::translate("WindowSystem", "工具条按钮设置无效")};
        QSet<QString> buttons;
        for (const auto& button : excluded.toArray()) {
            if (!button.isString() || button.toString().isEmpty() || buttons.contains(button.toString()))
                return {false, QCoreApplication::translate("WindowSystem", "工具条按钮 ID 无效")};
            buttons.insert(button.toString());
        }
    }
    return {true, {}};
}

QString toolbarKindKey(WindowKind kind)
{
    return kind == WindowKind::Workbench ? QStringLiteral("workbench") : QStringLiteral("floating");
}

OperationResult validateToolbarKinds(const QJsonValue& value)
{
    if (value.isUndefined()) return {true, {}};
    if (!value.isObject()) return {false, QCoreApplication::translate("WindowSystem", "窗口类型工具条布局无效")};
    const auto layouts = value.toObject();
    for (auto it = layouts.begin(); it != layouts.end(); ++it) {
        if (it.key() != "workbench" && it.key() != "floating")
            return {false, QCoreApplication::translate("WindowSystem", "工具条窗口类型无效")};
        const auto parsed = validateStripLayout(it.value());
        if (!parsed) return parsed;
        const auto layout = it->toObject();
        if (!layout.value("rows").isArray()) return {false, QCoreApplication::translate("WindowSystem", "工具条行布局无效")};
        const auto settings = layout.value("strips").toObject();
        QSet<QString> ids;
        for (const auto* field : {"rows", "bottomRows"})
            for (const auto& row : layout.value(field).toArray())
                for (const auto& entry : row.toArray()) ids.insert(entry.toString());
        for (auto setting = settings.begin(); setting != settings.end(); ++setting) ids.insert(setting.key());
        const bool mixed = it.key() == "workbench" ? ids.contains("floating-menu")
            : ids.contains("menu") || ids.contains("workspace");
        if (mixed) return {false, QCoreApplication::translate("WindowSystem", "不同窗口类型的工具条布局不能混用")};
    }
    return {true, {}};
}

bool layoutContainsStrip(const QJsonObject& layout, const QString& id)
{
    if (layout.value("strips").toObject().contains(id)) return true;
    for (const auto* field : {"rows", "bottomRows"})
        for (const auto& row : layout.value(field).toArray())
            if (row.toArray().contains(id)) return true;
    return false;
}

QJsonObject legacyToolbarLayout(QJsonObject layout, WindowKind kind)
{
    if (layout.isEmpty()) return layout;
    const QSet<QString> removed = kind == WindowKind::Workbench ? QSet<QString>{"floating-menu"}
        : QSet<QString>{"menu", "workspace"};
    auto settings = layout.value("strips").toObject();
    for (const auto& id : removed) settings.remove(id);
    layout.insert("strips", settings);
    for (const auto* field : {"rows", "bottomRows"}) {
        if (!layout.contains(field)) continue;
        auto rows = layout.value(field).toArray();
        for (qsizetype row = 0; row < rows.size(); ++row) {
            auto entries = rows[row].toArray();
            for (qsizetype index = entries.size(); index-- > 0;)
                if (removed.contains(entries[index].toString())) entries.removeAt(index);
            rows[row] = entries;
        }
        layout.insert(field, rows);
    }
    if (kind == WindowKind::Floating && !layoutContainsStrip(layout, "floating-menu")) {
        const char* target = "rows";
        qsizetype ordinal = 0;
        bool found = false;
        for (const auto* field : {"rows", "bottomRows"}) {
            const auto rows = layout.value(field).toArray();
            for (qsizetype row = 0; row < rows.size() && !found; ++row)
                if (rows[row].toArray().contains(QStringLiteral("title"))) {
                    target = field; ordinal = row; found = true;
                }
            if (found) break;
        }
        auto rows = layout.value(target).toArray();
        while (rows.size() <= ordinal) rows.append(QJsonArray{});
        auto entries = rows[ordinal].toArray(); entries.insert(0, QStringLiteral("floating-menu"));
        rows[ordinal] = entries; layout.insert(target, rows);
    }
    return layout;
}

} // namespace

QJsonObject WindowManager::saveLayout() const
{
    // Persist the last committed view during a drag, never its carrier,
    // insertion gaps or transient edge presentation coordinates.
    if (d->drag && d->drag->hasStableLayout()) return d->drag->stableLayout();
    auto layout = saveViewLayout();
    layout.insert("model", d->model.toJson());
    return layout;
}
QJsonObject WindowManager::saveViewLayout() const
{
    QJsonArray windows, panes;
    for (const auto id : d->model.windowOrder()) {
        const auto widget = d->windows.value(id); if (!widget) continue;
        const auto geometry = widget->normalPlacementGeometry();
        QJsonObject record{{"id", id.toString(QUuid::WithoutBraces)}, {"title", d->options.value(id).title},
            {"x", geometry.x()}, {"y", geometry.y()}, {"width", geometry.width()}, {"height", geometry.height()},
            {"maximized", widget->placementMaximized()},
            {"tabBarsVisible", d->options.value(id).tabBarsVisible},
            {"sideBarsVisible", d->options.value(id).sideBarsVisible},
            {"toolbarsVisible", d->options.value(id).toolbarsVisible},
            {"statusBarVisible", d->options.value(id).statusBarVisible},
            {"hiddenStatusItems", QJsonArray::fromStringList(d->options.value(id).hiddenStatusItems)}};
        record.insert("strips", widget->saveStripLayout());
        record.insert("barLayouts", visibilityProfilesToJson(d->windowVisibility.value(id)));
        const auto inactive = widget->saveInactiveStripLayouts();
        if (!inactive.isEmpty()) record.insert("stripLayouts", inactive);
        windows.append(record);
    }
    for (auto it = d->descriptors.cbegin(); it != d->descriptors.cend(); ++it)
        panes.append(QJsonObject{{"id", it.key().toString(QUuid::WithoutBraces)}, {"title", it->title},
            {"type", it->contentType}, {"state", QJsonObject::fromVariantMap(it->state)}, {"showIcon", it->showIcon},
            {"topBarSharing", it->topBarSharing == PaneBarSharing::Shared ? "shared" : "perPane"},
            {"bottomBarSharing", it->bottomBarSharing == PaneBarSharing::Shared ? "shared" : "perPane"}});
    QJsonObject profiles;
    for (const auto kind : {WindowKind::Workbench, WindowKind::Floating})
        if (d->toolbarProfiles.contains(kind)) profiles.insert(toolbarKindKey(kind), d->toolbarProfiles.value(kind));
    return {{"schemaVersion", 1}, {"windows", windows}, {"panes", panes},
        {"toolbarProfiles", profiles},
        {"visibilityProfiles", visibilityProfilesToJson(d->visibilityProfiles)},
        {"tabAppearance", tabAppearanceToJson(d->appearance)},
        {"behavior", QJsonObject{{"dragOutsideFloating", d->behavior.dragOutsideKind == WindowKind::Floating},
            {"closeEmptyWorkbench", d->behavior.closeEmptyWorkbench}, {"revealDuringDrag", d->behavior.revealDuringDrag},
            {"revealDelayMs", d->behavior.revealDelayMs}, {"newTabPosition", int(d->behavior.newTabPosition)},
            {"hideSingleTabWorkbench", d->behavior.hideSingleTabWorkbench}, {"hideSingleTabFloating", d->behavior.hideSingleTabFloating},
            {"hideToolbarOptionsWhenLocked", d->behavior.hideToolbarOptionsWhenLocked},
            {"zenFullscreenMode", int(d->behavior.zenFullscreenMode)}}}};
}
OperationResult WindowManager::restoreLayout(const QJsonObject& snapshot)
{
    const QPointer<WindowManager> self(this);
    if (snapshot.value("schemaVersion").toInt() != 1) return {false, QCoreApplication::translate("WindowSystem", "布局版本无效")};
    LayoutModel candidate;
    const auto parsed = candidate.fromJson(snapshot.value("model").toObject()); if (!parsed) return parsed;
    TabAppearance restoredAppearance;
    const auto parsedAppearance = parseTabAppearance(snapshot.value("tabAppearance"), restoredAppearance);
    if (!parsedAppearance) return parsedAppearance;
    QHash<WindowKind, BarVisibility> restoredVisibilityProfiles;
    QHash<WindowId, QHash<WindowKind, BarVisibility>> restoredWindowVisibility;
    const auto visibilityParsed = parseVisibilityProfiles(snapshot.value("visibilityProfiles"), restoredVisibilityProfiles);
    if (!visibilityParsed) return visibilityParsed;
    const auto parsedProfiles = validateToolbarKinds(snapshot.value("toolbarProfiles"));
    if (!parsedProfiles) return parsedProfiles;
    QHash<WindowKind, QJsonObject> restoredProfiles;
    const auto profiles = snapshot.value("toolbarProfiles").toObject();
    for (const auto kind : {WindowKind::Workbench, WindowKind::Floating})
        if (profiles.contains(toolbarKindKey(kind))) restoredProfiles.insert(kind, profiles.value(toolbarKindKey(kind)).toObject());
    QHash<WindowId, QJsonObject> restoredCurrentStrips;
    QHash<WindowId, QJsonObject> restoredInactiveStrips;
    WindowBehavior restoredBehavior;
    const auto settings = snapshot.value("behavior").toObject();
    for (const auto key : {"hideSingleTabWorkbench", "hideSingleTabFloating"})
        if (settings.contains(key) && !settings.value(key).isBool()) return {false, QCoreApplication::translate("WindowSystem", "单标签隐藏设置无效")};
    restoredBehavior.hideSingleTabWorkbench = settings.value("hideSingleTabWorkbench").toBool(false);
    restoredBehavior.hideSingleTabFloating = settings.value("hideSingleTabFloating").toBool(true);
    if (settings.contains("hideToolbarOptionsWhenLocked") && !settings.value("hideToolbarOptionsWhenLocked").isBool())
        return {false, QCoreApplication::translate("WindowSystem", "工具条选项显示设置无效")};
    restoredBehavior.hideToolbarOptionsWhenLocked = settings.value("hideToolbarOptionsWhenLocked").toBool(false);
    const auto position = settings.value("newTabPosition");
    if (!position.isUndefined() && (!position.isDouble() || position.toDouble() != position.toInt(-1)
        || position.toInt(-1) < int(NewTabPosition::Beginning) || position.toInt(-1) > int(NewTabPosition::End)))
        return {false, QCoreApplication::translate("WindowSystem", "新建标签位置无效")};
    restoredBehavior.newTabPosition = NewTabPosition(position.toInt(int(NewTabPosition::AfterActive)));
    const auto zenMode = settings.value("zenFullscreenMode");
    if (!zenMode.isUndefined() && (!zenMode.isDouble() || zenMode.toDouble() != zenMode.toInt(-1)
        || !validZenFullscreenMode(ViewMode(zenMode.toInt(-1)))))
        return {false, QCoreApplication::translate("WindowSystem", "禅模式全屏类型无效")};
    restoredBehavior.zenFullscreenMode = ViewMode(zenMode.toInt(int(ViewMode::WindowFullscreen)));
    restoredBehavior.dragOutsideKind = settings.value("dragOutsideFloating").toBool(true) ? WindowKind::Floating : WindowKind::Workbench;
    restoredBehavior.closeEmptyWorkbench = settings.value("closeEmptyWorkbench").toBool();
    restoredBehavior.revealDuringDrag = settings.value("revealDuringDrag").toBool(true);
    restoredBehavior.revealDelayMs = qBound(200, settings.value("revealDelayMs").toInt(500), 2000);
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject();
        const WindowId id(object.value("id").toString());
        if (!candidate.window(id)) continue;
        if (object.contains("tabBarsVisible") && !object.value("tabBarsVisible").isBool())
            return {false, QCoreApplication::translate("WindowSystem", "标签栏显示设置无效")};
        if (object.contains("sideBarsVisible") && !object.value("sideBarsVisible").isBool())
            return {false, QCoreApplication::translate("WindowSystem", "侧边栏显示设置无效")};
        if (object.contains("toolbarsVisible") && !object.value("toolbarsVisible").isBool())
            return {false, QCoreApplication::translate("WindowSystem", "工具栏显示设置无效")};
        if (object.contains("statusBarVisible") && !object.value("statusBarVisible").isBool())
            return {false, QCoreApplication::translate("WindowSystem", "状态栏显示设置无效")};
        const auto hiddenItems = object.value("hiddenStatusItems");
        QHash<WindowKind, BarVisibility> windowBars;
        const auto windowBarsParsed = parseVisibilityProfiles(object.value("barLayouts"), windowBars);
        if (!windowBarsParsed) return windowBarsParsed;
        restoredWindowVisibility.insert(id, windowBars);
        if (!hiddenItems.isUndefined()) {
            if (!hiddenItems.isArray() || hiddenItems.toArray().size() > 130)
                return {false, QCoreApplication::translate("WindowSystem", "状态控件显示设置无效")};
            QSet<QString> seen;
            for (const auto& item : hiddenItems.toArray()) {
                // Script/provider definitions can be absent during startup.
                // Keep bounded dormant preferences without rejecting the
                // entire workspace or manufacturing a live status control.
                if (!item.isString() || item.toString().isEmpty() || item.toString().size() > 256
                    || item.toString().trimmed() != item.toString() || seen.contains(item.toString()))
                    return {false, QCoreApplication::translate("WindowSystem", "状态控件 ID 无效")};
                seen.insert(item.toString());
            }
        }
        // Older previews persisted fullscreen/Zen targets. Validate their
        // complete contract before mutating content, but never restore this
        // temporary presentation as a user preference.
        const auto parsedState = validateSavedWindowViewState(object.value("viewState"), candidate, id);
        if (!parsedState) return parsedState;
        const auto parsedStrips = validateStripLayout(object.value("strips"));
        if (!parsedStrips) return parsedStrips;
        const auto parsedInactive = validateToolbarKinds(object.value("stripLayouts"));
        if (!parsedInactive) return parsedInactive;
        const auto kind = candidate.window(id)->kind;
        const auto savedStrips = object.value("strips").toObject();
        const bool legacy = !snapshot.contains("toolbarProfiles") && !object.contains("stripLayouts");
        if (!legacy && !savedStrips.isEmpty()) {
            const auto parsedCurrent = validateToolbarKinds(QJsonObject{{toolbarKindKey(kind), savedStrips}});
            if (!parsedCurrent) return parsedCurrent;
        }
        const auto current = legacy ? legacyToolbarLayout(savedStrips, kind) : savedStrips;
        auto inactive = object.value("stripLayouts").toObject();
        if (legacy) {
            const auto other = kind == WindowKind::Workbench ? WindowKind::Floating : WindowKind::Workbench;
            const bool hasOther = other == WindowKind::Workbench
                ? layoutContainsStrip(savedStrips, "menu") || layoutContainsStrip(savedStrips, "workspace")
                : layoutContainsStrip(savedStrips, "floating-menu");
            if (hasOther) inactive.insert(toolbarKindKey(other), legacyToolbarLayout(savedStrips, other));
        }
        restoredCurrentStrips.insert(id, current);
        restoredInactiveStrips.insert(id, inactive);
        // Old snapshots had no edit-order metadata. Pick one stable default
        // per kind once; current windows keep all their individual layouts.
        if (!snapshot.contains("toolbarProfiles") && !current.isEmpty() && !restoredProfiles.contains(kind))
            restoredProfiles.insert(kind, current);
    }
    QHash<PaneId, TabOptions> descriptors;
    for (const auto& entry : snapshot.value("panes").toArray()) {
        const auto object = entry.toObject(); const PaneId id(object.value("id").toString());
        if (id.isNull() || descriptors.contains(id)) return {false, QCoreApplication::translate("WindowSystem", "重复或非法内容 ID")};
        TabOptions options{object.value("title").toString(), object.value("type").toString(),
            object.value("state").toObject().toVariantMap(), object.value("showIcon").toBool(true)};
        for (const bool top : {true, false}) {
            const auto key = top ? "topBarSharing" : "bottomBarSharing";
            const auto value = object.value(key);
            if (!value.isUndefined() && (!value.isString() || (value.toString() != "shared" && value.toString() != "perPane")))
                return {false, QCoreApplication::translate("WindowSystem", "窗格栏共享设置无效")};
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
                return {false, QCoreApplication::translate("WindowSystem", "同一标签的窗格栏共享设置不一致")};
        }
    }
    QHash<PaneId, QPointer<QWidget>> created;
    auto cleanup = [&] { for (auto widget : created) delete widget; };
    for (const auto& tab : candidate.tabs()) for (const auto id : candidate.panes(tab.id)) {
        if (!descriptors.contains(id)) { cleanup(); return {false, QCoreApplication::translate("WindowSystem", "缺少内容恢复描述")}; }
        const QPointer<QWidget> content(createContent(descriptors.value(id), id));
        if (!self) {
            // Previously prepared content has not entered the manager's pane
            // map yet. The final factory return is also caller-owned unless
            // it is a widget the host already registered with another manager.
            if (content && !content->property("windowSystemManaged").toBool()) delete content.data();
            cleanup();
            return {false, QCoreApplication::translate("WindowSystem", "窗口管理器在准备恢复内容时被销毁")};
        }
        if (!content || content->property("windowSystemManaged").toBool()) { cleanup(); return {false, QCoreApplication::translate("WindowSystem", "内容恢复失败")}; }
        created.insert(id, content);
        content->setProperty("windowSystemManaged", true);
        if (!self || !content) { cleanup(); return {false, QCoreApplication::translate("WindowSystem", "恢复内容在准备时被销毁")}; }
        content->setProperty("paneId", id);
        if (!self || !content) { cleanup(); return {false, QCoreApplication::translate("WindowSystem", "恢复内容在准备时被销毁")}; }
        if (content->focusPolicy() == Qt::NoFocus) content->setFocusPolicy(Qt::StrongFocus);
        if (!self || !content) { cleanup(); return {false, QCoreApplication::translate("WindowSystem", "恢复内容在准备时被销毁")}; }
    }
    const bool previouslyBlocked = blockSignals(true);
    auto unblock = qScopeGuard([self, previouslyBlocked] { if (self) self->blockSignals(previouslyBlocked); });
    d->drag->releaseForMutation();
    for (auto window : d->windows) if (window) window->surface()->closePeek();
    for (auto pane : d->panes) if (pane) { pane->setParent(d->storage); pane->hide(); pane->deleteLater(); }
    for (auto* bars : {&d->paneTopBars, &d->paneBottomBars}) {
        for (auto bar : *bars) if (bar) { bar->setParent(d->storage); bar->hide(); bar->deleteLater(); }
        bars->clear();
    }
    for (auto tab : d->tabs) if (tab) { tab->setParent(d->storage); tab->deleteLater(); }
    d->tabs.clear(); d->panes = created; d->descriptors = descriptors; d->model = std::move(candidate);
    d->options.clear(); d->viewStates.clear(); d->behavior = restoredBehavior;
    d->appearance = std::move(restoredAppearance);
    d->toolbarProfiles = std::move(restoredProfiles);
    d->visibilityProfiles = std::move(restoredVisibilityProfiles);
    d->windowVisibility = std::move(restoredWindowVisibility); d->visibilityKinds.clear();
    const auto available = availableScreenGeometries();
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject(); const WindowId id(object.value("id").toString());
        if (!d->model.window(id)) continue;
        WindowOptions options;
        options.title = object.value("title").toString(options.title);
        options.size = normalizeWindowPlacement(savedWindowGeometry(object), available, {480, 320}).size();
        options.tabBarsVisible = object.value("tabBarsVisible").toBool(true);
        options.sideBarsVisible = object.value("sideBarsVisible").toBool(true);
        options.toolbarsVisible = object.value("toolbarsVisible").toBool(true);
        options.statusBarVisible = object.value("statusBarVisible").toBool(true);
        options.inheritVisibilityProfile = false;
        for (const auto& item : object.value("hiddenStatusItems").toArray()) options.hiddenStatusItems.append(item.toString());
        d->options.insert(id, options);
        const auto kind = d->model.window(id)->kind;
        const auto bars = preferredBarVisibility(id);
        d->windowVisibility[id].insert(kind, bars); d->visibilityKinds.insert(id, kind);
        if (!snapshot.contains("visibilityProfiles") && !d->visibilityProfiles.contains(kind)) d->visibilityProfiles.insert(kind, bars);
    }
    for (auto content : d->panes) if (content) content->setParent(d->storage);
    refresh();
    if (!self) return {false, QCoreApplication::translate("WindowSystem", "窗口管理器在恢复时被销毁")};
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject(); const WindowId id(object.value("id").toString());
        auto widget = d->windows.value(id); if (!widget) continue;
        widget->restoreStripLayouts(restoredInactiveStrips.value(id), restoredCurrentStrips.value(id));
        const auto minimum = widget->minimumSize().expandedTo(widget->minimumSizeHint());
        const auto desired = normalizeWindowPlacement(savedWindowGeometry(object), available, minimum);
        widget->restorePlacement(desired, object.value("maximized").toBool());
        if (!self) return {false, QCoreApplication::translate("WindowSystem", "窗口管理器在恢复时被销毁")};
    }
    // Normal/maximized placement and bar preferences survive restart;
    // fullscreen and Zen are confined to the current running window.
    applyWindowViewStates();
    if (!self) return {false, QCoreApplication::translate("WindowSystem", "窗口管理器在恢复时被销毁")};
    blockSignals(previouslyBlocked); unblock.dismiss();
    emit layoutChanged();
    return {true, {}};
}
} // namespace window_system
