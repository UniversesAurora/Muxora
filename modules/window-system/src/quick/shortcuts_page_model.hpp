// SPDX-License-Identifier: MIT
#pragma once

#include "settings/shortcut_edit.hpp"
#include "settings_style.hpp"
#include <QPointer>
#include <QVariantList>

namespace window_system { class WindowManager; }

namespace window_system::quick {

class ShortcutsPageModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the Quick workspace")
    Q_PROPERTY(SettingsStyle* style READ style CONSTANT)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandRowsChanged)
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY bindingRowsChanged)
    Q_PROPERTY(int commandIndex READ commandIndex NOTIFY changed)
    Q_PROPERTY(int bindingIndex READ bindingIndex NOTIFY changed)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY changed)
    Q_PROPERTY(QString commandHelp READ commandHelp NOTIFY changed)
    Q_PROPERTY(QString argumentPlaceholder READ argumentPlaceholder NOTIFY changed)
    Q_PROPERTY(QString sequence READ sequence NOTIFY editChanged)
    Q_PROPERTY(QString arguments READ arguments NOTIFY editChanged)
    Q_PROPERTY(bool selected READ selected NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY changed)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY changed)
public:
    explicit ShortcutsPageModel(WindowManager& manager, QObject* parent = nullptr);
    SettingsStyle* style() const { return style_; }
    QVariantList commands() const { return commands_; }
    QVariantList bindings() const { return bindings_; }
    int commandIndex() const { return commandIndex_; }
    int bindingIndex() const { return edit_.binding; }
    QString selectedTitle() const;
    QString commandHelp() const;
    QString argumentPlaceholder() const;
    QString sequence() const { return edit_.sequence.toString(QKeySequence::PortableText); }
    QString arguments() const { return edit_.arguments; }
    bool selected() const { return check_.selected; }
    bool canApply() const { return check_.canApply; }
    bool canRemove() const { return check_.canRemove; }
    QString diagnostic() const { return check_.message; }

    Q_INVOKABLE void search(const QString& query);
    Q_INVOKABLE void selectCommand(int index);
    Q_INVOKABLE void selectBinding(int index);
    Q_INVOKABLE void editSequence(const QString& sequence);
    Q_INVOKABLE void editArguments(const QString& text);
    Q_INVOKABLE void startAdding();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void remove();

signals:
    void changed();
    void editChanged();
    void commandRowsChanged();
    void bindingRowsChanged();

private:
    void refresh();
    void rebuildCommands();
    void loadBinding(int index);
    void updateDiagnostic();
    void commit(QVector<CommandBinding> bindings, int preferred);
    QPointer<WindowManager> manager_;
    SettingsStyle* style_;
    QVariantList commands_, bindings_;
    int commandIndex_ = -1;
    QString query_;
    ShortcutEdit edit_;
    ShortcutEditCheck check_;
    bool dirty_ = false;
    bool syncing_ = false;
    bool committing_ = false;
};

[[nodiscard]] QQuickItem* createShortcutsContent(WindowManager& manager);

} // namespace window_system::quick
