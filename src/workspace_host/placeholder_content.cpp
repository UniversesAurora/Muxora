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
#include <window_system/interface_appearance.hpp>

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
        const auto resizeFont = [editor] {
            QFont font; font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Microsoft YaHei"), QStringLiteral("monospace")});
            font.setStyleHint(QFont::Monospace); font.setPointSizeF(11 * interfaceScale()); editor->setFont(font);
        };
        resizeFont();
        if (auto* controller = InterfaceAppearanceController::applicationInstance())
            QObject::connect(controller, &InterfaceAppearanceController::appearanceChanged, editor, resizeFont);
        editor->setPlainText(QCoreApplication::translate("Muxora",
            "Muxora / Workspace notes\n\nThis is a local editor. Type here and check that content survives moving between windows.\nSSH and a real terminal will follow after the window system is ready.\n\nTry these operations:\n  · Drag a tab onto the center or direction guide of a target group\n  · Drag to the desktop to create a floating window, then drag its title bar to dock it again\n  · Right-click a tab to auto-hide it in any sidebar or split its panes\n\nCtrl+Shift+P     Command palette\nCtrl+Shift+E     Split pane right\nCtrl+Shift+O     Split pane down\nCtrl+Alt+Arrow   Navigate tab groups\nAlt+Arrow        Navigate panes\nCtrl+Shift+D     Move to floating window\n"));
        return editor;
    }
    if (options.contentType == "sessions") {
        auto* tree = new QTreeWidget; tree->setHeaderHidden(true); tree->setObjectName("SessionPlaceholder");
        auto* group = new QTreeWidgetItem(tree, {QCoreApplication::translate("Muxora", "Sessions · Layout preview")});
        for (const auto& text : {QCoreApplication::translate("Muxora", "Development server"), QCoreApplication::translate("Muxora", "Test server"), QCoreApplication::translate("Muxora", "Personal host")}) {
            auto* item = new QTreeWidgetItem(group, {text}); item->setIcon(0, iconFor("terminal", windows->theme()));
        }
        group->setExpanded(true); tree->setMinimumWidth(160); return tree;
    }
    if (options.contentType == "files") {
        auto* table = new QTableWidget(7, 3); table->setObjectName("FilePlaceholder");
        table->setHorizontalHeaderLabels({QCoreApplication::translate("Muxora", "Name"), QCoreApplication::translate("Muxora", "Size"), QCoreApplication::translate("Muxora", "Type")});
        table->verticalHeader()->hide(); table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setAlternatingRowColors(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        const QStringList names = {"src", "docs", "resources", "CMakeLists.txt", "README.md", "LICENSE", "AGENTS.md"};
        for (int row = 0; row < names.size(); ++row) {
            table->setItem(row, 0, new QTableWidgetItem(iconFor(row < 3 ? "files" : "window", windows->theme()), names[row]));
            table->setItem(row, 1, new QTableWidgetItem(row < 3 ? "—" : QCoreApplication::translate("Muxora", "Preview")));
            table->setItem(row, 2, new QTableWidgetItem(row < 3 ? QCoreApplication::translate("Muxora", "Directory") : QCoreApplication::translate("Muxora", "File")));
        }
        return table;
    }
    if (options.contentType == "transfers") {
        auto* label = new QLabel(QCoreApplication::translate("Muxora", "Transfer list\n\nFile transfer is not connected yet.\nThis tab uses the same docking and window operations as other tabs."));
        label->setAlignment(Qt::AlignCenter); label->setWordWrap(true);
        label->setFocusPolicy(Qt::StrongFocus); return label;
    }
    auto* page = new QWidget; page->setObjectName("WelcomePage");
    auto* layout = new QVBoxLayout(page); layout->setContentsMargins(30, 28, 30, 28); layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("Muxora"), page);
    QFont font = title->font(); font.setPointSize(26); font.setBold(true); title->setFont(font); layout->addWidget(title);
    auto* subtitle = new QLabel(QCoreApplication::translate("Muxora", "Arrange remote workspaces your way"), page);
    font = subtitle->font(); font.setPointSize(13); subtitle->setFont(font); layout->addWidget(subtitle);
    const auto resizeWelcome = [page, layout, title, subtitle] {
        const auto* controller = InterfaceAppearanceController::applicationInstance();
        const double factor = interfaceScale();
        layout->setContentsMargins(uiPixels(30), uiPixels(28), uiPixels(30), uiPixels(28)); layout->setSpacing(uiPixels(14));
        auto font = controller ? controller->font() : page->font(); font.setPointSizeF(26 * factor); font.setBold(true); title->setFont(font);
        font.setPointSizeF(13 * factor); font.setBold(false); subtitle->setFont(font);
    };
    resizeWelcome();
    if (auto* controller = InterfaceAppearanceController::applicationInstance())
        QObject::connect(controller, &InterfaceAppearanceController::appearanceChanged, page, resizeWelcome);
    auto* description = new QLabel(QCoreApplication::translate("Muxora",
        "This is the first Muxora window system preview.\n\nDrag tabs, arrange tab groups, split panes, and auto-hide tabs in sidebars.\nDetach a floating window and dock it into another window.\n\nSessions, files, and transfers are layout placeholders. Remote operations will follow later."), page);
    description->setWordWrap(true); layout->addWidget(description);
    const QVector<QPair<QString, QString>> actions = {{QCoreApplication::translate("Muxora", "Open a workspace tab"), "host.newTab"},
        {QCoreApplication::translate("Muxora", "Show all commands"), "workbench.showCommandPalette"},
        {QCoreApplication::translate("Muxora", "Adjust window behavior"), "window.settings"}};
    for (const auto& action : actions) {
        auto* button = new QPushButton(action.first, page); button->setMaximumWidth(260);
        QObject::connect(button, &QPushButton::clicked, windows, [windows, id = action.second] { windows->execute(id); });
        layout->addWidget(button);
    }
    layout->addStretch();
    auto* tip = new QLabel(QCoreApplication::translate("Muxora", "Right-click a tab or floating window title bar for more layout operations."), page);
    tip->setObjectName("MutedText"); tip->setWordWrap(true); layout->addWidget(tip); return page;
}
void populateInitialWorkspace(WindowManager& windows, WindowId window)
{
    const TabOptions welcome{QCoreApplication::translate("Muxora", "Welcome"), "welcome", {}};
    const auto first = windows.addTab(window, createPlaceholder(welcome, &windows), welcome);
    const TabOptions notes{QCoreApplication::translate("Muxora", "Workspace 1"), "notes", {}};
    windows.addTab(window, createPlaceholder(notes, &windows), notes);
    const TabOptions files{QCoreApplication::translate("Muxora", "Files"), "files", {}};
    const auto fileTab = windows.addTab(window, createPlaceholder(files, &windows), files);
    windows.moveTab(fileTab, windows.model().groupForTab(first), Placement::Right);
    auto* address = new QLineEdit(QCoreApplication::translate("Muxora", "/workspace · File view preview")); address->setReadOnly(true);
    windows.setTabTopBar(fileTab, address);
    windows.setTabBottomBar(fileTab, informationBar(QCoreApplication::translate("Muxora", "%n items  ·  File operations are not connected yet", nullptr, 7)));
    const TabOptions sessions{QCoreApplication::translate("Muxora", "Sessions"), "sessions", {}};
    const auto sessionTab = windows.addTab(window, createPlaceholder(sessions, &windows), sessions);
    windows.moveTabToEdge(sessionTab, Edge::Left, true);
    const TabOptions transfers{QCoreApplication::translate("Muxora", "Transfers"), "transfers", {}};
    const auto transferTab = windows.addTab(window, createPlaceholder(transfers, &windows), transfers);
    windows.moveTabToEdge(transferTab, Edge::Bottom);
    windows.activateTab(first);
}
} // namespace muxora
