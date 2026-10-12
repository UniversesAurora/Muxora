// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/window_manager.hpp>

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace window_system {

// Built-in window-system preferences remain independent of the host's settings
// service. The host saves the manager snapshot; this page edits the live model.
class SettingsPage final : public QWidget {
public:
    explicit SettingsPage(WindowManager* manager, QWidget* parent = nullptr);

private:
    struct Row {
        QString category;
        QString searchable;
        QPointer<QWidget> widget;
    };
    struct Category {
        QString id;
        QPointer<QWidget> heading;
        QTreeWidgetItem* item = nullptr; // Owned by categories_; never rebuilt.
    };
    void syncControls();
    void filterRows();
    void addCategory(QWidget* parent, QVBoxLayout* rows, const QString& id,
                     const QString& title);
    void scrollToCategory(QTreeWidgetItem* item);
    void syncNavigation();
    void markCurrentCategory(QTreeWidgetItem* item);
    QWidget* addRow(QWidget* parent, const QString& category, const QString& title,
                    const QString& key, const QString& description, QWidget* control);

    QPointer<WindowManager> manager_;
    QLineEdit* search_ = nullptr;
    QTreeWidget* categories_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QLabel* results_ = nullptr;
    QComboBox* detachKind_ = nullptr;
    QComboBox* emptyPolicy_ = nullptr;
    QCheckBox* reveal_ = nullptr;
    QSpinBox* delay_ = nullptr;
    QVector<Row> rows_;
    QVector<Category> sections_;
    bool navigating_ = false;
};

class ShortcutsPage final : public QWidget {
public:
    explicit ShortcutsPage(WindowManager* manager, QWidget* parent = nullptr);

private:
    void rebuild();
    void selectCommand();
    void updateDiagnostic();

    QPointer<WindowManager> manager_;
    QLineEdit* search_ = nullptr;
    QTreeWidget* commands_ = nullptr;
    QLabel* selectedTitle_ = nullptr;
    QKeySequenceEdit* capture_ = nullptr;
    QLabel* diagnostic_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* clear_ = nullptr;
    QString selectedId_;
};

} // namespace window_system
