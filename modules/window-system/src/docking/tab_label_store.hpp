// SPDX-License-Identifier: MIT
#pragma once

#include "style/tab_label_content.hpp"
#include "style/tab_title_layout.hpp"
#include <QVariant>
#include <QUuid>
#include <QVector>

namespace window_system {

struct TabLabelRecord {
    QVariant data;
    QString title;
    QIcon icon;
    QString tooltip;
    QString colorId;
    TabTitleLayout titleLayout;
    int titleWidth = -1;
private:
    friend class TabLabelStore;
    mutable std::shared_ptr<const TabLabelContent> content;
    mutable int contentIndex = -1;
};

// GUI-side label data and bounded text preparation. A device is borrowed only
// during measurement; neither it nor a source control is retained by the store.
class TabLabelStore final {
public:
    [[nodiscard]] qsizetype size() const { return records_.size(); }
    [[nodiscard]] bool isEmpty() const { return records_.isEmpty(); }
    [[nodiscard]] const TabLabelRecord& at(int index) const { return records_.at(index); }
    [[nodiscard]] TabTitleLayout& textLayout(int index) { return records_[index].titleLayout; }
    int append(const QIcon& icon, const QString& title);
    void removeAt(int index) { records_.removeAt(index); }
    [[nodiscard]] int indexOf(const QUuid& id) const;
    [[nodiscard]] QVariant data(int index) const;
    bool setData(int index, const QVariant& value);
    [[nodiscard]] QString title(int index) const;
    bool setTitle(int index, const QString& value);
    [[nodiscard]] QIcon icon(int index) const;
    bool setIcon(int index, const QIcon& value);
    [[nodiscard]] QString toolTip(int index) const;
    void setToolTip(int index, const QString& value);
    bool setColor(int index, const QString& value);
    void measureTitles(const QFont& font, Qt::LayoutDirection direction, const QPaintDevice* device, bool all = false);
    [[nodiscard]] std::shared_ptr<const TabLabelContent> content(int index, const std::shared_ptr<const TabStripLabelStyle>& style) const
    {
        const auto& record = records_.at(index);
        if (record.content && record.content->style == style && (!style->showNumbers || record.contentIndex == index)) return record.content;
        return createContent(index, style);
    }
private:
    [[nodiscard]] std::shared_ptr<const TabLabelContent> createContent(int index, std::shared_ptr<const TabStripLabelStyle> style) const;
    [[nodiscard]] bool contains(int index) const { return index >= 0 && index < records_.size(); }
    QVector<TabLabelRecord> records_;
};

} // namespace window_system
