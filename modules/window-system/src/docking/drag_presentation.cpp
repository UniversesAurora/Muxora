// SPDX-License-Identifier: MIT
#include "drag_presentation.hpp"
#include "edge_tab_button.hpp"
#include "peek_metrics.hpp"
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace window_system {
namespace {
class PeekTitleLabel final : public QLabel {
public:
    PeekTitleLabel(const QString& title, QWidget* parent) : QLabel(title, parent) { updatePeekHeaderHeight(this); }
protected:
    bool event(QEvent* event) override {
        const bool handled = QLabel::event(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            updatePeekHeaderHeight(this);
        return handled;
    }
};
} // namespace
EdgeDragPresentation::EdgeDragPresentation(QString title, Edge edge, const Theme& theme, QWidget* content, QWidget* parent)
    : QFrame(parent), label_(new EdgeTabButton(title, edge == Edge::Left || edge == Edge::Right, theme.metrics.edgeThickness, this)), panel_(new QFrame(this))
{
    setObjectName("EdgeDragPresentation"); setProperty("edge", int(edge));
    label_->setObjectName("DragEdgeLabel"); label_->setFocusPolicy(Qt::NoFocus);
    label_->setAttribute(Qt::WA_TransparentForMouseEvents); label_->setFixedSize(label_->sizeHint());
    panel_->setObjectName("DragEdgeContent");
    auto* panelLayout = new QVBoxLayout(panel_);
    panelLayout->setContentsMargins(1, 1, 1, 1); panelLayout->setSpacing(0);
    auto* header = new PeekTitleLabel(title, panel_); header->setObjectName("DragPeekTitle");
    header->setContentsMargins(6, 0, 4, 0);
    panelLayout->addWidget(header); panelLayout->addWidget(content, 1);
    auto* grid = new QGridLayout(this); grid->setContentsMargins(0, 0, 0, 0); grid->setSpacing(0);
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const bool trailing = edge == Edge::Right || edge == Edge::Bottom;
    if (vertical) {
        grid->addWidget(label_, 0, trailing ? 1 : 0, Qt::AlignTop);
        grid->addWidget(panel_, 0, trailing ? 0 : 1);
        grid->setColumnStretch(trailing ? 0 : 1, 1);
    } else {
        grid->addWidget(label_, trailing ? 1 : 0, 0, Qt::AlignLeft);
        grid->addWidget(panel_, trailing ? 0 : 1, 0);
        grid->setRowStretch(trailing ? 0 : 1, 1);
    }
}
QRegion EdgeDragPresentation::visibleRegion() const
{
    return QRegion(label_->geometry()).united(QRegion(panel_->geometry()));
}
QPoint EdgeDragPresentation::labelAnchor() const
{
    return label_->geometry().center();
}
} // namespace window_system
