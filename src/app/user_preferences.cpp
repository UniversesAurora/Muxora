// SPDX-License-Identifier: MIT
#include "user_preferences.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace muxora {
namespace {
constexpr qint64 maximumFileBytes = 1024 * 1024;
constexpr qsizetype maximumRules = 4096;
using window_system::CommandBinding;
using Bindings = QVector<CommandBinding>;



bool supportedLanguage(const QString& id)
{
    return id == QStringLiteral("system") || id == QStringLiteral("zh_CN") || id == QStringLiteral("en");
}

bool readDocument(const QString& path, QJsonDocument& document, QString& error, QByteArray* original = nullptr)
{
    if (!QFileInfo::exists(path)) { document = {}; return true; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > maximumFileBytes) { error = QCoreApplication::translate("WindowSystem", "The configuration file exceeds the 1 MiB limit."); return false; }
    QJsonParseError parseError;
    const auto bytes = file.read(maximumFileBytes + 1);
    if (bytes.size() > maximumFileBytes) { error = QCoreApplication::translate("WindowSystem", "The configuration file exceeds the 1 MiB limit."); return false; }
    if (original) *original = bytes;
    document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QCoreApplication::translate("WindowSystem", "Invalid JSON at position %1: %2").arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    return true;
}

bool writeDocument(const QString& path, const QJsonDocument& document, QString& error)
{
    const auto bytes = document.toJson(QJsonDocument::Indented);
    if (bytes.size() > maximumFileBytes) { error = QCoreApplication::translate("WindowSystem", "The configuration file exceeds the 1 MiB limit."); return false; }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = QCoreApplication::translate("WindowSystem", "Cannot create the user configuration directory."); return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString(); return false;
    }
    return true;
}

QKeySequence parseSequence(const QString& value)
{
    auto input = value.trimmed();
    if (input.count(QRegularExpression(QStringLiteral("(?<!\\+),"))) >= 4) return {};
    // VS Code separates strokes with spaces; Qt's portable representation uses
    // commas. Keep an actual comma key ("ctrl+,") intact.
    if (!input.contains(QStringLiteral(", ")) && input.contains(QRegularExpression(QStringLiteral("\\s")))) {
        const auto strokes = input.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (strokes.size() > 4) return {};
        input = strokes.join(QStringLiteral(", "));
    }
    input.replace(QRegularExpression(QStringLiteral("\\b(?:cmd|win)\\+"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Meta+"));
    const auto sequence = QKeySequence::fromString(input, QKeySequence::PortableText);
    if (sequence.isEmpty()) return {};
    for (int index = 0; index < sequence.count(); ++index) {
        const auto key = sequence[index].key();
        if (key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt || key == Qt::Key_Meta || key == 0) return {};
    }
    return sequence;
}

QString portableSequence(const QKeySequence& sequence)
{
    QStringList strokes;
    for (int index = 0; index < sequence.count(); ++index)
        strokes.append(QKeySequence(sequence[index]).toString(QKeySequence::PortableText).toLower());
    return strokes.join(QLatin1Char(' '));
}

bool sameBinding(const CommandBinding& left, const CommandBinding& right)
{
    return left.sequence == right.sequence && left.args == right.args;
}

bool sameBindings(const Bindings& left, const Bindings& right)
{
    return left.size() == right.size() && std::equal(left.cbegin(), left.cend(), right.cbegin(), sameBinding);
}
}

QString resolveUserDirectory(const QString& explicitStateDirectory, const QString& executableDirectory)
{
    if (!explicitStateDirectory.isEmpty()) return QDir(explicitStateDirectory).filePath(QStringLiteral("User"));
    const auto portable = QDir(executableDirectory).filePath(QStringLiteral("data"));
    if (QFileInfo(portable).isDir()) return QDir(portable).filePath(QStringLiteral("User"));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("User"));
}

UserPreferences::UserPreferences(QString directory) : directory_(std::move(directory))
{
    QJsonDocument document;
    if (!readDocument(QDir(directory_).filePath(QStringLiteral("settings.json")), document, settingsDiagnostic_)) return;
    if (document.isNull()) return;
    if (!document.isObject()) { settingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "settings.json must contain a JSON object."); return; }
    const auto value = document.object().value(QStringLiteral("workbench.language"));
    if (value.isUndefined()) return;
    if (!value.isString() || !supportedLanguage(value.toString())) {
        settingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "workbench.language supports only system, zh_CN, or en."); return;
    }
    language_ = value.toString();
}

QString UserPreferences::language() const { return language_; }
QString UserPreferences::diagnostic() const
{
    QStringList errors;
    if (!settingsDiagnostic_.isEmpty()) errors.append(settingsDiagnostic_);
    if (!keybindingsDiagnostic_.isEmpty()) errors.append(keybindingsDiagnostic_);
    return errors.join(QLatin1Char('\n'));
}

bool UserPreferences::saveLanguage(const QString& language)
{
    if (!supportedLanguage(language)) { settingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "Unsupported display language."); return false; }
    const auto path = QDir(directory_).filePath(QStringLiteral("settings.json"));
    QJsonDocument document;
    settingsDiagnostic_.clear();
    if (!readDocument(path, document, settingsDiagnostic_)) return false;
    if (!document.isNull() && !document.isObject()) { settingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "settings.json must contain a JSON object."); return false; }
    auto settings = document.object();
    settings.insert(QStringLiteral("workbench.language"), language);
    if (!writeDocument(path, QJsonDocument(settings), settingsDiagnostic_)) return false;
    language_ = language;
    return true;
}

bool UserPreferences::loadKeybindings(window_system::CommandRegistry& commands)
{
    if (!defaultsCaptured_) captureDefaultBindings(commands);
    keybindingsDiagnostic_.clear();
    keybindingsWritable_ = false;
    QJsonDocument document;
    const auto path = QDir(directory_).filePath(QStringLiteral("keybindings.json"));
    QByteArray original;
    if (!readDocument(path, document, keybindingsDiagnostic_, &original)) return false;
    if (!document.isNull() && (!document.isArray() || document.array().size() > maximumRules)) {
        keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "keybindings.json must contain a JSON array with at most 4096 entries."); return false;
    }
    QMap<QString, Bindings> bindings;
    for (const auto& command : commands.commands()) {
        if (!defaults_.contains(command.id)) defaults_.insert(command.id, command.bindings);
        bindings.insert(command.id, defaults_.value(command.id, command.bindings));
    }
    int index = 0;
    for (const auto& value : document.array()) {
        ++index;
        const auto reject = [this, index](const QString& reason) {
            keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "Keybinding rule %1: %2").arg(index).arg(reason); return false;
        };
        if (!value.isObject()) return reject(QCoreApplication::translate("WindowSystem", "The rule must be an object."));
        const auto rule = value.toObject();
        for (const auto& key : rule.keys()) if (key != QStringLiteral("key") && key != QStringLiteral("command") && key != QStringLiteral("args"))
            return reject(QCoreApplication::translate("WindowSystem", "Unsupported field: %1").arg(key));
        if (!rule.value(QStringLiteral("command")).isString() || !rule.value(QStringLiteral("key")).isString())
            return reject(QCoreApplication::translate("WindowSystem", "command and key must be strings."));
        auto command = rule.value(QStringLiteral("command")).toString();
        const auto key = rule.value(QStringLiteral("key")).toString();
        const bool remove = command.startsWith(QLatin1Char('-'));
        if (remove) command.remove(0, 1);
        const auto sequence = parseSequence(key);
        if (command.isEmpty() || command.size() > 256 || key.size() > 256 || sequence.isEmpty())
            return reject(QCoreApplication::translate("WindowSystem", "Invalid command ID or shortcut."));
        if (rule.contains(QStringLiteral("args")) && !rule.value(QStringLiteral("args")).isObject())
            return reject(QCoreApplication::translate("WindowSystem", "args must be an object."));
        if (!commands.find(command)) continue;
        if (remove) {
            auto& values = bindings[command];
            const bool matchArguments = rule.contains(QStringLiteral("args"));
            const auto arguments = rule.value(QStringLiteral("args")).toObject().toVariantMap();
            values.erase(std::remove_if(values.begin(), values.end(), [&](const CommandBinding& binding) {
                return binding.sequence == sequence && (!matchArguments || binding.args == arguments);
            }), values.end());
        } else {
            const CommandBinding binding{sequence, rule.value(QStringLiteral("args")).toObject().toVariantMap()};
            auto& values = bindings[command];
            if (std::none_of(values.cbegin(), values.cend(), [&binding](const CommandBinding& existing) { return sameBinding(existing, binding); }))
                values.append(binding);
        }
    }
    // Validate and commit the entire batch atomically. A rejected binding never
    // exposes partially imported values to registry observers.
    QHash<QString, Bindings> batch;
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) batch.insert(it.key(), it.value());
    if (!commands.setBindingsBatch(std::move(batch))) {
        keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "The keybinding rules exceed command binding limits. Existing bindings were kept."); return false;
    }
    userRules_ = document.array();
    persistedRules_ = userRules_;
    observedBindings_ = std::move(bindings);
    keybindingsFileExisted_ = QFileInfo::exists(path);
    keybindingsBytes_ = original;
    keybindingsWritable_ = true;
    return true;
}

bool UserPreferences::restoreDefaultBindings(window_system::CommandRegistry& commands) const
{
    if (!defaultsCaptured_) return true;
    QHash<QString, Bindings> batch;
    for (auto it = defaults_.cbegin(); it != defaults_.cend(); ++it)
        if (commands.find(it.key())) batch.insert(it.key(), it.value());
    return commands.setBindingsBatch(std::move(batch));
}

void UserPreferences::captureDefaultBindings(const window_system::CommandRegistry& commands)
{
    defaults_.clear(); observedBindings_.clear();
    for (const auto& command : commands.commands()) {
        defaults_.insert(command.id, command.bindings);
        observedBindings_.insert(command.id, command.bindings);
    }
    defaultsCaptured_ = true;
}

bool UserPreferences::stageKeybindings(const window_system::CommandRegistry& commands)
{
    if (!defaultsCaptured_ || !keybindingsWritable_) return false;
    QMap<QString, Bindings> current;
    for (const auto& command : commands.commands()) current.insert(command.id, command.bindings);
    if (current == observedBindings_) return true;
    QJsonArray rules;
    // Unknown rules remain useful when a temporarily unloaded extension comes
    // back. For just-removed commands the last observed effective binding below
    // replaces those rules, preserving an edit made before the deferred save.
    for (const auto& value : userRules_) {
        auto id = value.toObject().value(QStringLiteral("command")).toString();
        if (id.startsWith(QLatin1Char('-'))) id.remove(0, 1);
        if (!current.contains(id) && !observedBindings_.contains(id)) rules.append(value);
    }
    const auto appendRules = [&rules](const QString& id, const Bindings& defaults, const Bindings& effective) {
        if (sameBindings(defaults, effective)) return;
        for (const auto& binding : defaults)
            rules.append(QJsonObject{{QStringLiteral("key"), portableSequence(binding.sequence)},
                {QStringLiteral("command"), QLatin1Char('-') + id}});
        for (const auto& binding : effective) {
            QJsonObject rule{{QStringLiteral("key"), portableSequence(binding.sequence)}, {QStringLiteral("command"), id}};
            if (!binding.args.isEmpty()) rule.insert(QStringLiteral("args"), QJsonObject::fromVariantMap(binding.args));
            rules.append(rule);
        }
    };
    for (auto it = observedBindings_.cbegin(); it != observedBindings_.cend(); ++it)
        if (!current.contains(it.key())) appendRules(it.key(), defaults_.value(it.key()), it.value());
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (!defaults_.contains(it.key())) defaults_.insert(it.key(), it.value());
        appendRules(it.key(), defaults_.value(it.key()), it.value());
    }
    if (rules.size() > maximumRules) {
        keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "The keybinding rules exceed the 4096-entry limit.");
        return false;
    }
    for (auto it = defaults_.begin(); it != defaults_.end();)
        if (!current.contains(it.key())) it = defaults_.erase(it); else ++it;
    observedBindings_ = std::move(current);
    userRules_ = std::move(rules);
    return true;
}

bool UserPreferences::saveKeybindings(const window_system::CommandRegistry& commands)
{
    if (!defaultsCaptured_ || !keybindingsWritable_) {
        if (keybindingsDiagnostic_.isEmpty()) keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "The keybindings file has not been read successfully. The original file was kept.");
        return false;
    }
    if (!stageKeybindings(commands)) return false;
    // Do not overwrite an external editor's changes with an older UI snapshot.
    // Files are bounded; compare the saved bytes rather than computing hashes.
    const auto path = QDir(directory_).filePath(QStringLiteral("keybindings.json"));
    QFile existing(path);
    if (QFileInfo::exists(path) != keybindingsFileExisted_ ||
        (keybindingsFileExisted_ && (!existing.open(QIODevice::ReadOnly) ||
         existing.size() > maximumFileBytes || existing.read(maximumFileBytes + 1) != keybindingsBytes_))) {
        keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "The keybindings file changed externally and was not overwritten. Restart to load the changes.");
        return false;
    }
    existing.close();
    const QJsonArray rules = userRules_;
    if (rules.size() > maximumRules) { keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "The keybinding rules exceed the 4096-entry limit."); return false; }
    keybindingsDiagnostic_.clear();
    const QJsonDocument document(rules);
    if (!writeDocument(path, document, keybindingsDiagnostic_)) return false;
    keybindingsBytes_ = document.toJson(QJsonDocument::Indented);
    persistedRules_ = rules;
    keybindingsFileExisted_ = true;
    return true;
}

} // namespace muxora
