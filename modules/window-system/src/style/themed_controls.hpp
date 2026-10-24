// SPDX-License-Identifier: MIT
#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPointer>

namespace window_system {
struct Theme;

// A narrow overlay keeps the label's color and active underline visible through
// the handle. Both orientations use the same restrained hover emphasis.
[[nodiscard]] QString overlayScrollbarStyle(const Theme& theme);

// Native input, focus and accessibility behavior are retained. Only the small
// indicators are painted here, resolving colors from the nearest Theme root.
//
// Like VS Code's settings dropdowns, a popup row can show a muted detail after
// its text (for example an identifier) and "Default" at its trailing edge.
// Both are optional: a list without details or a default draws plain rows.
class ThemedComboBox final : public QComboBox {
public:
    // Item data role for the muted text drawn after an item's text.
    static constexpr int DetailRole = Qt::UserRole + 0x100;

    explicit ThemedComboBox(QWidget* parent = nullptr);
    void showPopup() override;
    void setItemDetail(int index, const QString& detail);
    // Marks the item whose data equals `value` as the default. A value that
    // matches no item, or an invalid QVariant, clears the mark.
    void setDefaultData(const QVariant& value);
    [[nodiscard]] int defaultIndex() const;
protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void alignPopup();
    QPointer<QWidget> popup_;
    QVariant defaultData_;
};

class ThemedSpinBox : public QSpinBox {
public:
    explicit ThemedSpinBox(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
};

// The configured suffix is a display unit, not a unit-conversion expression.
// Keep Qt's editor and step buttons while accepting a leading integer.
class UnitSpinBox final : public ThemedSpinBox {
public:
    using ThemedSpinBox::ThemedSpinBox;
protected:
    QValidator::State validate(QString& text, int& position) const override;
    int valueFromText(const QString& text) const override;
    void fixup(QString& text) const override;
};

class ThemedCheckBox final : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace window_system
