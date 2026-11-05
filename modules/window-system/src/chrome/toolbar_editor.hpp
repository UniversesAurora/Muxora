// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/custom_chrome.hpp>
#include <QStringList>
#include <QVector>
#include <optional>

namespace window_system {

// The toolbar layout of one window, whichever presentation shows it. Widths
// are authored (unscaled) lengths, as stored. A successful edit is applied
// at once and committed as the window's saved layout.
class ToolbarEditor {
public:
    [[nodiscard]] virtual QStringList toolbarIds() const = 0;
    [[nodiscard]] virtual QString toolbarTitle(const QString& id) const = 0;
    [[nodiscard]] virtual std::optional<ToolbarState> toolbarState(const QString& id) const = 0;
    // Rows of an area from its outer edge, as stored.
    [[nodiscard]] virtual QVector<QStringList> toolbarRows(ToolbarArea area) const = 0;
    [[nodiscard]] virtual bool isToolbarSizingEditable(const QString& id) const = 0;
    virtual bool setToolbarVisible(const QString& id, bool visible) = 0;
    virtual bool setToolbarWidth(const QString& id, int width) = 0;
    virtual bool resetToolbarWidth(const QString& id) = 0;
    virtual bool setToolbarElastic(const QString& id, bool elastic) = 0;
    virtual bool setToolbarItemVisible(const QString& id, const QString& item, bool visible) = 0;
    // The row exists or is appended; index is within the row.
    virtual bool moveToolbar(const QString& id, ToolbarArea area, int row, int index) = 0;
    virtual void setToolbarsLocked(bool locked) = 0;
    [[nodiscard]] virtual bool toolbarsLocked() const = 0;

protected:
    ~ToolbarEditor() = default;
};

} // namespace window_system
