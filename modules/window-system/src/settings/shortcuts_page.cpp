#include "style/ui_metrics.hpp"
// SPDX-License-Identifier: MIT
#include "settings_page.hpp"

#include <QCoreApplication>
#include "localization/display_names.hpp"
#include <QFocusEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace window_system {
namespace {

QString titleFor(const Command& command)
{
    return command.title.isEmpty() ? command.id : command.title;
}

QString bindingText(const QVector<CommandBinding>& bindings)
{
    QStringList text;
    for (const auto& binding : bindings)
        text.append(binding.sequence.toString(QKeySequence::NativeText));
    return text.join(QStringLiteral(" / "));
}

bool matches(const QString& haystack, const QString& query)
{
    const auto folded = haystack.toCaseFolded();
    const auto terms = query.simplified().toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(terms.cbegin(), terms.cend(), [&folded](const QString& term) {
        return folded.contains(term);
    });
}

QString argumentText(const QVariantMap& args, QJsonDocument::JsonFormat format)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(args)).toJson(format)).trimmed();
}

// QKeySequenceEdit remains the public widget type, so host shortcut routing
// recognizes this as a capture control. Every stroke is recorded explicitly:
// unmodified continuation letters/digits never become editable text instead.
class ShortcutCapture final : public QKeySequenceEdit {
public:
    explicit ShortcutCapture(QWidget* parent) : QKeySequenceEdit(parent)
    {
        setMaximumSequenceLength(4);
        setFinishingKeyCombinations({});
        timeout_.setSingleShot(true);
        QObject::connect(&timeout_, &QTimer::timeout, this, [this] { recording_ = false; });
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
        return QKeySequenceEdit::event(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        switch (event->key()) {
        case Qt::Key_Control: case Qt::Key_Shift: case Qt::Key_Alt: case Qt::Key_Meta:
        case Qt::Key_AltGr: case Qt::Key_unknown:
            event->accept();
            return;
        default: break;
        }
        if (event->isAutoRepeat()) { event->accept(); return; }
        if (!recording_ && event->modifiers() == Qt::NoModifier &&
            (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)) {
            clear();
            event->accept();
            return;
        }
        QVector<QKeyCombination> strokes;
        if (recording_) for (int index = 0; index < keySequence().count(); ++index)
            strokes.append(keySequence()[index]);
        if (strokes.size() == maximumSequenceLength()) strokes.clear();
        strokes.append(event->keyCombination());
        const auto at = [&strokes](int index) {
            return index < strokes.size() ? strokes[index] : QKeyCombination::fromCombined(0);
        };
        setKeySequence(QKeySequence(at(0), at(1), at(2), at(3)));
        recording_ = strokes.size() < maximumSequenceLength();
        timeout_.start(1500);
        event->accept();
    }

    void keyReleaseEvent(QKeyEvent* event) override { event->accept(); }

    void focusInEvent(QFocusEvent* event) override
    {
        recording_ = false;
        timeout_.stop();
        QKeySequenceEdit::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        recording_ = false;
        timeout_.stop();
        QKeySequenceEdit::focusOutEvent(event);
    }

private:
    QTimer timeout_;
    bool recording_ = false;
};

} // namespace

ShortcutsPage::ShortcutsPage(WindowManager* manager, QWidget* parent)
    : QWidget(parent), manager_(manager)
{
    setObjectName(QStringLiteral("ShortcutsPage"));
    setProperty("windowSystemSettingsKind", QStringLiteral("window_system.shortcuts"));
    auto* layout = new QVBoxLayout(this);
    uiMargins(layout, 24, 16, 24, 16);
    uiSpacing(layout, 10);
    auto* heading = new QLabel(QCoreApplication::translate("WindowSystem", "Keyboard shortcuts"), this);
    heading->setProperty("role", QStringLiteral("heading"));
    auto font = heading->font();
    heading->setFont(font);
    uiFontDelta(heading, 4 * 96.0 / 72.0);
    layout->addWidget(heading);
    auto* description = new QLabel(QCoreApplication::translate("WindowSystem",
        "Each command can have multiple bindings. A sequence may start with a combination and continue with letters, numbers, or other keys."), this);
    description->setWordWrap(true);
    description->setProperty("role", QStringLiteral("hint"));
    layout->addWidget(description);
    auto* searchRow = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("ShortcutSearch"));
    search_->setPlaceholderText(QCoreApplication::translate("WindowSystem", "Search commands, categories, command IDs, or shortcuts…"));
    search_->setClearButtonEnabled(true);
    searchRow->addWidget(search_, 1);
    auto* recordSearch = new QPushButton(QCoreApplication::translate("WindowSystem", "Record keys to search"), this);
    recordSearch->setObjectName(QStringLiteral("RecordShortcutSearch"));
    recordSearch->setCheckable(true);
    recordSearch->setToolTip(QCoreApplication::translate("WindowSystem", "Record keys to find bindings. Commands do not run while recording."));
    searchRow->addWidget(recordSearch);
    layout->addLayout(searchRow);
    searchCapture_ = new ShortcutCapture(this);
    searchCapture_->setObjectName(QStringLiteral("ShortcutSearchCapture"));
    searchCapture_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Record shortcut to search"));
    searchCapture_->hide();
    layout->addWidget(searchCapture_);
    commands_ = new QTreeWidget(this);
    commands_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    commands_->setObjectName(QStringLiteral("ShortcutCommands"));
    commands_->setColumnCount(3);
    commands_->setHeaderLabels({QCoreApplication::translate("WindowSystem", "Command"),
        QCoreApplication::translate("WindowSystem", "Shortcut"), QCoreApplication::translate("WindowSystem", "Category")});
    commands_->setRootIsDecorated(false);
    commands_->setUniformRowHeights(true);
    commands_->setAlternatingRowColors(true);
    commands_->setSelectionMode(QAbstractItemView::SingleSelection);
    commands_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    commands_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    commands_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(commands_, 2);
    selectedTitle_ = new QLabel(this);
    selectedTitle_->setWordWrap(true);
    layout->addWidget(selectedTitle_);
    commandHelp_ = new QLabel(this);
    commandHelp_->setObjectName(QStringLiteral("ShortcutCommandHelp"));
    commandHelp_->setWordWrap(true);
    commandHelp_->setProperty("role", QStringLiteral("hint"));
    commandHelp_->hide();
    layout->addWidget(commandHelp_);
    bindings_ = new QTreeWidget(this);
    bindings_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    bindings_->setObjectName(QStringLiteral("ShortcutBindings"));
    bindings_->setColumnCount(2);
    bindings_->setHeaderLabels({QCoreApplication::translate("WindowSystem", "Shortcut"),
        QCoreApplication::translate("WindowSystem", "Arguments")});
    bindings_->setRootIsDecorated(false);
    bindings_->setUniformRowHeights(true);
    bindings_->setMaximumHeight(112);
    bindings_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    bindings_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(bindings_, 1);
    capture_ = new ShortcutCapture(this);
    capture_->setObjectName(QStringLiteral("ShortcutCapture"));
    capture_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Record a command shortcut"));
    layout->addWidget(capture_);
    arguments_ = new QPlainTextEdit(this);
    arguments_->setObjectName(QStringLiteral("ShortcutArguments"));
    arguments_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Shortcut arguments JSON"));
    arguments_->setPlaceholderText(QCoreApplication::translate("WindowSystem", "Arguments (JSON object, optional)"));
    arguments_->setMaximumHeight(100);
    uiMinimumHeight(arguments_, 54);
    layout->addWidget(arguments_);
    diagnostic_ = new QLabel(this);
    diagnostic_->setWordWrap(true);
    diagnostic_->setProperty("role", QStringLiteral("hint"));
    diagnostic_->setObjectName(QStringLiteral("ShortcutDiagnostic"));
    layout->addWidget(diagnostic_);
    auto* actions = new QHBoxLayout;
    apply_ = new QPushButton(QCoreApplication::translate("WindowSystem", "Apply shortcut"), this);
    apply_->setObjectName(QStringLiteral("ApplyShortcut"));
    add_ = new QPushButton(QCoreApplication::translate("WindowSystem", "Add binding"), this);
    add_->setObjectName(QStringLiteral("AddShortcut"));
    clear_ = new QPushButton(QCoreApplication::translate("WindowSystem", "Remove this binding"), this);
    clear_->setObjectName(QStringLiteral("ClearShortcut"));
    actions->addWidget(apply_);
    actions->addWidget(add_);
    actions->addWidget(clear_);
    actions->addStretch();
    layout->addLayout(actions);
    QObject::connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    QObject::connect(commands_, &QTreeWidget::currentItemChanged, this, [this] { selectCommand(); });
    QObject::connect(commands_, &QTreeWidget::itemActivated, this, [this] { capture_->setFocus(); });
    QObject::connect(bindings_, &QTreeWidget::currentItemChanged, this, [this] { selectBinding(); });
    QObject::connect(capture_, &QKeySequenceEdit::keySequenceChanged, this, [this] { dirty_ = true; updateDiagnostic(); });
    QObject::connect(arguments_, &QPlainTextEdit::textChanged, this, [this] { dirty_ = true; updateDiagnostic(); });
    QObject::connect(apply_, &QPushButton::clicked, this, [this] { applyBinding(); });
    QObject::connect(add_, &QPushButton::clicked, this, [this] { startAdding(); });
    QObject::connect(clear_, &QPushButton::clicked, this, [this] { removeBinding(); });
    QObject::connect(recordSearch, &QPushButton::toggled, this, [this](bool recording) {
        searchCapture_->setVisible(recording);
        if (recording) {
            searchCapture_->clear();
            searchCapture_->setFocus();
        } else {
            search_->setFocus();
        }
    });
    QObject::connect(searchCapture_, &QKeySequenceEdit::keySequenceChanged, this, [this](const QKeySequence& sequence) {
        if (sequence.isEmpty()) return;
        // Quotes distinguish a recorded key sequence from ordinary words.
        // Compare strokes, not substrings (Ctrl+K must not match Ctrl+Shift+K).
        search_->setText(QLatin1Char('"') + sequence.toString(QKeySequence::NativeText) + QLatin1Char('"'));
    });
    QObject::connect(manager, &WindowManager::commandsChanged, this, [this] {
        if (refreshing_) return;
        if (!dirty_) { rebuild(); return; }
        // Keep an unfinished capture/JSON edit intact if another settings page
        // updates the registry. Applying verifies the original binding again.
        for (int row = 0; row < commands_->topLevelItemCount(); ++row) {
            auto* item = commands_->topLevelItem(row);
            item->setText(1, bindingText(manager_->commands().bindings(item->data(0, Qt::UserRole).toString())));
        }
        updateDiagnostic();
    });
    rebuild();
    setFocusProxy(search_);
}

void ShortcutsPage::rebuild()
{
    const QString previous = selectedId_;
    const QString query = search_->text().trimmed();
    const bool keyQuery = query.size() > 2 && query.startsWith(QLatin1Char('"'))
        && query.endsWith(QLatin1Char('"'));
    const QKeySequence recorded = keyQuery ? QKeySequence::fromString(query.mid(1, query.size() - 2),
        QKeySequence::NativeText) : QKeySequence{};
    const bool exactCommand = manager_ && manager_->commands().find(query);
    {
        const QSignalBlocker blocker(commands_);
        commands_->clear();
        if (manager_) for (const auto& command : manager_->commands().commands()) {
            const auto bindings = manager_->commands().bindings(command.id);
            const QString shortcut = bindingText(bindings);
            const auto category = commandCategoryDisplayName(command.category);
            if (exactCommand && command.id != query) continue;
            if (keyQuery) {
                if (recorded.isEmpty() || std::none_of(bindings.cbegin(), bindings.cend(),
                    [&recorded](const CommandBinding& binding) {
                        return recorded.matches(binding.sequence) != QKeySequence::NoMatch;
                    })) continue;
            } else if (!matches(titleFor(command) + QLatin1Char(' ') + category + QLatin1Char(' ') +
                                command.id + QLatin1Char(' ') + shortcut, query)) continue;
            auto* item = new QTreeWidgetItem(commands_, {titleFor(command), shortcut, category});
            item->setData(0, Qt::UserRole, command.id);
            item->setToolTip(0, command.id);
            if (command.id == previous) commands_->setCurrentItem(item);
        }
        if (!commands_->currentItem() && commands_->topLevelItemCount())
            commands_->setCurrentItem(commands_->topLevelItem(0));
    }
    selectCommand();
}

void ShortcutsPage::selectCommand()
{
    const auto* current = commands_->currentItem();
    selectedId_ = current ? current->data(0, Qt::UserRole).toString() : QString();
    const auto* command = manager_ ? manager_->commands().find(selectedId_) : nullptr;
    selectedTitle_->setText(command ? titleFor(*command) + QStringLiteral("  ·  ") + command->id :
        QCoreApplication::translate("WindowSystem", "No matching commands"));
    const bool composite = selectedId_ == QStringLiteral("runCommands");
    commandHelp_->setVisible(composite);
    commandHelp_->setText(composite ? QCoreApplication::translate("WindowSystem",
        "runCommands is a regular command. Each binding defines its steps in its own args.commands array. Example: {\"commands\":[\"host.newTab\",{\"command\":\"pane.split.right\",\"args\":{}}]}. The structure follows VS Code; each step's args currently accepts JSON objects only.") : QString());
    arguments_->setPlaceholderText(composite ? QStringLiteral("{\n  \"commands\": [\n    \"host.newTab\",\n    {\"command\": \"pane.split.right\", \"args\": {}}\n  ]\n}")
        : QCoreApplication::translate("WindowSystem", "Arguments (JSON object, optional)"));
    rebuildBindings();
}

void ShortcutsPage::rebuildBindings(int preferred)
{
    {
        const QSignalBlocker blocker(bindings_);
        bindings_->clear();
        const auto bindings = manager_ ? manager_->commands().bindings(selectedId_) : QVector<CommandBinding>{};
        for (int index = 0; index < bindings.size(); ++index) {
            const auto& binding = bindings[index];
            auto* item = new QTreeWidgetItem(bindings_, {binding.sequence.toString(QKeySequence::NativeText),
                binding.args.isEmpty() ? QString() : argumentText(binding.args, QJsonDocument::Compact)});
            item->setData(0, Qt::UserRole, index);
        }
        if (!bindings.isEmpty()) bindings_->setCurrentItem(bindings_->topLevelItem(
            std::clamp(preferred, 0, int(bindings.size()) - 1)));
    }
    selectBinding();
}

void ShortcutsPage::selectBinding()
{
    const auto* current = bindings_->currentItem();
    selectedBinding_ = current ? current->data(0, Qt::UserRole).toInt() : -1;
    const auto bindings = manager_ ? manager_->commands().bindings(selectedId_) : QVector<CommandBinding>{};
    const auto binding = selectedBinding_ >= 0 && selectedBinding_ < bindings.size()
        ? bindings[selectedBinding_] : CommandBinding{};
    originalBinding_ = binding;
    dirty_ = false;
    {
        const QSignalBlocker captureBlocker(capture_);
        const QSignalBlocker argsBlocker(arguments_);
        capture_->setKeySequence(binding.sequence);
        arguments_->setPlainText(binding.args.isEmpty() ? QString() : argumentText(binding.args, QJsonDocument::Indented));
    }
    updateDiagnostic();
}

void ShortcutsPage::startAdding()
{
    if (selectedId_.isEmpty()) return;
    {
        const QSignalBlocker blocker(bindings_);
        bindings_->setCurrentItem(nullptr);
        bindings_->clearSelection();
    }
    selectedBinding_ = -1;
    originalBinding_ = {};
    dirty_ = false;
    {
        const QSignalBlocker captureBlocker(capture_);
        const QSignalBlocker argsBlocker(arguments_);
        capture_->clear();
        arguments_->clear();
    }
    updateDiagnostic();
    capture_->setFocus();
}

bool ShortcutsPage::readArguments(QVariantMap& args, QString& error) const
{
    const auto text = arguments_->toPlainText().trimmed();
    if (!text.isEmpty()) {
        QJsonParseError diagnostic;
        const auto document = QJsonDocument::fromJson(text.toUtf8(), &diagnostic);
        if (diagnostic.error != QJsonParseError::NoError || !document.isObject()) {
            error = QCoreApplication::translate("WindowSystem", "Arguments must be a valid JSON object: %1")
                .arg(diagnostic.error == QJsonParseError::NoError
                    ? QCoreApplication::translate("WindowSystem", "The top level must be an object, not an array or another value") : diagnostic.errorString());
            return false;
        }
        args = document.object().toVariantMap();
    }
    if (selectedId_ != QStringLiteral("runCommands")) return true;
    const auto commands = QJsonObject::fromVariantMap(args).value(QStringLiteral("commands"));
    if (!commands.isArray() || commands.toArray().isEmpty()) {
        error = QCoreApplication::translate("WindowSystem", "A command sequence requires a nonempty commands array and runs in order.");
        return false;
    }
    for (const auto& entry : commands.toArray()) {
        const auto object = entry.toObject();
        const auto id = entry.isString() ? entry.toString() : object.value(QStringLiteral("command")).toString();
        if (id.isEmpty() || !manager_ || !manager_->commands().find(id)) {
            error = QCoreApplication::translate("WindowSystem", "The command sequence contains an unknown or missing command ID: %1").arg(id);
            return false;
        }
        if (entry.isObject() && object.contains(QStringLiteral("args")) && !object.value(QStringLiteral("args")).isObject()) {
            error = QCoreApplication::translate("WindowSystem", "Each command step's args must be a JSON object.");
            return false;
        }
    }
    return true;
}

void ShortcutsPage::updateDiagnostic()
{
    const bool selected = manager_ && manager_->commands().find(selectedId_);
    capture_->setEnabled(selected);
    arguments_->setEnabled(selected);
    add_->setEnabled(selected);
    clear_->setEnabled(selected && selectedBinding_ >= 0);
    apply_->setEnabled(selected);
    if (!selected) { diagnostic_->clear(); return; }
    const auto currentBindings = manager_->commands().bindings(selectedId_);
    if (selectedBinding_ >= 0 && (selectedBinding_ >= currentBindings.size() ||
        currentBindings[selectedBinding_] != originalBinding_)) {
        apply_->setEnabled(false);
        clear_->setEnabled(false);
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "This binding changed elsewhere. Select the command again before editing."));
        return;
    }
    QVariantMap args;
    QString error;
    if (!readArguments(args, error)) {
        apply_->setEnabled(false);
        diagnostic_->setText(error);
        return;
    }
    const auto sequence = capture_->keySequence();
    QStringList duplicates, prefixes;
    for (const auto& command : manager_->commands().commands()) {
        const auto bindings = manager_->commands().bindings(command.id);
        for (int index = 0; index < bindings.size(); ++index) {
            const auto& binding = bindings[index];
            if ((command.id == selectedId_ && index == selectedBinding_) || sequence.isEmpty()) continue;
            const auto forward = sequence.matches(binding.sequence);
            const auto backward = binding.sequence.matches(sequence);
            if (forward == QKeySequence::ExactMatch) duplicates.push_back(titleFor(command));
            else if (forward == QKeySequence::PartialMatch || backward == QKeySequence::PartialMatch)
                prefixes.push_back(titleFor(command));
        }
    }
    duplicates.removeDuplicates();
    prefixes.removeDuplicates();
    apply_->setEnabled(duplicates.isEmpty() && (!sequence.isEmpty() || selectedBinding_ >= 0));
    if (!duplicates.isEmpty()) {
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "This shortcut is already bound to: %1. Remove the other binding first.")
            .arg(duplicates.join(QStringLiteral("、"))));
    } else if (!prefixes.isEmpty()) {
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "This key sequence shares a prefix with: %1.")
            .arg(prefixes.join(QStringLiteral("、"))));
    } else if (sequence.isEmpty()) {
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "Record keys and apply. Clear the keys and apply to remove this binding."));
    } else {
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "Up to 4 key strokes, such as Ctrl+K, G, 1. Changes take effect immediately."));
    }
}

void ShortcutsPage::applyBinding()
{
    updateDiagnostic();
    if (!manager_ || !apply_->isEnabled() || selectedId_.isEmpty()) return;
    QVariantMap args;
    QString error;
    if (!readArguments(args, error)) return;
    auto bindings = manager_->commands().bindings(selectedId_);
    const auto index = selectedBinding_;
    if (capture_->keySequence().isEmpty()) {
        if (index >= 0 && index < bindings.size()) bindings.removeAt(index);
    } else if (index >= 0 && index < bindings.size()) {
        bindings[index] = {capture_->keySequence(), args};
    } else {
        bindings.append({capture_->keySequence(), args});
    }
    const QPointer<ShortcutsPage> alive(this);
    refreshing_ = true;
    const bool changed = manager_->commands().setBindings(selectedId_, bindings);
    if (!alive) return;
    refreshing_ = false;
    if (!changed) return;
    rebuild();
    rebuildBindings(index < 0 ? int(bindings.size()) - 1 : index);
}

void ShortcutsPage::removeBinding()
{
    updateDiagnostic();
    if (!manager_ || selectedId_.isEmpty() || selectedBinding_ < 0) return;
    if (!clear_->isEnabled()) return;
    const auto index = selectedBinding_;
    const QPointer<ShortcutsPage> alive(this);
    refreshing_ = true;
    const bool changed = manager_->commands().removeBinding(selectedId_, index);
    if (!alive) return;
    refreshing_ = false;
    if (!changed) return;
    rebuild();
    rebuildBindings(index);
}

} // namespace window_system
