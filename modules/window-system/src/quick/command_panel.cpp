// SPDX-License-Identifier: MIT
#include "command_panel.hpp"
#include "qml_scene_component.hpp"
#include "tab_icon_item.hpp"
#include <window_system/ui_scale.hpp>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <algorithm>

namespace window_system::quick {

CommandPanel::CommandPanel(QWindow* owner, const Theme& theme, const FloatingPanelOptions& options, Setup setup)
    : setup_(std::move(setup)), theme_(theme), options_(options), font_(QGuiApplication::font())
{
    auto* window = new QQuickWindow;
    window->QObject::setParent(this); // Owned, not a native child window.
    window_ = window;
    window->setObjectName(setup_.name);
    window->setTitle(setup_.title);
    window->setFlags(Qt::Dialog | Qt::FramelessWindowHint);
    window->setTransientParent(owner);
    window->setModality(options_.modal ? Qt::WindowModal : Qt::NonModal);
    window->setColor(theme_.surface);
    auto* content = qobject_cast<QQuickItem*>(sceneComponent(SceneComponent::CommandPanel)
        .createWithInitialProperties({{"panel", QVariant::fromValue(this)}}));
    Q_ASSERT(content);
    QQmlEngine::setObjectOwnership(content, QQmlEngine::CppOwnership);
    content->setParent(window);
    content->setParentItem(window->contentItem());
    connect(window->contentItem(), &QQuickItem::widthChanged, content, [content, window] { content->setWidth(window->width()); });
    connect(window->contentItem(), &QQuickItem::heightChanged, content, [content, window] { content->setHeight(window->height()); });
    // Centred on its owner, within the screen it sits on.
    const QPoint anchor = owner ? owner->geometry().center() : QPoint();
    auto* screen = owner ? QGuiApplication::screenAt(anchor) : QGuiApplication::primaryScreen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(QPoint(), setup_.size);
    const QSize requested(uiPixels(setup_.size.width()), uiPixels(setup_.size.height()));
    const QSize size(std::min(requested.width(), available.width()), std::min(requested.height(), available.height()));
    QPoint position = (owner ? anchor : available.center()) - QPoint(size.width() / 2, size.height() / 2);
    position.setX(std::clamp(position.x(), available.left(), available.right() - size.width() + 1));
    position.setY(std::clamp(position.y(), available.top(), available.bottom() - size.height() + 1));
    window->setGeometry(QRect(position, size));
    content->setSize(size);
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) { if (!visible) close(); });
    if (owner) connect(owner, &QObject::destroyed, this, &CommandPanel::reject);
    refresh();
    window->installEventFilter(this);
    window->show();
    window->requestActivate();
    // Typing starts in the search field, as in the Widgets panel.
    if (auto* search = content->findChild<QQuickItem*>(QStringLiteral("PanelSearch"))) search->forceActiveFocus(Qt::PopupFocusReason);
    if (options_.dismissOnOutsidePress) qApp->installEventFilter(this);
}

CommandPanel::~CommandPanel()
{
    if (qApp) qApp->removeEventFilter(this);
    // The view goes while the panel it reads is still whole.
    delete window_.data();
}

QQuickWindow* CommandPanel::window() const { return window_; }

void CommandPanel::refresh()
{
    const QString previous = current_ >= 0 && current_ < rows_.size() ? rows_.at(current_).id : setup_.initial;
    rows_ = setup_.rows ? setup_.rows(query_) : QVector<PanelRow>{};
    rowValues_.clear();
    const QFontMetrics metrics(font_);
    const int padding = uiPixels(12);
    QVector<int> widths(setup_.columns.size(), 0);
    for (int column = 1; column < setup_.columns.size(); ++column)
        widths[column] = metrics.horizontalAdvance(setup_.columns.at(column)) + padding;
    for (const auto& row : std::as_const(rows_)) {
        rowValues_.append(QVariantMap{{"id", row.id}, {"cells", row.cells}, {"tooltip", row.tooltip}, {"enabled", row.enabled}, {"icon", row.iconName}});
        for (int column = 1; column < std::min<qsizetype>(row.cells.size(), widths.size()); ++column)
            widths[column] = std::max(widths.at(column), metrics.horizontalAdvance(row.cells.at(column)) + padding);
    }
    // The first column takes what the others leave.
    columnWidths_.clear();
    for (const int width : std::as_const(widths)) columnWidths_.append(width);
    emit rowsChanged();
    int index = -1;
    for (int row = 0; row < rows_.size(); ++row) if (rows_.at(row).id == previous && rows_.at(row).enabled) index = row;
    if (index < 0) for (int row = 0; row < rows_.size() && index < 0; ++row) if (rows_.at(row).enabled) index = row;
    current_ = -2; // Force the notification for a list that changed.
    setCurrent(index);
}

bool CommandPanel::canAccept() const
{
    return current_ >= 0 && current_ < rows_.size() && rows_.at(current_).enabled
        && (!setup_.acceptable || setup_.acceptable(rows_.at(current_)));
}

QString CommandPanel::hint() const
{
    if (!setup_.hint) return {};
    return setup_.hint(current_ >= 0 && current_ < rows_.size() ? &rows_.at(current_) : nullptr);
}

qreal CommandPanel::unit() const { return uiPixelsF(1); }
qreal CommandPanel::rowHeight() const { return QFontMetrics(font_).height() + uiPixels(6); }
qreal CommandPanel::controlHeight() const { return std::max(uiPixels(std::max(14, theme_.metrics.controlHeight - 6)), QFontMetrics(font_).height()) + uiPixels(6); }

QVariantMap CommandPanel::colors() const
{
    return {{"surface", theme_.surface}, {"raised", theme_.raisedSurface}, {"text", theme_.text}, {"muted", theme_.mutedText},
        {"border", theme_.border}, {"accent", theme_.accent}, {"focus", theme_.focus}, {"selection", theme_.selection},
        {"selectionText", theme_.selectionText}, {"buttonHover", theme_.buttonHover}};
}

void CommandPanel::search(const QString& query)
{
    if (query_ == query) return;
    query_ = query;
    refresh();
}

void CommandPanel::move(int step)
{
    if (rows_.isEmpty() || !step) return;
    const int direction = step > 0 ? 1 : -1;
    int row = current_ >= 0 ? current_ : (step > 0 ? -1 : int(rows_.size()));
    int remaining = std::abs(step), selected = -1;
    while (row + direction >= 0 && row + direction < rows_.size()) {
        row += direction;
        if (!rows_.at(row).enabled) continue;
        selected = row;
        if (--remaining == 0) break;
    }
    if (selected >= 0) setCurrent(selected);
}

void CommandPanel::select(int index)
{
    if (index >= 0 && index < rows_.size() && rows_.at(index).enabled) setCurrent(index);
}

void CommandPanel::setCurrent(int index)
{
    if (current_ == index) return;
    current_ = index;
    emit currentChanged();
}

void CommandPanel::accept()
{
    if (!canAccept()) return;
    const auto id = rows_.at(current_).id;
    const auto accepted = setup_.accepted;
    closing_ = true;
    if (qApp) qApp->removeEventFilter(this);
    // The choice runs after this input returns, and may close the owner;
    // the panel goes after it. Hiding the active panel first would hand
    // the activation back to its owner, ahead of a window the choice shows.
    QMetaObject::invokeMethod(qApp, [accepted, id, self = QPointer<CommandPanel>(this)] {
        if (accepted) accepted(id);
        if (!self) return;
        if (self->window_) self->window_->hide();
        self->deleteLater();
    }, Qt::QueuedConnection);
}

void CommandPanel::reject() { close(); }

void CommandPanel::close()
{
    if (std::exchange(closing_, true)) return;
    if (qApp) qApp->removeEventFilter(this);
    if (window_) window_->hide();
    deleteLater();
}

bool CommandPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (closing_ || !window_ || !window_->isVisible()) return false;
    // Esc closes and the list keys move the selection from any control of
    // the panel, as the Widgets search field forwards them.
    if (watched == window_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) { reject(); return true; }
        if ((key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            const int step = key->key() == Qt::Key_Down ? 1 : key->key() == Qt::Key_Up ? -1
                : key->key() == Qt::Key_PageDown ? 8 : key->key() == Qt::Key_PageUp ? -8 : 0;
            if (step) { move(step); return true; }
        }
    }
    if (!options_.dismissOnOutsidePress || watched == window_) return false;
    if (event->type() == QEvent::ApplicationDeactivate) reject();
    else if (event->type() == QEvent::MouseButtonPress && qobject_cast<QWindow*>(watched) && watched != window_) {
        // The press still reaches its own target; only the panel goes.
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (!window_->geometry().contains(mouse->globalPosition().toPoint())) reject();
    }
    return false;
}

PanelIcon::PanelIcon(QQuickItem* parent) : QQuickItem(parent), icon_(new TabIconItem(this)) {}

void PanelIcon::setPanel(CommandPanel* panel) { panel_ = panel; refresh(); }
void PanelIcon::setName(const QString& name) { name_ = name; refresh(); }
void PanelIcon::setSelected(bool selected) { selected_ = selected; refresh(); }

void PanelIcon::refresh()
{
    if (!panel_ || name_.isEmpty()) { icon_->clearIcon(); return; }
    const auto& theme = panel_->theme();
    const int extent = uiPixels(16);
    icon_->setIcon(iconFor(name_, theme), {extent, extent}, selected_ ? theme.selectionText : QColor(), true, false);
    setImplicitSize(extent, extent);
}

} // namespace window_system::quick
