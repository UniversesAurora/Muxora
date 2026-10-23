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
class ThemedComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;
    void showPopup() override;
protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void alignPopup();
    QPointer<QWidget> popup_;
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
