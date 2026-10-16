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
    if (file.size() > maximumFileBytes) { error = QCoreApplication::translate("WindowSystem", "配置文件超过 1 MiB 限制。"); return false; }
    QJsonParseError parseError;
    const auto bytes = file.read(maximumFileBytes + 1);
    if (bytes.size() > maximumFileBytes) { error = QCoreApplication::translate("WindowSystem", "配置文件超过 1 MiB 限制。"); return false; }
    if (original) *original = bytes;
    document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QCoreApplication::translate("WindowSystem", "JSON 格式错误（位置 %1）：%2").arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    return true;
}

bool writeDocument(const QString& path, const QJsonDocument& document, QString& error)
{
    const auto bytes = document.toJson(QJsonDocument::Indented);
    if (bytes.size() > maximumFileBytes) { error = QCoreApplication::translate("WindowSystem", "配置文件超过 1 MiB 限制。"); return false; }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = QCoreApplication::translate("WindowSystem", "无法创建用户配置目录。"); return false;
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
    if (!document.isObject()) { settingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "settings.json 必须是 JSON 对象。"); return; }
    const auto value = document.object().value(QStringLiteral("workbench.language"));
    if (value.isUndefined()) return;
    if (!value.isString() || !supportedLanguage(value.toString())) {
        settingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "workbench.language 只支持 system、zh_CN 或 en。"); return;
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
    if (!supportedLanguage(language)) { settingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "不支持此界面语言。"); return false; }
    const auto path = QDir(directory_).filePath(QStringLiteral("settings.json"));
    QJsonDocument document;
    settingsDiagnostic_.clear();
    if (!readDocument(path, document, settingsDiagnostic_)) return false;
    if (!document.isNull() && !document.isObject()) { settingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "settings.json 必须是 JSON 对象。"); return false; }
    auto settings = document.object();
    settings.insert(QStringLiteral("workbench.language"), language);
    if (!writeDocument(path, QJsonDocument(settings), settingsDiagnostic_)) return false;
    language_ = language;
    return true;
}

bool UserPreferences::loadKeybindings(window_system::CommandRegistry& commands)
{
    if (!defaultsCaptured_) {
        for (const auto& command : commands.commands()) defaults_.insert(command.id, commands.bindings(command.id));
        defaultsCaptured_ = true;
    }
    keybindingsDiagnostic_.clear();
    keybindingsWritable_ = false;
    QJsonDocument document;
    const auto path = QDir(directory_).filePath(QStringLiteral("keybindings.json"));
    QByteArray original;
    if (!readDocument(path, document, keybindingsDiagnostic_, &original)) return false;
    if (!document.isNull() && (!document.isArray() || document.array().size() > maximumRules)) {
        keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "keybindings.json 必须是最多 4096 项的 JSON 数组。"); return false;
    }
    auto bindings = defaults_;
    QJsonArray unknown;
    int index = 0;
    for (const auto& value : document.array()) {
        ++index;
        const auto reject = [this, index](const QString& reason) {
            keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "快捷键规则 %1：%2").arg(index).arg(reason); return false;
        };
        if (!value.isObject()) return reject(QCoreApplication::translate("WindowSystem", "规则必须是对象。"));
        const auto rule = value.toObject();
        for (const auto& key : rule.keys()) if (key != QStringLiteral("key") && key != QStringLiteral("command") && key != QStringLiteral("args"))
            return reject(QCoreApplication::translate("WindowSystem", "当前不支持字段：%1").arg(key));
        if (!rule.value(QStringLiteral("command")).isString() || !rule.value(QStringLiteral("key")).isString())
            return reject(QCoreApplication::translate("WindowSystem", "command 和 key 必须是字符串。"));
        auto command = rule.value(QStringLiteral("command")).toString();
        const auto key = rule.value(QStringLiteral("key")).toString();
        const bool remove = command.startsWith(QLatin1Char('-'));
        if (remove) command.remove(0, 1);
        const auto sequence = parseSequence(key);
        if (command.isEmpty() || command.size() > 256 || key.size() > 256 || sequence.isEmpty())
            return reject(QCoreApplication::translate("WindowSystem", "命令 ID 或快捷键无效。"));
        if (rule.contains(QStringLiteral("args")) && !rule.value(QStringLiteral("args")).isObject())
            return reject(QCoreApplication::translate("WindowSystem", "args 必须是对象。"));
        if (!commands.find(command)) { unknown.append(rule); continue; }
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
    // Parse the entire file before touching the registry. If a registry policy
    // rejects a binding, restore all previous values instead of partial import.
    QMap<QString, Bindings> previous;
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        previous.insert(it.key(), commands.bindings(it.key()));
        if (!commands.setBindings(it.key(), it.value())) {
            for (auto old = previous.cbegin(); old != previous.cend(); ++old) commands.setBindings(old.key(), old.value());
            keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "快捷键规则不符合命令绑定限制，已保留原绑定。"); return false;
        }
    }
    unknownRules_ = unknown;
    keybindingsFileExisted_ = QFileInfo::exists(path);
    keybindingsBytes_ = original;
    keybindingsWritable_ = true;
    return true;
}

bool UserPreferences::saveKeybindings(const window_system::CommandRegistry& commands)
{
    if (!defaultsCaptured_ || !keybindingsWritable_) {
        if (keybindingsDiagnostic_.isEmpty()) keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "快捷键文件尚未成功读取，未覆盖原文件。");
        return false;
    }
    // Do not overwrite an external editor's changes with an older UI snapshot.
    // Files are bounded; compare the saved bytes rather than computing hashes.
    const auto path = QDir(directory_).filePath(QStringLiteral("keybindings.json"));
    QFile existing(path);
    if (QFileInfo::exists(path) != keybindingsFileExisted_ ||
        (keybindingsFileExisted_ && (!existing.open(QIODevice::ReadOnly) ||
         existing.size() > maximumFileBytes || existing.read(maximumFileBytes + 1) != keybindingsBytes_))) {
        keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "快捷键文件已被外部修改，未覆盖；请重新启动以加载修改。");
        return false;
    }
    existing.close();
    QJsonArray rules = unknownRules_;
    for (const auto& command : commands.commands()) {
        const auto current = commands.bindings(command.id);
        if (!defaults_.contains(command.id)) defaults_.insert(command.id, current);
        const auto defaults = defaults_.value(command.id);
        if (sameBindings(defaults, current)) continue;
        for (const auto& binding : defaults)
            rules.append(QJsonObject{{QStringLiteral("key"), portableSequence(binding.sequence)}, {QStringLiteral("command"), QLatin1Char('-') + command.id}});
        for (const auto& binding : current) {
            QJsonObject rule{{QStringLiteral("key"), portableSequence(binding.sequence)}, {QStringLiteral("command"), command.id}};
            if (!binding.args.isEmpty()) rule.insert(QStringLiteral("args"), QJsonObject::fromVariantMap(binding.args));
            rules.append(rule);
        }
    }
    if (rules.size() > maximumRules) { keybindingsDiagnostic_ = QCoreApplication::translate("WindowSystem", "快捷键规则超过 4096 项限制。"); return false; }
    keybindingsDiagnostic_.clear();
    const QJsonDocument document(rules);
    if (!writeDocument(path, document, keybindingsDiagnostic_)) return false;
    keybindingsBytes_ = document.toJson(QJsonDocument::Indented);
    keybindingsFileExisted_ = true;
    return true;
}

} // namespace muxora
