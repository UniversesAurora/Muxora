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
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;

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
    void syncControls();
    void filterRows();
    QWidget* addRow(QWidget* parent, const QString& category, const QString& title,
                    const QString& key, const QString& description, QWidget* control);

    QPointer<WindowManager> manager_;
    QLineEdit* search_ = nullptr;
    QListWidget* categories_ = nullptr;
    QLabel* results_ = nullptr;
    QComboBox* detachKind_ = nullptr;
    QComboBox* emptyPolicy_ = nullptr;
    QCheckBox* reveal_ = nullptr;
    QSpinBox* delay_ = nullptr;
    QVector<Row> rows_;
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
