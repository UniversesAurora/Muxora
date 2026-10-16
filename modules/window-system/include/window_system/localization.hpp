// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QStringList>
#include <memory>

namespace window_system {

enum class UiLanguage { System, SimplifiedChinese, English };

[[nodiscard]] QString uiLanguageId(UiLanguage language);
[[nodiscard]] UiLanguage uiLanguageFromId(const QString& id);
[[nodiscard]] QList<UiLanguage> supportedUiLanguages();
[[nodiscard]] QString uiLanguageName(UiLanguage language);

// Installs standard Qt translators before any application widgets are created.
// Hosts own persistence and may add their own catalog prefix, e.g. :/host/i18n/host.
// A catalog consists of prefix + "_en.qm" and prefix + "_zh_CN.qm" resources/files.
class TranslationManager final : public QObject {
    Q_OBJECT
public:
    explicit TranslationManager(QObject* parent = nullptr);
    ~TranslationManager() override;

    [[nodiscard]] static TranslationManager* applicationInstance();
    [[nodiscard]] UiLanguage requestedLanguage() const;
    [[nodiscard]] UiLanguage activeLanguage() const;
    [[nodiscard]] bool restartRequired() const;

    // Register host catalogs before installLanguage(). Catalog paths are copied;
    // no host configuration or content service is retained by the module.
    void addCatalog(const QString& prefix);
    [[nodiscard]] bool installLanguage(UiLanguage language);

    // Preference only: existing content is not recreated or translated in place.
    // The host persists requestedLanguageChanged and installs it on next launch.
    void setRequestedLanguage(UiLanguage language);

signals:
    void requestedLanguageChanged(window_system::UiLanguage language);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace window_system

Q_DECLARE_METATYPE(window_system::UiLanguage)
