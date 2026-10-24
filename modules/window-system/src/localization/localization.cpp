// SPDX-License-Identifier: MIT
#include <window_system/localization.hpp>
#include "display_names.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QLibraryInfo>
#include <QLocale>
#include <QPointer>
#include <QTranslator>
#include <vector>

// Referencing the generated resource explicitly keeps catalogs linked when this
// module is consumed as a static library by an independent host.
static void initializeWindowSystemTranslations()
{
    Q_INIT_RESOURCE(window_system_translations);
}

namespace window_system {
namespace {
QPointer<TranslationManager> applicationTranslations;

bool supported(UiLanguage language)
{
    return language == UiLanguage::System || language == UiLanguage::SimplifiedChinese
        || language == UiLanguage::English;
}

UiLanguage resolved(UiLanguage language)
{
    if (language != UiLanguage::System) return language;
    // Traditional Chinese is not advertised as supported. It falls back to
    // English rather than silently presenting Simplified Chinese as native.
    for (const auto& name : QLocale::system().uiLanguages()) {
        const QLocale locale(name);
        if (locale.language() == QLocale::Chinese && locale.script() == QLocale::SimplifiedHanScript)
            return UiLanguage::SimplifiedChinese;
        if (locale.language() == QLocale::English) return UiLanguage::English;
    }
    return UiLanguage::English;
}
}

struct TranslationManager::Private {
    UiLanguage requested = UiLanguage::System;
    UiLanguage installed = UiLanguage::System;
    UiLanguage active = UiLanguage::English;
    QStringList prefixes{QStringLiteral(":/window_system/i18n/window_system")};
    std::vector<std::unique_ptr<QTranslator>> translators;
    bool installedOnce = false;
};

QString uiLanguageId(UiLanguage language)
{
    switch (language) {
    case UiLanguage::System: return QStringLiteral("system");
    case UiLanguage::SimplifiedChinese: return QStringLiteral("zh_CN");
    case UiLanguage::English: return QStringLiteral("en");
    }
    return QStringLiteral("system");
}

UiLanguage uiLanguageFromId(const QString& id)
{
    if (id == "zh_CN" || id == "zh-CN" || id == "zh-Hans") return UiLanguage::SimplifiedChinese;
    if (id == "en" || id.startsWith("en-") || id.startsWith("en_")) return UiLanguage::English;
    return UiLanguage::System;
}

QList<UiLanguage> supportedUiLanguages()
{
    return {UiLanguage::System, UiLanguage::SimplifiedChinese, UiLanguage::English};
}

QString uiLanguageName(UiLanguage language)
{
    switch (language) {
    case UiLanguage::System: return QCoreApplication::translate("WindowSystem", "Follow system");
    case UiLanguage::SimplifiedChinese: return QStringLiteral("简体中文");
    case UiLanguage::English: return QStringLiteral("English");
    }
    return {};
}

QString commandCategoryDisplayName(const QString& identity)
{
    static const QHash<QString, const char*> names{
        {QStringLiteral("file"), QT_TRANSLATE_NOOP("WindowSystem", "File")},
        {QStringLiteral("view"), QT_TRANSLATE_NOOP("WindowSystem", "View")},
        {QStringLiteral("layout"), QT_TRANSLATE_NOOP("WindowSystem", "Layout")},
        {QStringLiteral("window"), QT_TRANSLATE_NOOP("WindowSystem", "Window")},
        {QStringLiteral("tools"), QT_TRANSLATE_NOOP("WindowSystem", "Tools")},
        {QStringLiteral("help"), QT_TRANSLATE_NOOP("WindowSystem", "Help")},
        {QStringLiteral("commands"), QT_TRANSLATE_NOOP("WindowSystem", "Command")},
        {QStringLiteral("other"), QT_TRANSLATE_NOOP("WindowSystem", "Other")},
        {QStringLiteral("extensions"), QT_TRANSLATE_NOOP("WindowSystem", "Extensions")},
        {QStringLiteral("hierarchy"), QT_TRANSLATE_NOOP("WindowSystem", "Hierarchy conversions")},
        {QStringLiteral("tabAppearance"), QT_TRANSLATE_NOOP("WindowSystem", "Tab appearance")},
        {QStringLiteral("sortTabs"), QT_TRANSLATE_NOOP("WindowSystem", "Sort tabs")},
        {QStringLiteral("tabGroup"), QT_TRANSLATE_NOOP("WindowSystem", "Tab group")},
        {QStringLiteral("swapGroups"), QT_TRANSLATE_NOOP("WindowSystem", "Swap tab groups")},
        {QStringLiteral("mergeGroups"), QT_TRANSLATE_NOOP("WindowSystem", "Merge tab groups")},
        {QStringLiteral("moveGroups"), QT_TRANSLATE_NOOP("WindowSystem", "Move tab groups")},
        {QStringLiteral("tabBar"), QT_TRANSLATE_NOOP("WindowSystem", "Tab bar")},
        {QStringLiteral("toolbars"), QT_TRANSLATE_NOOP("WindowSystem", "Toolbar strips")},
        {QStringLiteral("newTab"), QT_TRANSLATE_NOOP("WindowSystem", "New tab")}};
    const auto found = names.constFind(identity);
    if (found != names.cend()) return QCoreApplication::translate("WindowSystem", *found);
    return identity;
}

TranslationManager::TranslationManager(QObject* parent) : QObject(parent), d(std::make_unique<Private>())
{
    initializeWindowSystemTranslations();
    // The usual application owns one controller. Independent test/host
    // controllers do not replace a still-live application controller.
    if (!applicationTranslations) applicationTranslations = this;
}

TranslationManager::~TranslationManager()
{
    if (auto* app = QCoreApplication::instance()) {
        for (const auto& translator : d->translators) app->removeTranslator(translator.get());
    }
}

TranslationManager* TranslationManager::applicationInstance() { return applicationTranslations; }
UiLanguage TranslationManager::requestedLanguage() const { return d->requested; }
UiLanguage TranslationManager::activeLanguage() const { return d->active; }
bool TranslationManager::restartRequired() const { return d->installedOnce && d->requested != d->installed; }

void TranslationManager::addCatalog(const QString& prefix)
{
    if (!d->installedOnce && !prefix.isEmpty() && !d->prefixes.contains(prefix)) d->prefixes.append(prefix);
}

bool TranslationManager::installLanguage(UiLanguage language)
{
    auto* app = QCoreApplication::instance();
    if (!app || !supported(language)) return false;
    const auto active = resolved(language);
    const auto suffix = QStringLiteral("_") + uiLanguageId(active) + QStringLiteral(".qm");
    std::vector<std::unique_ptr<QTranslator>> next;
    // Qt's standard controls/dialogs use their own official catalog.
    auto qt = std::make_unique<QTranslator>();
    if (qt->load(QStringLiteral(":/window_system/i18n/qt/qtbase_") + uiLanguageId(active) + QStringLiteral(".qm"))
        || qt->load(QStringLiteral("qtbase_") + uiLanguageId(active),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath))) next.push_back(std::move(qt));
    for (const auto& prefix : d->prefixes) {
        auto translator = std::make_unique<QTranslator>();
        if (!translator->load(prefix + suffix)) return false;
        next.push_back(std::move(translator));
    }
    for (const auto& translator : d->translators) app->removeTranslator(translator.get());
    for (const auto& translator : next) app->installTranslator(translator.get());
    d->translators = std::move(next);
    d->installed = language; d->active = active; d->installedOnce = true;
    setRequestedLanguage(language);
    return true;
}

void TranslationManager::setRequestedLanguage(UiLanguage language)
{
    if (!supported(language) || d->requested == language) return;
    d->requested = language;
    emit requestedLanguageChanged(language);
}

} // namespace window_system
