// SPDX-License-Identifier: MIT
#include "layout_file.hpp"

#include <window_system/window_manager.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

namespace muxora {
namespace {
constexpr qint64 maximumLayoutBytes = 4 * 1024 * 1024;

struct Section {
    const char* key;
    const char* name;
    QJsonValue empty; // What the restore accepts as "use the defaults".
};

// Settings saved with the layout. Each can be reset on its own without
// touching the windows and tabs.
const Section sections[] = {
    {"tabAppearance", QT_TRANSLATE_NOOP("Muxora", "tab appearance"), QJsonValue::Undefined},
    {"behavior", QT_TRANSLATE_NOOP("Muxora", "window behavior"), QJsonValue::Undefined},
    {"visibilityProfiles", QT_TRANSLATE_NOOP("Muxora", "default bar visibility"), QJsonObject{}},
    {"toolbarProfiles", QT_TRANSLATE_NOOP("Muxora", "default toolbar layout"), QJsonObject{}},
};

QJsonObject withDefaults(QJsonObject snapshot, const QVector<const Section*>& reset)
{
    for (const auto* section : reset) {
        if (section->empty.isUndefined()) snapshot.remove(QLatin1String(section->key));
        else snapshot.insert(QLatin1String(section->key), section->empty);
    }
    return snapshot;
}

QString keepCopy(const QString& path)
{
    const QFileInfo info(path);
    const auto stem = info.completeBaseName() + QStringLiteral("-invalid-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    auto copy = info.dir().filePath(stem + QStringLiteral(".json"));
    for (int index = 1; QFileInfo::exists(copy); ++index)
        copy = info.dir().filePath(stem + QStringLiteral("-%1.json").arg(index));
    return QFile::copy(path, copy) ? QDir::toNativeSeparators(copy) : QString();
}

QString keptText(const QString& copy)
{
    return copy.isEmpty() ? QCoreApplication::translate("Muxora", "The file could not be copied and will be replaced at the next save.")
        : QCoreApplication::translate("Muxora", "The original file was kept as %1.").arg(copy);
}
}

LayoutFileRestore restoreLayoutFile(window_system::WindowManager& windows, const QString& path)
{
    if (!QFileInfo::exists(path)) return {};
    QFile file(path);
    QString error;
    QJsonObject snapshot;
    if (!file.open(QIODevice::ReadOnly)) error = file.errorString();
    else if (file.size() > maximumLayoutBytes) error = QCoreApplication::translate("Muxora", "the file exceeds 4 MiB");
    else {
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError) error = parse.errorString();
        else if (!document.isObject()) error = QCoreApplication::translate("Muxora", "the file does not contain a JSON object");
        else snapshot = document.object();
    }
    file.close();
    if (!error.isEmpty())
        return {false, QCoreApplication::translate("Muxora", "The saved layout could not be read (%1), so the default layout is shown.").arg(error)
            + QLatin1Char(' ') + keptText(keepCopy(path))};

    const auto first = windows.restoreLayout(snapshot);
    if (first.ok) return {true, {}};
    // Try resetting one section at a time first, the usual case of a single
    // bad value, then all of them. A failed restore changes nothing.
    QVector<QVector<const Section*>> attempts;
    for (const auto& section : sections) attempts.append({&section});
    QVector<const Section*> all;
    for (const auto& section : sections) all.append(&section);
    attempts.append(all);
    for (const auto& reset : attempts) {
        if (!windows.restoreLayout(withDefaults(snapshot, reset)).ok) continue;
        QStringList names;
        for (const auto* section : reset) names.append(QCoreApplication::translate("Muxora", section->name));
        return {true, QCoreApplication::translate("Muxora", "Some settings in the saved layout were invalid (%1). Reset to defaults: %2.")
            .arg(first.error, QLocale().createSeparatedList(names)) + QLatin1Char(' ') + keptText(keepCopy(path))};
    }
    return {false, QCoreApplication::translate("Muxora", "The saved layout could not be restored (%1), so the default layout is shown.").arg(first.error)
        + QLatin1Char(' ') + keptText(keepCopy(path))};
}

} // namespace muxora
