// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <functional>

namespace window_system {

// One input of a dialog. Values are QString for text and int for numbers.
struct DialogField {
    enum class Kind { Text, Lines, Integer };
    QString id;    // The key of its value; also its object name.
    QString label; // May hold a & mnemonic.
    Kind kind = Kind::Text;
    QVariant value;
    int minimum = 0, maximum = 0; // Integer
    QString suffix;               // Integer: a unit such as " px", only shown.
    QString emptyText;            // Integer: shown for the minimum, meaning none.
};

// A dialog over a window, shown by the presentation without blocking the
// caller (WindowManager::openDialog). It closes on accepting, on cancelling,
// on closing its window, or when it no longer applies.
struct DialogRequest {
    QString name;  // Object name, for tests and assistive tools.
    QString title;
    QString text;  // Plain text above the fields.
    QVector<DialogField> fields;
    QString acceptText; // Empty: OK.
    QString cancelText; // Empty: Cancel.
    bool cancellable = true; // False: only the accept button.
    bool modal = true;       // Blocks input to its window while open.
    int timeoutMs = 0;       // Closes by itself after this, when nonzero.
    QSize minimumSize;       // In logical pixels; empty to fit the content.
    // Whether the values can be accepted now; always when unset.
    std::function<bool(const QVariantMap& values)> acceptable;
    // Checked as the layout changes; the dialog closes once it returns false.
    std::function<bool()> applies;
    std::function<void(const QVariantMap& values)> accepted;
    // Runs once the dialog has closed, accepted or not.
    std::function<void()> finished;
};

// Shows dialogs for a presentation. The manager assigns the IDs and keeps
// track of which are open; the presenter reports each one's end once.
class DialogPresenter {
public:
    virtual ~DialogPresenter() = default;
    // `finished` runs once when the dialog closes, after `accepted` if it was
    // accepted. It may run before open() returns.
    virtual void open(quint64 id, WindowId window, const DialogRequest& request, std::function<void()> finished) = 0;
    virtual void close(quint64 id) = 0;
    virtual void raise(quint64 id) = 0;
};

} // namespace window_system
