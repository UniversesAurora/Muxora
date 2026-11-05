// SPDX-License-Identifier: MIT
#include "status_bar_item.hpp"
#include "tab_label_text_item.hpp"
#include "tool_tip.hpp"
#include <window_system/ui_scale.hpp>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSGSimpleRectNode>
#include <QTimer>
#include <algorithm>

namespace window_system::quick {

// One text entry of the bar. A command entry is a flat button: the soft
// accent fills it while hovered or focused, as the Widgets style does.
class StatusEntryItem final : public QQuickItem {
public:
    StatusEntryItem(QQuickItem* parent, QString id, bool command)
        : QQuickItem(parent), id_(std::move(id)), command_(command), text_(new TabLabelTextItem(this))
    {
        setFlag(ItemHasContents, command_);
        if (!command_) return;
        setAcceptedMouseButtons(Qt::LeftButton);
        setAcceptHoverEvents(true);
        setActiveFocusOnTab(true);
        setCursor(QCursor(Qt::PointingHandCursor));
    }
    const QString& id() const { return id_; }
    bool command() const { return command_; }
    const QString& text() const { return value_; }
    void set(const QString& text, const QString& tip, const Theme& theme, const QFont& font)
    {
        value_ = text; tip_ = tip; theme_ = theme; font_ = font;
        setProperty("text", value_); // For accessibility and inspection.
        naturalWidth_ = QFontMetrics(font_).horizontalAdvance(value_);
        refresh();
    }
    int naturalWidth() const { return naturalWidth_ + (command_ ? uiPixels(8) : 0); }
    void restyle(const Theme& theme, const QFont& font) { set(value_, tip_, theme, font); }
    std::function<void()> activated;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override
    {
        auto* node = static_cast<QSGSimpleRectNode*>(previous);
        if (!node) node = new QSGSimpleRectNode;
        node->setRect(boundingRect());
        node->setColor(hovered_ || hasActiveFocus() ? theme_.accentSoft : Qt::transparent);
        return node;
    }
    void geometryChange(const QRectF& now, const QRectF& previous) override
    {
        QQuickItem::geometryChange(now, previous);
        if (now.size() != previous.size()) refresh();
    }
    void itemChange(ItemChange change, const ItemChangeData& data) override
    {
        QQuickItem::itemChange(change, data);
        if (change == ItemActiveFocusHasChanged) refresh();
    }
    void hoverEnterEvent(QHoverEvent*) override { hovered_ = true; refresh(); showToolTip(this, theme_, tip_); }
    void hoverLeaveEvent(QHoverEvent*) override { hovered_ = false; refresh(); hideToolTip(this); }
    void mousePressEvent(QMouseEvent* event) override { hideToolTip(this); event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        event->accept();
        if (contains(event->position()) && activated) activated();
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() != Qt::Key_Space && event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
            return QQuickItem::keyPressEvent(event);
        event->accept();
        if (activated) activated();
    }

private:
    void refresh()
    {
        const int inset = command_ ? uiPixels(4) : 0;
        const QRect area(inset, 0, std::max(0, qRound(width()) - 2 * inset), qRound(height()));
        // Only a command entry draws a fill of its own.
        if (command_) update();
        if (value_.isEmpty() || area.isEmpty()) { text_->clearLabel(); return; }
        TabLabelPresentation label;
        label.parts.tab = area; label.parts.title = area;
        label.state.shape = TabShape::Row; label.state.selected = true; label.state.title = value_;
        label.font = font_; label.naturalWidth = naturalWidth_;
        TabStripLabelStyle style;
        style.palette.text = command_ && (hovered_ || hasActiveFocus()) ? theme_.text : theme_.statusText;
        text_->setLabel(label, style);
        text_->setPosition(area.topLeft());
    }
    QString id_;
    bool command_;
    TabLabelTextItem* text_;
    QString value_, tip_;
    Theme theme_;
    QFont font_;
    int naturalWidth_ = 0;
    bool hovered_ = false;
};

StatusBarItem::StatusBarItem(QQuickItem* parent)
    : QQuickItem(parent), font_(QGuiApplication::font()),
      message_(new StatusEntryItem(this, QStringLiteral("message"), false)),
      workspace_(new StatusEntryItem(this, QStringLiteral("workspace"), false))
{
    setFlag(ItemHasContents);
    setObjectName(QStringLiteral("StatusBar"));
    message_->setObjectName(QStringLiteral("StatusMessage"));
    workspace_->setObjectName(QStringLiteral("StatusWorkspace"));
    setMessage(QCoreApplication::translate("WindowSystem", "Ready"), {});
}

QStringList StatusBarItem::builtinItemIds() { return {QStringLiteral("message"), QStringLiteral("workspace")}; }

void StatusBarItem::setTheme(const Theme& theme)
{
    theme_ = theme;
    font_ = QGuiApplication::font();
    // As VS Code's 22 px bar; a larger font still expands it.
    setImplicitHeight(std::max(uiPixels(theme.metrics.statusHeight), QFontMetrics(font_).height() + uiPixels(6)));
    message_->restyle(theme_, font_);
    workspace_->restyle(theme_, font_);
    for (const auto& entry : std::as_const(custom_)) if (entry) entry->restyle(theme_, font_);
    update();
    relayout();
}

void StatusBarItem::setMessage(const QString& message, const QString& transient)
{
    const auto shown = transient.isEmpty() ? message : transient;
    message_->set(shown, shown, theme_, font_);
    relayout();
}

void StatusBarItem::setWorkspace(const QString& title, int tabCount)
{
    const auto text = title.isEmpty() ? QCoreApplication::translate("WindowSystem", "%n tabs", nullptr, tabCount)
        : QCoreApplication::translate("WindowSystem", "%1  ·  %n tabs", nullptr, tabCount).arg(title);
    workspace_->set(text, text, theme_, font_);
    relayout();
}

void StatusBarItem::setItems(const QVector<StatusItem>& items, const QStringList& hidden)
{
    hidden_ = hidden;
    QVector<QPointer<StatusEntryItem>> next;
    for (const auto& item : items) {
        const bool command = !item.command.isEmpty();
        auto found = std::find_if(custom_.begin(), custom_.end(), [&](const auto& entry) {
            return entry && entry->id() == item.id && entry->command() == command;
        });
        QPointer<StatusEntryItem> entry = found != custom_.end() ? *found : nullptr;
        if (found != custom_.end()) custom_.erase(found);
        if (!entry) {
            entry = new StatusEntryItem(this, item.id, command);
            entry->setObjectName(command ? QStringLiteral("CustomStatusCommandItem") : QStringLiteral("CustomStatusItem"));
        }
        // The definition may change before a queued click runs.
        entry->activated = [this, id = item.id, command = item.command] {
            QTimer::singleShot(0, this, [this, id, command] { if (const auto callback = triggered) callback(id, command); });
        };
        entry->set(item.text, item.tooltip, theme_, font_);
        next.append(entry);
    }
    for (const auto& retired : std::as_const(custom_)) delete retired.data();
    custom_ = std::move(next);
    relayout();
}

void StatusBarItem::relayout()
{
    const int margin = uiPixels(10), spacing = uiPixels(10);
    const auto shown = [this](const StatusEntryItem* entry) {
        return !hidden_.contains(entry->id()) && (!entry->command() || !entry->text().isEmpty());
    };
    message_->setVisible(shown(message_));
    workspace_->setVisible(shown(workspace_));
    // Right to left: the workspace information, then the items before it.
    int right = qRound(width()) - margin;
    const auto place = [&](StatusEntryItem* entry, int width) {
        entry->setPosition({qreal(right - width), 0});
        entry->setSize({qreal(width), height()});
        right -= width + spacing;
    };
    if (workspace_->isVisible()) place(workspace_, std::min(workspace_->naturalWidth(), uiPixels(320)));
    for (auto it = custom_.crbegin(); it != custom_.crend(); ++it) if (auto* entry = it->data()) {
        entry->setVisible(shown(entry));
        if (entry->isVisible()) place(entry, entry->naturalWidth());
    }
    message_->setPosition({qreal(margin), 0});
    message_->setSize({qreal(std::max(0, right - margin)), height()});
}

QSGNode* StatusBarItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGSimpleRectNode*>(previous);
    if (!node) node = new QSGSimpleRectNode;
    node->setRect(boundingRect());
    node->setColor(theme_.statusBackground);
    return node;
}

void StatusBarItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) { update(); relayout(); }
}

bool StatusBarItem::event(QEvent* event)
{
    if (event->type() == QEvent::ContextMenu) if (const auto callback = menuRequested) {
        const auto* menu = static_cast<QContextMenuEvent*>(event);
        event->accept();
        callback(menu->reason() == QContextMenuEvent::Keyboard ? mapToGlobal(boundingRect().center()).toPoint() : menu->globalPos());
        return true;
    }
    return QQuickItem::event(event);
}

} // namespace window_system::quick
