// SPDX-License-Identifier: MIT
#include "platform/font_families.hpp"

#include <QtGui/private/qfontdatabase_p.h>
#include <QtGui/private/qwindowsfontdatabasebase_p.h>
#include <QFontDatabase>
#include <QHash>
#include <QSet>
#include <array>
#include <dwrite_2.h>
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

// One character for IDWriteFontFallback::MapCharacters.
class CharacterSource final : public IDWriteTextAnalysisSource {
public:
    explicit CharacterSource(std::wstring text) : text_(std::move(text)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
    {
        *object = id == __uuidof(IUnknown) || id == __uuidof(IDWriteTextAnalysisSource) ? this : nullptr;
        return *object ? S_OK : E_NOINTERFACE;
    }
    // Lives on the stack for one synchronous call.
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text, UINT32* length) override
    {
        const bool inside = position < text_.size();
        *text = inside ? text_.data() + position : nullptr;
        *length = inside ? UINT32(text_.size() - position) : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text, UINT32* length) override
    {
        const bool inside = position > 0 && position <= text_.size();
        *text = inside ? text_.data() : nullptr;
        *length = inside ? position : 0;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32, UINT32* length, const WCHAR** locale) override
    {
        *locale = L""; *length = UINT32(text_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32, UINT32* length, IDWriteNumberSubstitution** substitution) override
    {
        *substitution = nullptr; *length = UINT32(text_.size());
        return S_OK;
    }

private:
    std::wstring text_;
};

constexpr int scriptCount = QFontDatabasePrivate::ScriptCount;

// Visible characters spread over each script; up to 1024 for the shared
// scripts, whose symbols span many fonts, and 64 for the others.
const std::array<QList<char32_t>, scriptCount>& scriptSamples()
{
    static const auto samples = [] {
        std::array<QList<char32_t>, scriptCount> all;
        for (char32_t c = 0x20; c < 0x40000; ++c) {
            const auto category = QChar::category(c);
            if (category == QChar::Other_NotAssigned || category == QChar::Other_Control || category == QChar::Other_Format
                || category == QChar::Other_Surrogate || category == QChar::Other_PrivateUse || category == QChar::Separator_Space) continue;
            all[QChar::script(c)].append(c);
        }
        for (int script = 0; script < scriptCount; ++script) {
            auto& chars = all[script];
            const qsizetype limit = script == QChar::Script_Common || script == QChar::Script_Inherited ? 1024 : 64;
            if (chars.size() <= limit) continue;
            QList<char32_t> spread;
            for (qsizetype i = 0; i < limit; ++i) spread.append(chars.at(i * chars.size() / limit));
            chars = spread;
        }
        return all;
    }();
    return samples;
}

using ScriptFonts = std::array<QStringList, scriptCount>;

// The families the system font fallback picks for each script's samples
// next to `base`. Kept per base family while the installed fonts stay the
// same; asking DirectWrite is most of the prefill's time.
const ScriptFonts* systemFallbacks(const QString& base, const QStringList& available)
{
    static QHash<QString, ScriptFonts> cache;
    const auto key = base + QChar(0) + QString::number(qHashRange(available.cbegin(), available.cend()));
    if (const auto found = cache.constFind(key); found != cache.cend()) return &*found;
    ComPtr<IDWriteFactory2> factory;
    ComPtr<IDWriteFontFallback> fallback;
    ComPtr<IDWriteFontCollection> collection;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), reinterpret_cast<IUnknown**>(factory.GetAddressOf())))
        || FAILED(factory->GetSystemFontFallback(&fallback)) || FAILED(factory->GetSystemFontCollection(&collection))) return nullptr;
    const auto wide = base.toStdWString();
    const auto& samples = scriptSamples();
    ScriptFonts fonts;
    for (int script = 0; script < scriptCount; ++script) {
        auto& mapped = fonts[script];
        for (const char32_t c : samples[script]) {
            auto text = QString::fromUcs4(&c, 1).toStdWString();
            const auto units = UINT32(text.size()); // Two for a supplementary character.
            CharacterSource source(std::move(text));
            UINT32 length = 0; FLOAT scale = 1;
            ComPtr<IDWriteFont> font;
            ComPtr<IDWriteFontFamily> family;
            ComPtr<IDWriteLocalizedStrings> names;
            if (FAILED(fallback->MapCharacters(&source, 0, units, collection.Get(), wide.c_str(), DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &length, &font, &scale))
                || !font || FAILED(font->GetFontFamily(&family)) || FAILED(family->GetFamilyNames(&names))) continue;
            const auto name = qtFamilyName(*names.Get(), available);
            if (!name.isEmpty() && !mapped.contains(name)) mapped.append(name);
        }
    }
    if (cache.size() > 8) cache.clear();
    return &*cache.insert(key, fonts);
}
}

void prefillFontFallbacks(const QStringList& families, QFont::StyleHint hint)
{
    if (families.isEmpty()) return;
    const auto available = QFontDatabase::families();
    const auto* system = systemFallbacks(families.first(), available);
    if (!system) return;
    QSet<QString> installed;
    for (const auto& name : available) installed.insert(name.toCaseFolded());
    QHash<QString, QStringList> tryFonts;
    QList<QFont::StyleHint> hints{QFont::AnyStyle};
    if (hint != QFont::AnyStyle) hints.append(hint);
    QList<std::pair<QtFontFallbacksCacheKey, QStringList>> entries;
    for (int script = 0; script < scriptCount; ++script) {
        const auto& mapped = system->at(script);
        // A script the system maps to no font keeps Qt's complete search.
        const bool shared = script == QChar::Script_Common || script == QChar::Script_Inherited
            || script == QChar::Script_Latin || script == QFontDatabasePrivate::Script_Emoji;
        if (mapped.isEmpty() && !shared) continue;
        // Qt also asks for the fallbacks of a font it reached through these
        // lists, while the request still names further families.
        QStringList owners = families;
        for (qsizetype index = 0; index < owners.size(); ++index) {
            const auto owner = owners.at(index);
            auto found = tryFonts.find(owner);
            if (found == tryFonts.end()) found = tryFonts.insert(owner, QWindowsFontDatabaseBase::extraTryFontsForFamily(owner));
            for (const auto styleHint : std::as_const(hints)) {
                // As QWindowsDirectWriteFontDatabase::fallbacksForFamily, with
                // the system's mapping instead of every installed family.
                auto candidates = QWindowsFontDatabaseBase::familiesForScript(QFontDatabasePrivate::ExtendedScript(script));
                candidates.append(QWindowsFontDatabaseBase::familyForStyleHint(styleHint));
                candidates += *found;
                candidates += mapped;
                QStringList list;
                QSet<QString> listed;
                for (const auto& name : std::as_const(candidates)) {
                    const auto folded = name.toCaseFolded();
                    if (!installed.contains(folded) || listed.contains(folded)) continue;
                    listed.insert(folded);
                    list.append(name);
                    if (!owners.contains(name, Qt::CaseInsensitive)) owners.append(name);
                }
                for (const auto style : {QFont::StyleNormal, QFont::StyleItalic, QFont::StyleOblique})
                    entries.append({{owner, style, styleHint, script}, list});
            }
        }
    }
    // Qt's cache (QFontDatabasePrivate, Qt 6.12) is keyed by family, style,
    // style hint and script and holds 64 lists by default. Qt takes its font
    // database lock around it, which is not exported; the interface fonts
    // change only on the GUI thread while no other thread loads fonts.
    auto* database = QFontDatabasePrivate::ensureFontDatabase();
    if (database->fallbacksCache.maxCost() < entries.size() + 64) database->fallbacksCache.setMaxCost(entries.size() + 64);
    for (const auto& [key, list] : std::as_const(entries)) database->fallbacksCache.insert(key, new QStringList(list));
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
