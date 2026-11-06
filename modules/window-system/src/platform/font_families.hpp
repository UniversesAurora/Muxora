// SPDX-License-Identifier: MIT
#pragma once

#include <QFont>
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
// Gives Qt the fallback fonts of these families before it computes them: its
// own choices first, then what the system font fallback maps each script to,
// as browsers ask it. Qt's computation loads every installed family once,
// about a second with many fonts; it still runs for a script the system maps
// to no font. Call on the GUI thread whenever the families or the font
// database change, before text in these families is laid out.
void prefillFontFallbacks(const QStringList& families, QFont::StyleHint hint);
#else
inline QStringList resolvePlatformFontFamilies(const QStringList& requested) { return requested; }
inline void prefillFontFallbacks(const QStringList&, QFont::StyleHint) {}
#endif
} // namespace window_system
