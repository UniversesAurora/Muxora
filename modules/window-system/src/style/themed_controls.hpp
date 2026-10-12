// SPDX-License-Identifier: MIT
#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>

namespace window_system {

// Native input, focus and accessibility behavior are retained. Only the small
// indicators are painted here, resolving colors from the nearest Theme root.
class ThemedComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void paintEvent(QPaintEvent* event) override;
};

class ThemedSpinBox final : public QSpinBox {
public:
    explicit ThemedSpinBox(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
};

class ThemedCheckBox final : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace window_system
