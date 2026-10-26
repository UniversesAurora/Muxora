// SPDX-License-Identifier: MIT
#pragma once

#include "compact_tool_button.hpp"
#include <QContextMenuEvent>
#include <QCursor>
#include <QHBoxLayout>
#include <QStyle>
#include <functional>

namespace window_system {

// Two native buttons with coupled hover and an independent menu press.
// Commands, icons, labels, widths and the popup itself belong to the caller.
class SplitToolButton : public QWidget {
public:
    SplitToolButton(QWidget* parent, QSize primarySize, QSize menuSize, int gap)
        : QWidget(parent), gap_(std::max(0, gap))
    {
        setProperty("windowSystemSplitButton", true);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(gap_);
        primary = new CompactToolButton(this, primarySize);
        menu = new CompactToolButton(this, menuSize);
        primary->setProperty("splitPart", "primary"); menu->setProperty("splitPart", "menu");
        primary->setProperty("splitSide", "left"); menu->setProperty("splitSide", "right");
        for (auto* button : {primary, menu}) { row->addWidget(button); button->installEventFilter(this); }
        setButtonSizes(primarySize, menuSize);
    }

    CompactToolButton* primary = nullptr;
    CompactToolButton* menu = nullptr;
    std::function<void()> menuRequested;

    void setButtonSizes(QSize primarySize, QSize menuSize)
    {
        primary->setTargetSize(primarySize); menu->setTargetSize(menuSize);
        setFixedSize(primarySize.width() + gap_ + menuSize.width(), std::max(primarySize.height(), menuSize.height()));
    }
    void setReversed(bool reversed)
    {
        if (reversed_ == reversed) return;
        reversed_ = reversed;
        static_cast<QHBoxLayout*>(layout())->setDirection(reversed ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight);
        primary->setProperty("splitSide", reversed ? "right" : "left");
        menu->setProperty("splitSide", reversed ? "left" : "right");
        refreshStyle();
    }
    void setPopupOpen(bool open)
    {
        const bool hover = open ? emphasized_ : rect().contains(mapFromGlobal(QCursor::pos()));
        if (popupOpen_ == open && emphasized_ == hover) return;
        popupOpen_ = open; emphasized_ = hover; menu->setDown(open);
        refreshStyle();
    }

protected:
    void enterEvent(QEnterEvent* event) override { setEmphasized(true); QWidget::enterEvent(event); }
    void leaveEvent(QEvent* event) override { setEmphasized(false); QWidget::leaveEvent(event); }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::ContextMenu) {
            const auto callback = menuRequested;
            if (callback) callback();
            return true;
        }
        if (event->type() == QEvent::Enter) setEmphasized(true);
        else if (event->type() == QEvent::Leave) setEmphasized(rect().contains(mapFromGlobal(QCursor::pos())));
        return QWidget::eventFilter(watched, event);
    }

private:
    void setEmphasized(bool emphasized)
    {
        if (emphasized_ == emphasized) return;
        emphasized_ = emphasized; refreshStyle();
    }
    void refreshStyle()
    {
        setProperty("emphasized", emphasized_); setProperty("popupOpen", popupOpen_);
        for (auto* button : {primary, menu}) {
            button->style()->unpolish(button); button->style()->polish(button); button->update();
        }
    }
    int gap_;
    bool reversed_ = false;
    bool emphasized_ = false;
    bool popupOpen_ = false;
};

} // namespace window_system
