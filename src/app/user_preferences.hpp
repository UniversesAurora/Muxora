// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>
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
    bool loadKeybindings(window_system::CommandRegistry& commands);
    bool saveKeybindings(const window_system::CommandRegistry& commands);

private:
    QString directory_;
    QString language_ = QStringLiteral("system");
    QString settingsDiagnostic_;
    QString keybindingsDiagnostic_;
    QMap<QString, QVector<window_system::CommandBinding>> defaults_;
    QJsonArray unknownRules_;
    bool defaultsCaptured_ = false;
    bool keybindingsWritable_ = false;
    bool keybindingsFileExisted_ = false;
    QByteArray keybindingsBytes_;
};

} // namespace muxora
