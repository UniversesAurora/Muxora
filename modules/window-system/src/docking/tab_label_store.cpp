// SPDX-License-Identifier: MIT
#include "tab_label_store.hpp"
#include <QFontMetrics>
#include <utility>

namespace window_system {

int TabLabelStore::append(const QIcon& icon, const QString& title)
{
    TabLabelRecord record; record.icon = icon; record.title = title;
    records_.append(std::move(record)); return int(records_.size()) - 1;
}

int TabLabelStore::indexOf(const QUuid& id) const
{
    for (int index = 0; index < records_.size(); ++index) if (records_.at(index).data.toUuid() == id) return index;
    return -1;
}

QVariant TabLabelStore::data(int index) const { return contains(index) ? records_.at(index).data : QVariant{}; }
bool TabLabelStore::setData(int index, const QVariant& value)
{
    if (!contains(index) || records_.at(index).data == value) return false;
    records_[index].data = value; return true;
}
QString TabLabelStore::title(int index) const { return contains(index) ? records_.at(index).title : QString{}; }
bool TabLabelStore::setTitle(int index, const QString& value)
{
    if (!contains(index) || records_.at(index).title == value) return false;
    auto& record = records_[index]; record.title = value; record.titleWidth = -1; record.content.reset(); return true;
}
QIcon TabLabelStore::icon(int index) const { return contains(index) ? records_.at(index).icon : QIcon{}; }
bool TabLabelStore::setIcon(int index, const QIcon& value)
{
    if (!contains(index)) return false;
    auto& record = records_[index]; record.icon = value; record.content.reset(); return true;
}
QString TabLabelStore::toolTip(int index) const { return contains(index) ? records_.at(index).tooltip : QString{}; }
void TabLabelStore::setToolTip(int index, const QString& value) { if (contains(index)) records_[index].tooltip = value; }
bool TabLabelStore::setColor(int index, const QString& value)
{
    if (!contains(index)) return false;
    auto& record = records_[index];
    if (record.colorId != value) record.content.reset();
    record.colorId = value; return true;
}

void TabLabelStore::measureTitles(const QFont& font, Qt::LayoutDirection direction, const QPaintDevice* device, bool all)
{
    for (auto& record : records_) if (all || record.titleWidth < 0) {
        record.titleLayout.prepare(record.title, font, direction, true, device);
        record.titleWidth = record.titleLayout.naturalWidth();
    }
}

std::shared_ptr<const TabLabelContent> TabLabelStore::createContent(int index, std::shared_ptr<const TabStripLabelStyle> style) const
{
    const auto& record = records_.at(index);
    auto content = std::make_shared<TabLabelContent>();
    content->style = std::move(style); content->title = record.title;
    if (content->style->showNumbers) {
        content->number = QString::number(index + 1);
        content->numberExtent = QFontMetrics(content->style->font).horizontalAdvance(content->number);
    }
    if (content->style->showIcons) content->icon = record.icon;
    if (!record.colorId.isEmpty())
        for (const auto& preset : content->style->colors) if (preset.id == record.colorId) { content->preset = preset; break; }
    record.content = std::move(content); record.contentIndex = index;
    return record.content;
}

} // namespace window_system
