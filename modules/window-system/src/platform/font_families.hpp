// SPDX-License-Identifier: MIT
#pragma once

#include <QStringList>
#include <functional>

namespace window_system {
// Keep known Qt families and missing fallbacks intact. A platform alias is
// usable only when its target is present in Qt's own font collection.
inline QStringList mapFontFamilyAliases(QStringList requested, const QStringList& available,
                                        const std::function<QString(const QString&)>& lookup)
{
    for (auto& family : requested) {
        if (available.contains(family, Qt::CaseInsensitive)) continue;
        const auto mapped = lookup(family);
        if (!mapped.isEmpty() && available.contains(mapped, Qt::CaseInsensitive)) family = mapped;
    }
    return requested;
}
#ifdef Q_OS_WIN
[[nodiscard]] QStringList resolvePlatformFontFamilies(const QStringList& requested);
#else
inline QStringList resolvePlatformFontFamilies(const QStringList& requested) { return requested; }
#endif
} // namespace window_system
