// SPDX-License-Identifier: MIT
#include "platform/font_families.hpp"

#include <QFontDatabase>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

namespace window_system {
namespace {
using Microsoft::WRL::ComPtr;

QString qtFamilyName(IDWriteLocalizedStrings& names, const QStringList& available)
{
    UINT32 english = 0; BOOL exists = FALSE;
    names.FindLocaleName(L"en-us", &english, &exists);
    const auto at = [&](UINT32 index) {
        UINT32 length = 0;
        if (FAILED(names.GetStringLength(index, &length))) return QString{};
        std::wstring text(length + 1, L'\0');
        if (FAILED(names.GetString(index, text.data(), length + 1))) return QString{};
        return QString::fromWCharArray(text.data(), int(length));
    };
    if (exists) {
        const auto name = at(english);
        if (available.contains(name, Qt::CaseInsensitive)) return name;
    }
    for (UINT32 index = 0; index < names.GetCount(); ++index) {
        const auto name = at(index);
        if (available.contains(name, Qt::CaseInsensitive)) return name;
    }
    return {};
}
}

QStringList resolvePlatformFontFamilies(const QStringList& requested)
{
    const auto available = QFontDatabase::families();
    ComPtr<IDWriteFactory> factory;
    ComPtr<IDWriteFontCollection> collection;
    bool initialized = false;
    return mapFontFamilyAliases(requested, available, [&](const QString& family) -> QString {
        if (!initialized) {
            initialized = true;
            if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) return {};
            // The legacy/WWS collection used by Chromium can expose names
            // absent from Qt's typographic collection. Ask the OS, not a
            // guessed prefix or a list of aliases specific to one machine.
            if (FAILED(factory->GetSystemFontCollection(&collection, TRUE))) return {};
        }
        if (!collection) return {};
        UINT32 index = 0; BOOL exists = FALSE;
        const auto wide = family.toStdWString();
        if (FAILED(collection->FindFamilyName(wide.c_str(), &index, &exists)) || !exists) return {};
        ComPtr<IDWriteFontFamily> legacy;
        ComPtr<IDWriteFont> font;
        if (FAILED(collection->GetFontFamily(index, &legacy))
            || FAILED(legacy->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                  DWRITE_FONT_STYLE_NORMAL, &font))) return {};
        ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_TYPOGRAPHIC_FAMILY_NAMES,
                                                  &names, &exists)) || !exists || !names) return {};
        return qtFamilyName(*names.Get(), available);
    });
}
} // namespace window_system
