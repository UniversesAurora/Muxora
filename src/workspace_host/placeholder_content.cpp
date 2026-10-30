#include <QCoreApplication>
#include <QApplication>
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
QString notesText()
{
    return QCoreApplication::translate("Muxora",
        "Muxora / Workspace notes\n\nThis is a local editor. Type here and check that content survives moving between windows.\nSSH and a real terminal will follow after the window system is ready.\n\nTry these operations:\n  · Drag a tab onto the center or direction guide of a target group\n  · Drag to the desktop to create a floating window, then drag its title bar to dock it again\n  · Right-click a tab to auto-hide it in any sidebar or split its panes\n\nCtrl+Shift+P     Command palette\nCtrl+Shift+E     Split pane right\nCtrl+Shift+O     Split pane down\nCtrl+Alt+Arrow   Navigate tab groups\nAlt+Arrow        Navigate panes\nCtrl+Shift+D     Move to floating window\n");
}
QFont notesFont()
{
    auto font = QApplication::font(); font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Microsoft YaHei"), QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace); font.setPointSizeF(11 * interfaceScale());
    return font;
}
QString welcomeSubtitle() { return QCoreApplication::translate("Muxora", "Arrange remote workspaces your way"); }
QString welcomeDescription()
{
    return QCoreApplication::translate("Muxora",
        "This is the first Muxora window system preview.\n\nDrag tabs, arrange tab groups, split panes, and auto-hide tabs in sidebars.\nDetach a floating window and dock it into another window.\n\nSessions, files, and transfers are layout placeholders. Remote operations will follow later.");
}
QString welcomeTip() { return QCoreApplication::translate("Muxora", "Right-click a tab or floating window title bar for more layout operations."); }
QVector<QPair<QString, QString>> welcomeActions()
{
    return {{QCoreApplication::translate("Muxora", "Open a workspace tab"), "host.newTab"},
        {QCoreApplication::translate("Muxora", "Show all commands"), "workbench.showCommandPalette"},
        {QCoreApplication::translate("Muxora", "Adjust window behavior"), "window.settings"}};
}
QString sessionsHeading() { return QCoreApplication::translate("Muxora", "Sessions · Layout preview"); }
QStringList sessionNames()
{
    return {QCoreApplication::translate("Muxora", "Development server"), QCoreApplication::translate("Muxora", "Test server"),
        QCoreApplication::translate("Muxora", "Personal host")};
}
QStringList fileColumns()
{
    return {QCoreApplication::translate("Muxora", "Name"), QCoreApplication::translate("Muxora", "Size"), QCoreApplication::translate("Muxora", "Type")};
}
QVector<QStringList> fileRows()
{
    QVector<QStringList> rows;
    const QStringList names = {"src", "docs", "resources", "CMakeLists.txt", "README.md", "LICENSE", "AGENTS.md"};
    for (int row = 0; row < names.size(); ++row)
        rows.append({names[row], row < 3 ? "—" : QCoreApplication::translate("Muxora", "Preview"),
                     row < 3 ? QCoreApplication::translate("Muxora", "Directory") : QCoreApplication::translate("Muxora", "File")});
    return rows;
}
QString transfersText()
{
    return QCoreApplication::translate("Muxora", "Transfer list\n\nFile transfer is not connected yet.\nThis tab uses the same docking and window operations as other tabs.");
}

QWidget* createPlaceholder(const TabOptions& options, WindowManager* windows)
{
    if (options.contentType == "notes") {
        auto* editor = new QPlainTextEdit;
        editor->setObjectName("WorkspaceNotes");
        const auto resizeFont = [editor] { editor->setFont(notesFont()); };
        resizeFont();
        if (auto* controller = InterfaceAppearanceController::applicationInstance())
            QObject::connect(controller, &InterfaceAppearanceController::appearanceChanged, editor, resizeFont);
        editor->setPlainText(notesText());
        return editor;
    }
    if (options.contentType == "sessions") {
        auto* tree = new QTreeWidget; tree->setHeaderHidden(true); tree->setObjectName("SessionPlaceholder");
        auto* group = new QTreeWidgetItem(tree, {sessionsHeading()});
        for (const auto& text : sessionNames()) {
            auto* item = new QTreeWidgetItem(group, {text}); item->setIcon(0, iconFor("terminal", windows->theme()));
        }
        group->setExpanded(true); tree->setMinimumWidth(160); return tree;
    }
    if (options.contentType == "files") {
        auto* table = new QTableWidget(7, 3); table->setObjectName("FilePlaceholder");
        table->setHorizontalHeaderLabels(fileColumns());
        table->verticalHeader()->hide(); table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setAlternatingRowColors(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        const auto rows = fileRows();
        for (int row = 0; row < rows.size(); ++row) {
            table->setItem(row, 0, new QTableWidgetItem(iconFor(row < 3 ? "files" : "window", windows->theme()), rows[row][0]));
            table->setItem(row, 1, new QTableWidgetItem(rows[row][1]));
            table->setItem(row, 2, new QTableWidgetItem(rows[row][2]));
        }
        return table;
    }
    if (options.contentType == "transfers") {
        auto* label = new QLabel(transfersText());
        label->setAlignment(Qt::AlignCenter); label->setWordWrap(true);
        label->setFocusPolicy(Qt::StrongFocus); return label;
    }
    auto* page = new QWidget; page->setObjectName("WelcomePage");
    auto* layout = new QVBoxLayout(page); layout->setContentsMargins(30, 28, 30, 28); layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("Muxora"), page);
    QFont font = title->font(); font.setPointSize(26); font.setBold(true); title->setFont(font); layout->addWidget(title);
    auto* subtitle = new QLabel(welcomeSubtitle(), page);
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
    auto* description = new QLabel(welcomeDescription(), page);
    description->setWordWrap(true); layout->addWidget(description);
    const auto actions = welcomeActions();
    for (const auto& action : actions) {
        auto* button = new QPushButton(action.first, page); button->setMaximumWidth(260);
        QObject::connect(button, &QPushButton::clicked, windows, [windows, id = action.second] { windows->execute(id); });
        layout->addWidget(button);
    }
    layout->addStretch();
    auto* tip = new QLabel(welcomeTip(), page);
    tip->setObjectName("MutedText"); tip->setWordWrap(true); layout->addWidget(tip); return page;
}
TabId addPlaceholderTab(WindowManager& windows, WindowId window, const TabOptions& options, GroupId group)
{
    if (windows.presentation() == WindowManager::Presentation::External) return windows.addTab(window, options, group);
    auto* content = createPlaceholder(options, &windows);
    const auto tab = windows.addTab(window, content, options, group);
    if (tab.isNull()) delete content;
    return tab;
}
void populateInitialWorkspace(WindowManager& windows, WindowId window)
{
    const auto first = addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Welcome"), "welcome", {}});
    addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Workspace 1"), "notes", {}});
    const auto fileTab = addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Files"), "files", {}});
    windows.moveTab(fileTab, windows.model().groupForTab(first), Placement::Right);
    // Quick tab bars arrive with the Quick bar interface (10.3, sub-step 6).
    if (windows.presentation() == WindowManager::Presentation::Widgets) {
        auto* address = new QLineEdit(QCoreApplication::translate("Muxora", "/workspace · File view preview")); address->setReadOnly(true);
        windows.setTabTopBar(fileTab, address);
        windows.setTabBottomBar(fileTab, informationBar(QCoreApplication::translate("Muxora", "%n items  ·  File operations are not connected yet", nullptr, 7)));
    }
    const auto sessionTab = addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Sessions"), "sessions", {}});
    windows.moveTabToEdge(sessionTab, Edge::Left, true);
    const auto transferTab = addPlaceholderTab(windows, window, {QCoreApplication::translate("Muxora", "Transfers"), "transfers", {}});
    windows.moveTabToEdge(transferTab, Edge::Bottom);
    windows.activateTab(first);
}
} // namespace muxora
