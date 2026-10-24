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
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace window_system {

class IntegerSlider;

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
    void addViewOptions(QWidget* parent, QVBoxLayout* rows);
    void syncViewOptions();
    void addTabAppearanceOptions(QWidget* parent, QVBoxLayout* rows);
    void syncTabAppearanceOptions();
    [[nodiscard]] WindowId settingsWindow() const;
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
    QComboBox* language_ = nullptr;
    QComboBox* detachKind_ = nullptr;
    QComboBox* newTabPosition_ = nullptr;
    QComboBox* emptyPolicy_ = nullptr;
    QCheckBox* reveal_ = nullptr;
    QSpinBox* delay_ = nullptr;
    QComboBox* zenFullscreenMode_ = nullptr;
    QCheckBox* hideSingleWorkbench_ = nullptr;
    QCheckBox* hideSingleFloating_ = nullptr;
    QCheckBox* hideToolbarOptionsWhenLocked_ = nullptr;
    QComboBox* captionButtonOrder_ = nullptr;
    QComboBox* centralPlacement_ = nullptr;
    QComboBox* pinnedPlacement_ = nullptr;
    QCheckBox* multiline_ = nullptr;
    QCheckBox* wheelExpandRows_ = nullptr;
    QCheckBox* showIcons_ = nullptr;
    QCheckBox* showNewTabButton_ = nullptr;
    QCheckBox* showNumbers_ = nullptr;
    QCheckBox* showSidebarIcons_ = nullptr;
    QCheckBox* showSidebarNumbers_ = nullptr;
    QCheckBox* clipTitles_ = nullptr;
    IntegerSlider* maximumTitleWidth_ = nullptr;
    QCheckBox* clipSidebarTitles_ = nullptr;
    IntegerSlider* maximumSidebarTitleWidth_ = nullptr;
    QPlainTextEdit* tabColors_ = nullptr;
    QLabel* tabColorDiagnostic_ = nullptr;
    bool tabColorsDirty_ = false;
    QComboBox* stickySizing_ = nullptr;
    IntegerSlider* shrinkStickyTabWidth_ = nullptr;
    QCheckBox* stickyTabsOnSeparateRow_ = nullptr;
    QComboBox* preventStickyClose_ = nullptr;
    WindowId initialWindow_;
    struct VisibilityControl { WindowBar bar; QCheckBox* control; };
    QVector<VisibilityControl> visibilityControls_;
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
    void rebuildBindings(int preferred = 0);
    void selectBinding();
    void startAdding();
    void updateDiagnostic();
    [[nodiscard]] bool readArguments(QVariantMap& args, QString& error) const;
    void applyBinding();
    void removeBinding();

    QPointer<WindowManager> manager_;
    QLineEdit* search_ = nullptr;
    QTreeWidget* commands_ = nullptr;
    QTreeWidget* bindings_ = nullptr;
    QLabel* selectedTitle_ = nullptr;
    QLabel* commandHelp_ = nullptr;
    QKeySequenceEdit* searchCapture_ = nullptr;
    QKeySequenceEdit* capture_ = nullptr;
    QPlainTextEdit* arguments_ = nullptr;
    QLabel* diagnostic_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* clear_ = nullptr;
    QString selectedId_;
    int selectedBinding_ = -1;
    CommandBinding originalBinding_;
    bool dirty_ = false;
    bool refreshing_ = false;
};

} // namespace window_system
