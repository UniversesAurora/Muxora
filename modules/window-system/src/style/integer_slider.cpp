// SPDX-License-Identifier: MIT
#include "integer_slider.hpp"
#include "ui_metrics.hpp"

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

namespace window_system {

IntegerSlider::IntegerSlider(QSpinBox* editor, int sliderMinimum, int sliderMaximum, QWidget* parent)
    : QWidget(parent), editor_(editor), slider_(new QSlider(Qt::Horizontal, this))
{
    Q_ASSERT(editor_);
    Q_ASSERT(sliderMinimum >= editor_->minimum() && sliderMaximum <= editor_->maximum());
    Q_ASSERT(sliderMinimum < sliderMaximum);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    uiSpacing(row, 8);
    slider_->setProperty("windowSystemIntegerSlider", true);
    slider_->setRange(sliderMinimum, sliderMaximum);
    slider_->setSingleStep(editor_->singleStep());
    slider_->setPageStep(int(qMax<qint64>(editor_->singleStep(), (qint64(sliderMaximum) - sliderMinimum) / 20)));
    uiMinimumWidth(slider_, 180);
    slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editor_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    row->addWidget(slider_, 1);
    row->addWidget(editor_);
    setFocusProxy(editor_);
    slider_->setValue(qBound(sliderMinimum, editor_->value(), sliderMaximum));
    connect(slider_, &QSlider::valueChanged, editor_, &QSpinBox::setValue);
    connect(editor_, &QSpinBox::valueChanged, this, [this](int value) {
        {
            const QSignalBlocker quiet(slider_);
            slider_->setValue(qBound(slider_->minimum(), value, slider_->maximum()));
        }
        emit valueChanged(value);
    });
}

int IntegerSlider::value() const { return editor_->value(); }
void IntegerSlider::setValue(int value) { editor_->setValue(value); }

} // namespace window_system
