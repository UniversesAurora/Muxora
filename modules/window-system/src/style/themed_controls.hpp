// SPDX-License-Identifier: MIT
#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPointer>

namespace window_system {

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
