// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>
#include <window_system/interface_appearance.hpp>
#include <QByteArray>
#include <QJsonArray>
#include <QMap>
#include <QString>

namespace muxora {

QString resolveUserDirectory(const QString& explicitStateDirectory,
                             const QString& executableDirectory);

// User choices are separate from the window layout. The application owns this
// one writer; the reusable window module never reads a host configuration path.
class UserPreferences final {
public:
    explicit UserPreferences(QString directory);
    [[nodiscard]] QString language() const;
    [[nodiscard]] QString diagnostic() const;
    [[nodiscard]] QString directory() const { return directory_; }
    bool saveLanguage(const QString& language);
    [[nodiscard]] window_system::InterfaceAppearance interfaceAppearance() const { return interfaceAppearance_; }
    bool saveInterfaceAppearance(const window_system::InterfaceAppearance& value);
    bool loadKeybindings(window_system::CommandRegistry& commands);
    // Script reloads restore the prior defaults before replacing script-owned
    // bindings, then capture the new generation before reapplying user rules.
    bool restoreDefaultBindings(window_system::CommandRegistry& commands) const;
    void captureDefaultBindings(const window_system::CommandRegistry& commands);
    // Capture edits immediately while coalescing disk writes. Removed commands'
    // user rules survive, but their defaults and observed bindings are released.
    bool stageKeybindings(const window_system::CommandRegistry& commands);
    bool saveKeybindings(const window_system::CommandRegistry& commands);
    [[nodiscard]] qsizetype trackedDefaultCommandCount() const { return defaults_.size(); }
    [[nodiscard]] bool hasPendingKeybindingChanges() const { return userRules_ != persistedRules_; }

private:
    QString directory_;
    QString language_ = QStringLiteral("system");
    window_system::InterfaceAppearance interfaceAppearance_;
    QString settingsDiagnostic_;
    QString keybindingsDiagnostic_;
    QMap<QString, QVector<window_system::CommandBinding>> defaults_;
    QMap<QString, QVector<window_system::CommandBinding>> observedBindings_;
    QJsonArray userRules_;
    QJsonArray persistedRules_;
    bool defaultsCaptured_ = false;
    bool keybindingsWritable_ = false;
    bool keybindingsFileExisted_ = false;
    QByteArray keybindingsBytes_;
};

} // namespace muxora
