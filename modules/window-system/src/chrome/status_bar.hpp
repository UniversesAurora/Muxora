// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <window_system/custom_chrome.hpp>
#include <QHash>
#include <QLabel>
#include <QPointer>
#include <QStringList>

class QContextMenuEvent;
class QMenu;

namespace window_system {
class WindowManager;

class WorkspaceStatusBar final : public QLabel {
    Q_OBJECT
public:
    WorkspaceStatusBar(WindowManager* manager, WindowId window, QWidget* parent = nullptr);
    static QStringList itemIds();
    void setMessage(const QString& text);
    void setTransientMessage(const QString& text);
    void setWorkspace(const QString& title, int tabCount);
    void setHiddenItems(const QStringList& hidden);
    void setCustomItems(const QVector<StatusItem>& items);
    [[nodiscard]] QMenu* createContextMenu(QWidget* parent = nullptr) const;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updateHeight();
    QPointer<WindowManager> manager_;
    WindowId window_;
    QLabel* message_;
    QLabel* workspace_;
    QHash<QString, QPointer<QWidget>> customItems_;
    QVector<StatusItem> customDefinitions_;
    QStringList hiddenItems_;
    QString messageText_;
    QString transientMessage_;
};

} // namespace window_system
