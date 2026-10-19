// SPDX-License-Identifier: MIT
#pragma once

#include "docking/splitter_layout_pass.hpp"
#include <QPointer>
#include <QSplitter>
#include <QVector>
#include <QWidget>

namespace window_system {

// A focus view hides sibling branches in the existing widget tree. It never
// edits the layout model or reparents a live pane; ratios are restored when
// those branches become visible again.
class FocusProjection final {
public:
    void restore()
    {
        const SplitterLayoutPass pass;
        for (const auto& branch : branches_)
            if (branch.widget) branch.widget->setVisible(!branch.hidden);
        for (auto iterator = splitters_.crbegin(); iterator != splitters_.crend(); ++iterator)
            if (iterator->widget) iterator->widget->setSizes(iterator->sizes);
        branches_.clear();
        splitters_.clear();
    }

    void apply(QWidget* target, QWidget* boundary)
    {
        restore();
        if (!target || !boundary || !boundary->isAncestorOf(target)) return;
        const SplitterLayoutPass pass;
        for (auto* branch = target; branch && branch != boundary; branch = branch->parentWidget()) {
            auto* splitter = qobject_cast<QSplitter*>(branch->parentWidget());
            if (!splitter) continue;
            splitters_.append({splitter, splitter->sizes()});
            for (int index = 0; index < splitter->count(); ++index) {
                auto* sibling = splitter->widget(index);
                if (sibling == branch) continue;
                branches_.append({sibling, sibling->isHidden()});
                sibling->hide();
            }
        }
    }

private:
    struct Branch { QPointer<QWidget> widget; bool hidden = false; };
    struct Splitter { QPointer<QSplitter> widget; QList<int> sizes; };
    QVector<Branch> branches_;
    QVector<Splitter> splitters_;
};

} // namespace window_system
