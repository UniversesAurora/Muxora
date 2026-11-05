// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QVariant>
#include <QVector>
#include <functional>

namespace window_system {
class WindowManager;

// One built-in setting as a settings page lists it, independent of the
// presentation. The control follows the kind (settings design §3): a check
// box, a drop-down marking the default, a number box with an optional unit,
// a text field, or a slider over the common part of a number's range next to
// a number box for all of it.
struct SettingDescription {
    enum class Kind { Toggle, Choice, Integer, Real, Text, IntegerSlider };
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
};

struct SettingCategory {
    QString id;
    QString title;
};

// The categories and settings the Quick settings page shows, in page order.
// Sections are added here as they move from the Widgets page (10.4h).
[[nodiscard]] QVector<SettingCategory> settingCategories(WindowManager* manager);
[[nodiscard]] QVector<SettingDescription> describeSettings(WindowManager* manager);

} // namespace window_system
