#include "command_strip.hpp"
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QMimeData>
#include <QSet>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>

namespace window_system {
namespace {
class StripHandle final : public QToolButton {
public:
    QString id;
    using QToolButton::QToolButton;
protected:
    void mousePressEvent(QMouseEvent* event) override { start_ = event->pos(); QToolButton::mousePressEvent(event); }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!event->buttons().testFlag(Qt::LeftButton) ||
            (event->pos() - start_).manhattanLength() < QApplication::startDragDistance()) return;
        auto* data = new QMimeData;
        data->setData("application/x-window-system-strip", id.toUtf8());
        QDrag drag(this); drag.setMimeData(data); drag.exec(Qt::MoveAction);
    }
private:
    QPoint start_;
};
}
CommandStripHost::CommandStripHost(QWidget* parent) : QFrame(parent)
{
    setObjectName("CommandStripHost"); setAcceptDrops(true);
    auto* column = new QVBoxLayout(this); column->setContentsMargins(0, 0, 0, 0); column->setSpacing(0);
    for (int row = 0; row < 2; ++row) {
        auto* frame = new QWidget(this); frame->setObjectName("CommandStripRow");
        layouts_[row] = new QHBoxLayout(frame);
        layouts_[row]->setContentsMargins(2, 0, 2, 0); layouts_[row]->setSpacing(6);
        layouts_[row]->addStretch(); column->addWidget(frame);
    }
}
void CommandStripHost::addStrip(QString id, QWidget* content, int row)
{
    if (!content || strips_.contains(id)) return;
    auto* strip = new QFrame(this); strip->setObjectName("CommandStrip");
    auto* layout = new QHBoxLayout(strip); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* handle = new StripHandle(strip); handle->id = id; handle->setObjectName("StripHandle");
    handle->setText(QStringLiteral("⋮")); handle->setFixedWidth(14); handle->setVisible(!locked_);
    handle->setToolTip(QStringLiteral("拖动以调整条带位置")); layout->addWidget(handle);
    layout->addWidget(content); strips_.insert(id, strip);
    rows_[qBound(0, row, 1)].append(id); arrange();
}
void CommandStripHost::setLocked(bool locked)
{
    locked_ = locked;
    for (auto* strip : strips_) if (auto* handle = strip->findChild<QToolButton*>("StripHandle")) handle->setVisible(!locked);
}
bool CommandStripHost::isLocked() const { return locked_; }
void CommandStripHost::arrange()
{
    for (int row = 0; row < 2; ++row) {
        while (layouts_[row]->count()) delete layouts_[row]->takeAt(0);
        for (const auto& id : rows_[row]) layouts_[row]->addWidget(strips_.value(id));
        layouts_[row]->addStretch();
        layouts_[row]->parentWidget()->setVisible(!rows_[row].isEmpty());
    }
}
QJsonObject CommandStripHost::saveLayout() const
{
    QJsonArray rows;
    for (int row = 0; row < 2; ++row) { QJsonArray entries; for (const auto& id : rows_[row]) entries.append(id); rows.append(entries); }
    return {{"locked", locked_}, {"rows", rows}};
}
void CommandStripHost::restoreLayout(const QJsonObject& json)
{
    const auto rows = json.value("rows").toArray();
    if (rows.size() != 2) return;
    QVector<QString> proposed[2]; QSet<QString> seen;
    for (int row = 0; row < 2; ++row) for (const auto& entry : rows[row].toArray()) {
        const auto id = entry.toString();
        if (!strips_.contains(id) || seen.contains(id)) return;
        proposed[row].append(id); seen.insert(id);
    }
    if (seen.size() != strips_.size()) return;
    rows_[0] = proposed[0]; rows_[1] = proposed[1]; arrange(); setLocked(json.value("locked").toBool(true));
}
void CommandStripHost::dragEnterEvent(QDragEnterEvent* event)
{
    if (!locked_ && event->mimeData()->hasFormat("application/x-window-system-strip")) event->acceptProposedAction();
}
void CommandStripHost::dragMoveEvent(QDragMoveEvent* event) { if (!locked_) event->acceptProposedAction(); }
void CommandStripHost::dropEvent(QDropEvent* event)
{
    if (locked_) return;
    const auto id = QString::fromUtf8(event->mimeData()->data("application/x-window-system-strip"));
    if (!strips_.contains(id)) return;
    const int row = event->position().y() < height() / 2 ? 0 : 1;
    rows_[0].removeAll(id); rows_[1].removeAll(id);
    int index = 0;
    while (index < rows_[row].size() && event->position().x() > strips_.value(rows_[row][index])->geometry().center().x()) ++index;
    rows_[row].insert(index, id); arrange(); event->acceptProposedAction();
}
} // namespace window_system
