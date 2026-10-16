#include <QCoreApplication>
#include "placeholder_content.hpp"
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace muxora {
using namespace window_system;
namespace {
QWidget* informationBar(const QString& text)
{
    auto* bar = new QLabel(text); bar->setObjectName("ContentInfoBar");
    bar->setContentsMargins(8, 3, 8, 3); return bar;
}
}
QWidget* createPlaceholder(const TabOptions& options, WindowManager* windows)
{
    if (options.contentType == "notes") {
        auto* editor = new QPlainTextEdit;
        editor->setObjectName("WorkspaceNotes");
        QFont font("Cascadia Mono", 11); font.setStyleHint(QFont::Monospace); editor->setFont(font);
        editor->setPlainText(QCoreApplication::translate("Muxora",
            "Muxora / 工作区草稿\n\n"
            "这是一块本地编辑区域，可以输入文字并观察移动后状态是否保留。\n"
            "SSH 与真正的终端会在窗口系统确定后接入。\n\n"
            "试试这些操作：\n"
            "  · 拖动上面的标签到目标组的中央或方向指示\n"
            "  · 拖到桌面形成轻量浮窗，再拖标题栏合并回来\n"
            "  · 右键标签，收纳到任意边栏或拆分内部窗格\n\n"
            "Ctrl+Shift+P     命令面板\n"
            "Ctrl+Shift+E     向右拆分窗格\n"
            "Ctrl+Shift+O     向下拆分窗格\n"
            "Ctrl+Alt+方向    标签组导航\n"
            "Alt+方向        内部窗格导航\n"
            "Ctrl+Shift+D     移到轻量浮窗\n"));
        return editor;
    }
    if (options.contentType == "sessions") {
        auto* tree = new QTreeWidget; tree->setHeaderHidden(true); tree->setObjectName("SessionPlaceholder");
        auto* group = new QTreeWidgetItem(tree, {QCoreApplication::translate("Muxora", "会话区域 · 布局预览")});
        for (const auto& text : {QCoreApplication::translate("Muxora", "开发服务器"), QCoreApplication::translate("Muxora", "测试服务器"), QCoreApplication::translate("Muxora", "个人主机")}) {
            auto* item = new QTreeWidgetItem(group, {text}); item->setIcon(0, iconFor("terminal", windows->theme()));
        }
        group->setExpanded(true); tree->setMinimumWidth(160); return tree;
    }
    if (options.contentType == "files") {
        auto* table = new QTableWidget(7, 3); table->setObjectName("FilePlaceholder");
        table->setHorizontalHeaderLabels({QCoreApplication::translate("Muxora", "名称"), QCoreApplication::translate("Muxora", "大小"), QCoreApplication::translate("Muxora", "类型")});
        table->verticalHeader()->hide(); table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setAlternatingRowColors(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        const QStringList names = {"src", "docs", "resources", "CMakeLists.txt", "README.md", "LICENSE", "AGENTS.md"};
        for (int row = 0; row < names.size(); ++row) {
            table->setItem(row, 0, new QTableWidgetItem(iconFor(row < 3 ? "files" : "window", windows->theme()), names[row]));
            table->setItem(row, 1, new QTableWidgetItem(row < 3 ? "—" : QCoreApplication::translate("Muxora", "预览")));
            table->setItem(row, 2, new QTableWidgetItem(row < 3 ? QCoreApplication::translate("Muxora", "目录") : QCoreApplication::translate("Muxora", "文件")));
        }
        return table;
    }
    if (options.contentType == "transfers") {
        auto* label = new QLabel(QCoreApplication::translate("Muxora", "传输列表区域\n\n文件传输尚未接入。\n这个标签与其他标签使用同一套停靠和窗口操作。"));
        label->setAlignment(Qt::AlignCenter); label->setWordWrap(true);
        label->setFocusPolicy(Qt::StrongFocus); return label;
    }
    auto* page = new QWidget; page->setObjectName("WelcomePage");
    auto* layout = new QVBoxLayout(page); layout->setContentsMargins(30, 28, 30, 28); layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("Muxora"), page);
    QFont font = title->font(); font.setPointSize(26); font.setBold(true); title->setFont(font); layout->addWidget(title);
    auto* subtitle = new QLabel(QCoreApplication::translate("Muxora", "让远程工作区按你的习惯排列"), page);
    font = subtitle->font(); font.setPointSize(13); subtitle->setFont(font); layout->addWidget(subtitle);
    auto* description = new QLabel(QCoreApplication::translate("Muxora",
        "这是 Muxora 的首个窗口系统预览。\n\n"
        "可以拖动标签、自由分组、拆分窗格、收纳边栏，\n"
        "也可以拖出轻量浮窗，再合并到其他窗口。\n\n"
        "会话、文件和传输区域目前用于试用布局，实际远程功能将在后续接入。"), page);
    description->setWordWrap(true); layout->addWidget(description);
    const QVector<QPair<QString, QString>> actions = {{QCoreApplication::translate("Muxora", "打开一个工作区标签"), "host.newTab"},
        {QCoreApplication::translate("Muxora", "查看所有命令"), "workbench.showCommandPalette"},
        {QCoreApplication::translate("Muxora", "调整窗口行为"), "window.settings"}};
    for (const auto& action : actions) {
        auto* button = new QPushButton(action.first, page); button->setMaximumWidth(260);
        QObject::connect(button, &QPushButton::clicked, windows, [windows, id = action.second] { windows->execute(id); });
        layout->addWidget(button);
    }
    layout->addStretch();
    auto* tip = new QLabel(QCoreApplication::translate("Muxora", "右键标签或浮窗标题栏，可以找到更多布局操作。"), page);
    tip->setObjectName("MutedText"); tip->setWordWrap(true); layout->addWidget(tip); return page;
}
void populateInitialWorkspace(WindowManager& windows, WindowId window)
{
    const TabOptions welcome{QCoreApplication::translate("Muxora", "欢迎"), "welcome", {}};
    const auto first = windows.addTab(window, createPlaceholder(welcome, &windows), welcome);
    const TabOptions notes{QCoreApplication::translate("Muxora", "工作区 1"), "notes", {}};
    windows.addTab(window, createPlaceholder(notes, &windows), notes);
    const TabOptions files{QCoreApplication::translate("Muxora", "文件浏览"), "files", {}};
    const auto fileTab = windows.addTab(window, createPlaceholder(files, &windows), files);
    windows.moveTab(fileTab, windows.model().groupForTab(first), Placement::Right);
    auto* address = new QLineEdit(QCoreApplication::translate("Muxora", "/workspace · 文件视图预览")); address->setReadOnly(true);
    windows.setTabTopBar(fileTab, address);
    windows.setTabBottomBar(fileTab, informationBar(QCoreApplication::translate("Muxora", "7 项  ·  文件功能尚未接入")));
    const TabOptions sessions{QCoreApplication::translate("Muxora", "会话"), "sessions", {}};
    const auto sessionTab = windows.addTab(window, createPlaceholder(sessions, &windows), sessions);
    windows.moveTabToEdge(sessionTab, Edge::Left, true);
    const TabOptions transfers{QCoreApplication::translate("Muxora", "传输队列"), "transfers", {}};
    const auto transferTab = windows.addTab(window, createPlaceholder(transfers, &windows), transfers);
    windows.moveTabToEdge(transferTab, Edge::Bottom);
    windows.activateTab(first);
}
} // namespace muxora
