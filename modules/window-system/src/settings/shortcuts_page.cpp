// SPDX-License-Identifier: MIT
#include "settings_page.hpp"

#include <QCoreApplication>
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
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(10);
    auto* heading = new QLabel(QCoreApplication::translate("WindowSystem", "键盘快捷键"), this);
    heading->setProperty("role", QStringLiteral("heading"));
    auto font = heading->font();
    font.setPointSizeF(font.pointSizeF() + 4);
    heading->setFont(font);
    layout->addWidget(heading);
    auto* description = new QLabel(QCoreApplication::translate("WindowSystem",
        "每个命令可绑定多组按键。序列支持组合键后接字母、数字或其他按键。"), this);
    description->setWordWrap(true);
    description->setProperty("role", QStringLiteral("hint"));
    layout->addWidget(description);
    auto* searchRow = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("ShortcutSearch"));
    search_->setPlaceholderText(QCoreApplication::translate("WindowSystem", "搜索全部命令、分类、命令 ID 或快捷键…"));
    search_->setClearButtonEnabled(true);
    searchRow->addWidget(search_, 1);
    auto* composite = new QPushButton(QCoreApplication::translate("WindowSystem", "组合命令…"), this);
    composite->setObjectName(QStringLiteral("AddCompositeShortcut"));
    searchRow->addWidget(composite);
    layout->addLayout(searchRow);
    commands_ = new QTreeWidget(this);
    commands_->setObjectName(QStringLiteral("ShortcutCommands"));
    commands_->setColumnCount(3);
    commands_->setHeaderLabels({QCoreApplication::translate("WindowSystem", "命令"),
        QCoreApplication::translate("WindowSystem", "快捷键"), QCoreApplication::translate("WindowSystem", "分类")});
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
    bindings_ = new QTreeWidget(this);
    bindings_->setObjectName(QStringLiteral("ShortcutBindings"));
    bindings_->setColumnCount(2);
    bindings_->setHeaderLabels({QCoreApplication::translate("WindowSystem", "快捷键"),
        QCoreApplication::translate("WindowSystem", "参数")});
    bindings_->setRootIsDecorated(false);
    bindings_->setUniformRowHeights(true);
    bindings_->setMaximumHeight(112);
    bindings_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    bindings_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(bindings_, 1);
    capture_ = new ShortcutCapture(this);
    capture_->setObjectName(QStringLiteral("ShortcutCapture"));
    capture_->setAccessibleName(QCoreApplication::translate("WindowSystem", "录入命令快捷键"));
    layout->addWidget(capture_);
    arguments_ = new QPlainTextEdit(this);
    arguments_->setObjectName(QStringLiteral("ShortcutArguments"));
    arguments_->setAccessibleName(QCoreApplication::translate("WindowSystem", "快捷键参数 JSON"));
    arguments_->setPlaceholderText(QCoreApplication::translate("WindowSystem", "参数（JSON 对象，可留空）"));
    arguments_->setMaximumHeight(100);
    arguments_->setMinimumHeight(54);
    layout->addWidget(arguments_);
    diagnostic_ = new QLabel(this);
    diagnostic_->setWordWrap(true);
    diagnostic_->setProperty("role", QStringLiteral("hint"));
    diagnostic_->setObjectName(QStringLiteral("ShortcutDiagnostic"));
    layout->addWidget(diagnostic_);
    auto* actions = new QHBoxLayout;
    apply_ = new QPushButton(QCoreApplication::translate("WindowSystem", "应用快捷键"), this);
    apply_->setObjectName(QStringLiteral("ApplyShortcut"));
    add_ = new QPushButton(QCoreApplication::translate("WindowSystem", "添加绑定"), this);
    add_->setObjectName(QStringLiteral("AddShortcut"));
    clear_ = new QPushButton(QCoreApplication::translate("WindowSystem", "移除此绑定"), this);
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
    QObject::connect(composite, &QPushButton::clicked, this, [this] {
        search_->setText(QStringLiteral("runCommands"));
        startAdding();
        arguments_->setPlainText(QStringLiteral("{\n  \"commands\": [\n    \"workbench.showCommandPalette\"\n  ]\n}"));
        arguments_->setFocus();
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
    const bool exactCommand = manager_ && manager_->commands().find(query);
    {
        const QSignalBlocker blocker(commands_);
        commands_->clear();
        if (manager_) for (const auto& command : manager_->commands().commands()) {
            const QString shortcut = bindingText(manager_->commands().bindings(command.id));
            const auto category = QCoreApplication::translate("WindowSystem", command.category.toUtf8().constData());
            if (exactCommand && command.id != query) continue;
            if (!matches(titleFor(command) + QLatin1Char(' ') + category + QLatin1Char(' ') +
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
        QCoreApplication::translate("WindowSystem", "没有匹配的命令"));
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
            error = QCoreApplication::translate("WindowSystem", "参数必须是有效的 JSON 对象：%1")
                .arg(diagnostic.error == QJsonParseError::NoError
                    ? QCoreApplication::translate("WindowSystem", "顶层不能是数组或其他类型") : diagnostic.errorString());
            return false;
        }
        args = document.object().toVariantMap();
    }
    if (selectedId_ != QStringLiteral("runCommands")) return true;
    const auto commands = QJsonObject::fromVariantMap(args).value(QStringLiteral("commands"));
    if (!commands.isArray() || commands.toArray().isEmpty()) {
        error = QCoreApplication::translate("WindowSystem", "组合命令需要非空的 commands 数组，按数组顺序执行。");
        return false;
    }
    for (const auto& entry : commands.toArray()) {
        const auto object = entry.toObject();
        const auto id = entry.isString() ? entry.toString() : object.value(QStringLiteral("command")).toString();
        if (id.isEmpty() || !manager_ || !manager_->commands().find(id)) {
            error = QCoreApplication::translate("WindowSystem", "组合命令包含未知或缺失的命令 ID：%1").arg(id);
            return false;
        }
        if (entry.isObject() && object.contains(QStringLiteral("args")) && !object.value(QStringLiteral("args")).isObject()) {
            error = QCoreApplication::translate("WindowSystem", "组合命令中每一步的 args 必须是 JSON 对象。");
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
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "此绑定已在其他位置修改。请重新选择命令后编辑。"));
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
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "此快捷键已绑定到：%1。请先移除其他绑定。")
            .arg(duplicates.join(QStringLiteral("、"))));
    } else if (!prefixes.isEmpty()) {
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "此按键序列与以下命令有前缀关系：%1。")
            .arg(prefixes.join(QStringLiteral("、"))));
    } else if (sequence.isEmpty()) {
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "录入按键后应用；清空现有按键并应用可移除此绑定。"));
    } else {
        diagnostic_->setText(QCoreApplication::translate("WindowSystem", "最多 4 段按键；例如 Ctrl+K、G、1。应用后立即生效。"));
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
