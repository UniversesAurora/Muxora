// SPDX-License-Identifier: MIT
#pragma once

#include <QWidget>

class QSlider;
class QSpinBox;

namespace window_system {

// Composes Qt value controls without knowing their units or setting semantics.
// The editor owns the full range; the slider may cover a smaller useful range.
class IntegerSlider final : public QWidget {
    Q_OBJECT
public:
    IntegerSlider(QSpinBox* editor, int sliderMinimum, int sliderMaximum, QWidget* parent = nullptr);
    [[nodiscard]] int value() const;
    void setValue(int value);
    [[nodiscard]] QSpinBox* editor() const { return editor_; }
    [[nodiscard]] QSlider* slider() const { return slider_; }
signals:
    void valueChanged(int value);
private:
    QSpinBox* editor_;
    QSlider* slider_;
};

} // namespace window_system
