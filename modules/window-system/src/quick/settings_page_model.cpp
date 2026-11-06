// SPDX-License-Identifier: MIT
#include "settings_page_model.hpp"
#include "qml_scene_component.hpp"
#include "shortcuts_page_model.hpp"
#include <window_system/interface_appearance.hpp>
#include <window_system/localization.hpp>
#include <window_system/window_manager.hpp>
#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QPointer>
#include <QQuickItem>
#include <algorithm>

namespace window_system::quick {
namespace {

bool matches(const QString& haystack, const QString& query)
{
    const QString folded = haystack.toCaseFolded();
    const auto terms = query.simplified().toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(terms.cbegin(), terms.cend(), [&folded](const QString& term) { return folded.contains(term); });
}

bool within(const QString& category, const QString& section)
{
    return category == section || category.startsWith(section + QLatin1Char('/'));
}

QVariantMap describe(const SettingDescription& setting, int index)
{
    QVariantList choices;
    for (const auto& choice : setting.choices)
        choices.append(QVariantMap{{"label", choice.label}, {"value", choice.value},
            {"isDefault", setting.defaultValue.isValid() && choice.value == setting.defaultValue}});
    static const char* kinds[] = {"toggle", "choice", "integer", "real", "text", "slider", "json"};
    return {{"type", "setting"}, {"setting", index}, {"id", setting.id}, {"category", setting.category},
        {"title", setting.title}, {"description", setting.description}, {"kind", kinds[int(setting.kind)]},
        {"checkLabel", setting.checkLabel}, {"choices", choices}, {"minimum", setting.minimum}, {"maximum", setting.maximum},
        {"step", setting.step}, {"decimals", setting.decimals}, {"suffix", setting.suffix},
        {"sliderMaximum", setting.sliderMaximum}, {"maximumLength", setting.maximumLength}, {"placeholder", setting.placeholder},
        {"applyLabel", setting.applyLabel}};
}

} // namespace

SettingsPageModel::SettingsPageModel(QVector<SettingCategory> categories, QVector<SettingDescription> settings,
                                     const Theme& theme, QObject* parent)
    : QObject(parent), categories_(std::move(categories)), settings_(std::move(settings)),
      style_(new SettingsStyle(theme, this))
{
    // Page order: each category's heading, then its own settings.
    for (const auto& category : std::as_const(categories_)) {
        entries_.append(QVariantMap{{"type", "heading"}, {"category", category.id}, {"title", category.title},
            {"depth", int(category.id.count(QLatin1Char('/')))}});
        for (int index = 0; index < settings_.size(); ++index) if (settings_.at(index).category == category.id)
            entries_.append(describe(settings_.at(index), index));
    }
    // A setting is found by its title, description, ID and the titles of
    // the categories it belongs to.
    for (const auto& setting : std::as_const(settings_)) {
        QString titles;
        for (const auto& category : std::as_const(categories_))
            if (within(setting.category, category.id)) titles += category.title + QLatin1Char(' ');
        searchable_.append(titles + setting.title + QLatin1Char(' ') + setting.id + QLatin1Char(' ') + setting.description);
    }
    if (!categories_.isEmpty()) current_ = categories_.first().id;
    refresh();
    search({});
}

QVariantList SettingsPageModel::categories() const
{
    QVariantList result;
    for (const auto& category : categories_) {
        bool shown = false;
        for (int index = 0; index < settings_.size() && !shown; ++index)
            shown = within(settings_.at(index).category, category.id) && shown_.value(index).toBool();
        result.append(QVariantMap{{"id", category.id}, {"title", category.title},
            {"depth", int(category.id.count(QLatin1Char('/')))}, {"shown", shown}});
    }
    return result;
}

QString SettingsPageModel::results() const
{
    return count_ ? QCoreApplication::translate("WindowSystem", "%n settings", nullptr, count_)
                  : QCoreApplication::translate("WindowSystem", "No matching settings");
}

void SettingsPageModel::setCurrent(const QString& category)
{
    if (current_ == category) return;
    current_ = category;
    emit currentChanged();
}

QString SettingsPageModel::pageTitle() const { return QCoreApplication::translate("WindowSystem", "Settings"); }
QString SettingsPageModel::pageDescription() const
{
    return QCoreApplication::translate("WindowSystem", "Changes take effect immediately. Window behavior is saved with the workspace layout.");
}
QString SettingsPageModel::searchPlaceholder() const
{
    return QCoreApplication::translate("WindowSystem", "Search setting names, descriptions, or IDs…");
}

void SettingsPageModel::search(const QString& query)
{
    query_ = query;
    shown_.clear();
    count_ = 0;
    for (const auto& text : std::as_const(searchable_)) {
        const bool shown = matches(text, query_);
        shown_.append(shown);
        count_ += shown ? 1 : 0;
    }
    emit filterChanged();
}

void SettingsPageModel::write(int setting, const QVariant& value)
{
    if (setting < 0 || setting >= settings_.size()) return;
    const auto& description = settings_.at(setting);
    if (description.enabled && !description.enabled()) return;
    if (value == values_.value(setting)) return;
    // The write may change other settings; their controls follow.
    if (description.write) description.write(value);
    refresh();
}

QVariantMap SettingsPageModel::apply(int setting, const QString& text)
{
    if (setting < 0 || setting >= settings_.size() || !settings_.at(setting).apply) return {};
    const auto& description = settings_.at(setting);
    if (description.enabled && !description.enabled()) return {};
    QString message;
    const bool applied = description.apply(text, message);
    refresh();
    return {{"applied", applied}, {"message", message}};
}

void SettingsPageModel::refresh()
{
    QVariantList values, editable;
    for (const auto& setting : std::as_const(settings_)) {
        values.append(setting.read ? setting.read() : QVariant());
        editable.append(!setting.enabled || setting.enabled());
    }
    if (values == values_ && editable == editable_) return;
    values_ = std::move(values);
    editable_ = std::move(editable);
    emit valuesChanged();
}

QQuickItem* createSettingsContent(WindowManager& manager, const TabOptions& options, PaneId pane)
{
    if (options.contentType == QStringLiteral("window_system.shortcuts")) return createShortcutsContent(manager);
    if (options.contentType != QStringLiteral("window_system.settings")) return nullptr;
    const auto currentWindow = [owner = QPointer<WindowManager>(&manager), pane]() -> WindowId {
        if (!owner) return {};
        const auto& model = owner->model();
        const auto* group = model.group(model.groupForTab(model.tabForPane(pane)));
        return group ? group->windowId : WindowId{};
    };
    auto* model = new SettingsPageModel(settingCategories(&manager), describeSettings(&manager, currentWindow), manager.interfaceTheme());
    auto* view = qobject_cast<QQuickItem*>(sceneComponent(SceneComponent::SettingsPage)
        .createWithInitialProperties({{"page", QVariant::fromValue(model)}}));
    Q_ASSERT(view);
    QQmlEngine::setObjectOwnership(view, QQmlEngine::CppOwnership);
    model->setParent(view);
    view->setObjectName(QStringLiteral("SettingsPage"));
    // Settings changed elsewhere, from a menu, a script or another page.
    QObject::connect(&manager, &WindowManager::layoutChanged, model, &SettingsPageModel::refresh);
    if (auto* translations = TranslationManager::applicationInstance())
        QObject::connect(translations, &TranslationManager::requestedLanguageChanged, model, &SettingsPageModel::refresh);
    if (auto* appearance = InterfaceAppearanceController::applicationInstance())
        QObject::connect(appearance, &InterfaceAppearanceController::appearanceChanged, model, [model, owner = QPointer<WindowManager>(&manager)] {
            if (owner) model->style()->setTheme(owner->interfaceTheme());
            model->refresh();
        });
    return view;
}

} // namespace window_system::quick
