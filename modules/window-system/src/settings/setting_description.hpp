// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QVariant>
#include <QVector>
#include <window_system/layout_model.hpp>
#include <functional>

namespace window_system {
class WindowManager;
struct TabAppearance;

// One built-in setting as a settings page lists it, independent of the
// presentation. The control follows the kind (settings design §3): a check
// box, a drop-down marking the default, a number box with an optional unit,
// a text field, a slider over the common part of a number's range next to a
// number box for all of it, or a JSON text applied with a button.
struct SettingDescription {
    enum class Kind { Toggle, Choice, Integer, Real, Text, IntegerSlider, Json };
    struct Choice {
        QString label;
        QVariant value;
    };
    QString category; // A category ID; "parent/child" nests.
    QString id;       // The setting's stable ID, shown under the row.
    QString title;
    QString description;
    Kind kind = Kind::Toggle;
    QString checkLabel;      // Toggle: the text beside the check box.
    QVector<Choice> choices; // Choice
    QVariant defaultValue;   // Choice: the marked entry; none when invalid.
    double minimum = 0, maximum = 0, step = 1;
    int decimals = 0;        // Real
    QString suffix;          // Number boxes: a unit such as " px".
    int sliderMaximum = 0;   // IntegerSlider: the slider covers 0 to this.
    int maximumLength = 0;   // Text: 0 for no limit.
    QString placeholder;     // Text
    std::function<QVariant()> read;
    // Applies a value from the control; false leaves the setting as it was.
    std::function<bool(const QVariant&)> write;
    // Whether the control can be edited now; always when unset.
    std::function<bool()> enabled;
    // Json: read gives the text; apply checks and applies an edited text and
    // says what happened in `message`. False leaves the setting as it was.
    QString applyLabel;
    std::function<bool(const QString& text, QString& message)> apply;
};

struct SettingCategory {
    QString id;
    QString title;
};

// The categories and settings the Quick settings page shows, in page order.
// Sections are added here as they move from the Widgets page (10.4h).
[[nodiscard]] QVector<SettingCategory> settingCategories(WindowManager* manager);
// Resolve the page's current window when reading or editing visibility, so a
// page moved to another window follows that window without being recreated.
[[nodiscard]] QVector<SettingDescription> describeSettings(WindowManager* manager,
    const std::function<WindowId()>& currentWindow);

[[nodiscard]] QVector<SettingDescription> describeViewSettings(WindowManager* manager,
    const std::function<WindowId()>& currentWindow);
[[nodiscard]] QVector<SettingDescription> describeTabSettings(WindowManager* manager);

// The tab color list as both settings pages edit it: an indented JSON array,
// applied over the other tab appearance settings.
[[nodiscard]] QString tabColorsText(const TabAppearance& appearance);
bool applyTabColorsText(WindowManager& manager, const QString& text, QString& message);

} // namespace window_system
