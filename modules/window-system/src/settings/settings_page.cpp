// SPDX-License-Identifier: MIT
#include "settings_page.hpp"
#include "../style/themed_controls.hpp"
#include <window_system/localization.hpp>
#include <QCoreApplication>

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace window_system {
namespace {

bool matches(const QString& haystack, const QString& query)
{
    const QString folded = haystack.toCaseFolded();
    const auto terms = query.simplified().toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(terms.cbegin(), terms.cend(), [&folded](const QString& term) {
        return folded.contains(term);
    });
}

QLabel* descriptionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setProperty("role", QStringLiteral("hint"));
    return label;
}

QVBoxLayout* pageLayout(QWidget* page, const QString& heading, const QString& description)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);
    auto* title = new QLabel(heading, page);
    title->setProperty("role", QStringLiteral("heading"));
    QFont font = title->font();
    font.setPointSizeF(font.pointSizeF() + 4);
    title->setFont(font);
    layout->addWidget(title);
    layout->addWidget(descriptionLabel(description, page));
    return layout;
}

// Keep the native tree's keyboard navigation, expansion and accessibility,
// while presenting its current location with typography and a narrow marker.
class CategoryDelegate final : public QStyledItemDelegate {
public:
    explicit CategoryDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        auto plain = option;
        plain.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        QStyledItemDelegate::paint(painter, plain, index);
    }
};

// Native branch primitives can include connection lines and a platform hover
// plate. Draw only the disclosure glyph, reserving a separate location gutter.
class CategoryNavigation final : public QTreeWidget {
public:
    explicit CategoryNavigation(WindowManager* manager, QWidget* parent)
        : QTreeWidget(parent), manager_(manager)
    {
        setIndentation(32);
        setMouseTracking(true);
        viewport()->setMouseTracking(true);
        QObject::connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this] { viewport()->update(); });
    }

protected:
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override
    {
        // QTreeView paints the selected row's branch plate before this hook.
        // Clear it for both parent and leaf rows; the location marker is drawn
        // after the tree, separately from the disclosure glyph.
        painter->fillRect(rect, manager_ ? manager_->theme().surface : palette().color(QPalette::Base));
        if (!hasVisibleChildren(index)) return;
        const auto glyph = arrowRect(index);
        const bool hovered = hoveredBranch_ == index;
        const QColor color = manager_ ? (hovered ? manager_->theme().text : manager_->theme().mutedText) :
                                       palette().color(hovered ? QPalette::Text : QPalette::PlaceholderText);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const auto center = glyph.center();
        QPolygonF arrow;
        if (isExpanded(index))
            arrow << center + QPointF(-3, -1.5) << center + QPointF(0, 1.5) << center + QPointF(3, -1.5);
        else
            arrow << center + QPointF(-1.5, -3) << center + QPointF(1.5, 0) << center + QPointF(-1.5, 3);
        painter->drawPolyline(arrow);
        painter->restore();
    }

    void paintEvent(QPaintEvent* event) override
    {
        QTreeWidget::paintEvent(event);
        const auto row = visualRect(currentIndex());
        if (!currentIndex().isValid() || row.isEmpty()) return;
        QPainter painter(viewport());
        const QColor accent = manager_ ? manager_->theme().accent : palette().color(QPalette::Highlight);
        painter.fillRect(QRect(2, row.top() + 4, 2, std::max(0, row.height() - 8)), accent);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        QTreeWidget::mouseMoveEvent(event);
        // The row can be found through its text column even when the pointer
        // is over its branch gutter rather than over the text itself.
        const QPoint position = event->position().toPoint();
        const auto row = indexAt(QPoint(viewport()->width() / 2, position.y()));
        const QPersistentModelIndex next = row.isValid() && hasVisibleChildren(row) &&
            arrowRect(row).adjusted(-4, -4, 4, 4).contains(position) ? row : QModelIndex();
        if (next == hoveredBranch_) return;
        hoveredBranch_ = next;
        viewport()->update();
    }

    bool viewportEvent(QEvent* event) override
    {
        if (event->type() == QEvent::Leave && hoveredBranch_.isValid()) {
            hoveredBranch_ = QModelIndex();
            viewport()->update();
        }
        return QTreeWidget::viewportEvent(event);
    }

private:
    bool hasVisibleChildren(const QModelIndex& index) const
    {
        for (int row = 0; row < model()->rowCount(index); ++row)
            if (!isRowHidden(row, index)) return true;
        return false;
    }

    QRectF arrowRect(const QModelIndex& index) const
    {
        const auto row = visualRect(index);
        return QRectF(row.left() - indentation() / 2.0 - 4, row.center().y() - 4, 8, 8);
    }

    QPointer<WindowManager> manager_;
    QPersistentModelIndex hoveredBranch_;
};

} // namespace

SettingsPage::SettingsPage(WindowManager* manager, QWidget* parent)
    : QWidget(parent), manager_(manager)
{
    initialWindow_ = manager->context().window;
    setObjectName(QStringLiteral("SettingsPage"));
    setProperty("windowSystemSettingsKind", QStringLiteral("window_system.settings"));
    auto* layout = pageLayout(this, QCoreApplication::translate("WindowSystem", "Settings"),
        QCoreApplication::translate("WindowSystem", "Changes take effect immediately. Window behavior is saved with the workspace layout."));
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("SettingsSearch"));
    search_->setPlaceholderText(QCoreApplication::translate("WindowSystem", "Search setting names, descriptions, or IDs…"));
    search_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Search settings"));
    search_->setClearButtonEnabled(true);
    layout->addWidget(search_);
    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    categories_ = new CategoryNavigation(manager, this);
    categories_->setObjectName(QStringLiteral("SettingsCategories"));
    categories_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Setting categories"));
    categories_->setHeaderHidden(true);
    categories_->setFrameShape(QFrame::NoFrame);
    categories_->setColumnCount(1);
    categories_->setSelectionMode(QAbstractItemView::SingleSelection);
    categories_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories_->setUniformRowHeights(true);
    categories_->setItemDelegate(new CategoryDelegate(categories_));
    categories_->setMaximumWidth(180);
    categories_->setMinimumWidth(90);
    body->addWidget(categories_);
    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("SettingsCategoryDivider"));
    divider->setFixedWidth(1);
    divider->setAttribute(Qt::WA_StyledBackground);
    body->addWidget(divider);
    auto* right = new QVBoxLayout;
    results_ = descriptionLabel({}, this);
    results_->setObjectName(QStringLiteral("SettingsResultCount"));
    right->addWidget(results_);
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("SettingsScrollArea"));
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->viewport()->setObjectName(QStringLiteral("SettingsViewport"));
    scroll_->viewport()->setAttribute(Qt::WA_StyledBackground);
    auto* contents = new QWidget(scroll_);
    contents->setObjectName(QStringLiteral("SettingsRows"));
    contents->setAttribute(Qt::WA_StyledBackground);
    auto* rows = new QVBoxLayout(contents);
    rows->setContentsMargins(0, 0, 12, 0);
    rows->setSpacing(18);

    if (auto* translations = TranslationManager::applicationInstance()) {
        addCategory(contents, rows, QStringLiteral("application"), QCoreApplication::translate("WindowSystem", "Application"));
        language_ = new ThemedComboBox(contents);
        language_->setObjectName(QStringLiteral("DisplayLanguage"));
        language_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Display language"));
        for (const auto language : supportedUiLanguages()) language_->addItem(uiLanguageName(language), uiLanguageId(language));
        rows->addWidget(addRow(contents, QStringLiteral("application"), QCoreApplication::translate("WindowSystem", "Display language"),
            QStringLiteral("workbench.language"), QCoreApplication::translate("WindowSystem", "Restart the application to apply. This setting is independent of the window layout."), language_));
        const QPointer<TranslationManager> controller(translations);
        QObject::connect(language_, &QComboBox::currentIndexChanged, this, [this, controller] {
            if (!controller) return;
            const QPointer<SettingsPage> self(this);
            controller->setRequestedLanguage(uiLanguageFromId(language_->currentData().toString()));
            if (self) self->syncControls();
        });
        QObject::connect(translations, &TranslationManager::requestedLanguageChanged, this, [this] { syncControls(); });
        QObject::connect(translations, &QObject::destroyed, language_, [this] { language_->setEnabled(false); });
    }
    addCategory(contents, rows, QStringLiteral("window"), QCoreApplication::translate("WindowSystem", "Window"));
    newTabPosition_ = new ThemedComboBox(contents);
    newTabPosition_->setObjectName(QStringLiteral("NewTabPosition"));
    newTabPosition_->setAccessibleName(QCoreApplication::translate("WindowSystem", "New tab position"));
    for (const auto& entry : {std::pair{QCoreApplication::translate("WindowSystem", "After the active tab (default)"), NewTabPosition::AfterActive},
                             std::pair{QCoreApplication::translate("WindowSystem", "Before the active tab"), NewTabPosition::BeforeActive},
                             std::pair{QCoreApplication::translate("WindowSystem", "Start of tab group"), NewTabPosition::Beginning},
                             std::pair{QCoreApplication::translate("WindowSystem", "End of tab group"), NewTabPosition::End}})
        newTabPosition_->addItem(entry.first, int(entry.second));
    rows->addWidget(addRow(contents, QStringLiteral("window"), QCoreApplication::translate("WindowSystem", "New tab position"),
        QStringLiteral("window.newTabPosition"), QCoreApplication::translate("WindowSystem", "Applies to tabs created using the plus button, type menu, or commands."), newTabPosition_));
    detachKind_ = new ThemedComboBox(contents);
    detachKind_->setObjectName(QStringLiteral("DetachWindowKind"));
    detachKind_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Detached tab window type"));
    detachKind_->addItem(QCoreApplication::translate("WindowSystem", "Floating window (keeps its owner)"), int(WindowKind::Floating));
    detachKind_->addItem(QCoreApplication::translate("WindowSystem", "Workbench window"), int(WindowKind::Workbench));
    rows->addWidget(addRow(contents, QStringLiteral("window"), QCoreApplication::translate("WindowSystem", "Window type for detached tabs"),
        QStringLiteral("window.dragOutsideKind"),
        QCoreApplication::translate("WindowSystem", "Choose a floating or workbench window when detaching a tab."), detachKind_));

    emptyPolicy_ = new ThemedComboBox(contents);
    emptyPolicy_->setObjectName(QStringLiteral("EmptyWorkbenchPolicy"));
    emptyPolicy_->setAccessibleName(QCoreApplication::translate("WindowSystem", "When a workbench window becomes empty"));
    emptyPolicy_->addItem(QCoreApplication::translate("WindowSystem", "Keep the empty workbench window"), false);
    emptyPolicy_->addItem(QCoreApplication::translate("WindowSystem", "Automatically close the empty workbench window"), true);
    rows->addWidget(addRow(contents, QStringLiteral("window"), QCoreApplication::translate("WindowSystem", "When the window becomes empty"),
        QStringLiteral("window.closeEmptyWorkbench"),
        QCoreApplication::translate("WindowSystem", "The workbench window remains open while it owns floating windows."), emptyPolicy_));

    addCategory(contents, rows, QStringLiteral("window/drag"), QCoreApplication::translate("WindowSystem", "Dragging"));
    reveal_ = new ThemedCheckBox(QCoreApplication::translate("WindowSystem", "Bring an obscured target window forward on hover"), contents);
    reveal_->setObjectName(QStringLiteral("RevealDuringDrag"));
    rows->addWidget(addRow(contents, QStringLiteral("window/drag"), QCoreApplication::translate("WindowSystem", "Reveal target window"),
        QStringLiteral("window.revealDuringDrag"),
        QCoreApplication::translate("WindowSystem", "Hover a dragged tab over a target window to bring it forward for dropping."), reveal_));

    delay_ = new ThemedSpinBox(contents);
    delay_->setObjectName(QStringLiteral("RevealDelayMs"));
    delay_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Target window hover delay"));
    delay_->setRange(200, 2000);
    delay_->setSingleStep(100);
    delay_->setSuffix(QCoreApplication::translate("WindowSystem", " ms"));
    rows->addWidget(addRow(contents, QStringLiteral("window/drag"), QCoreApplication::translate("WindowSystem", "Hover delay"),
        QStringLiteral("window.revealDelayMs"),
        QCoreApplication::translate("WindowSystem", "Delay before bringing an obscured window forward."), delay_));
    addViewOptions(contents, rows);
    addTabAppearanceOptions(contents, rows);
    rows->addStretch();
    scroll_->setWidget(contents);
    right->addWidget(scroll_, 1);
    body->addLayout(right, 1);
    layout->addLayout(body, 1);

    const auto change = [this] {
        if (!manager_) return;
        auto next = manager_->behavior();
        next.dragOutsideKind = WindowKind(detachKind_->currentData().toInt());
        next.closeEmptyWorkbench = emptyPolicy_->currentData().toBool();
        next.revealDuringDrag = reveal_->isChecked();
        next.revealDelayMs = delay_->value();
        next.newTabPosition = NewTabPosition(newTabPosition_->currentData().toInt());
        const QPointer<SettingsPage> self(this);
        manager_->setBehavior(next);
        // Saving the change may synchronously close this page or destroy its
        // manager. The notification is the last unguarded use of this widget.
        if (self) self->syncControls();
    };
    QObject::connect(detachKind_, &QComboBox::currentIndexChanged, this, change);
    QObject::connect(newTabPosition_, &QComboBox::currentIndexChanged, this, change);
    QObject::connect(emptyPolicy_, &QComboBox::currentIndexChanged, this, change);
    QObject::connect(reveal_, &QCheckBox::toggled, this, change);
    QObject::connect(delay_, &QSpinBox::valueChanged, this, change);
    QObject::connect(search_, &QLineEdit::textChanged, this, [this] { filterRows(); });
    QObject::connect(categories_, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* item) { scrollToCategory(item); });
    QObject::connect(categories_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) { scrollToCategory(item); });
    QObject::connect(categories_, &QTreeWidget::itemCollapsed, this, [this] { syncNavigation(); });
    QObject::connect(categories_, &QTreeWidget::itemExpanded, this, [this] { syncNavigation(); });
    QObject::connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this,
        [this] { syncNavigation(); });
    QObject::connect(manager, &WindowManager::layoutChanged, this, [this] { syncControls(); });
    syncControls();
    filterRows();
    setFocusProxy(search_);
}

QWidget* SettingsPage::addRow(QWidget* parent, const QString& category,
                              const QString& title, const QString& key,
                              const QString& description, QWidget* control)
{
    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("SettingRow"));
    row->setProperty("settingId", key);
    auto* column = new QVBoxLayout(row);
    column->setContentsMargins(8, 4, 8, 8);
    column->setSpacing(6);
    auto* titleLabel = new QLabel(title, row);
    titleLabel->setProperty("role", QStringLiteral("heading"));
    titleLabel->setBuddy(control);
    column->addWidget(titleLabel);
    column->addWidget(descriptionLabel(description, row));
    auto* controlRow = new QHBoxLayout;
    controlRow->addWidget(control);
    controlRow->addStretch();
    column->addLayout(controlRow);
    auto* idLabel = descriptionLabel(key, row);
    idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(idLabel);
    QString categoryTitles;
    for (const auto& section : sections_)
        if (category == section.id || category.startsWith(section.id + QLatin1Char('/')))
            categoryTitles += section.item->text(0) + QLatin1Char(' ');
    rows_.push_back({category, categoryTitles + title + QLatin1Char(' ') +
                              key + QLatin1Char(' ') + description, row});
    return row;
}

void SettingsPage::addCategory(QWidget* parent, QVBoxLayout* rows,
                               const QString& id, const QString& title)
{
    QTreeWidgetItem* parentItem = nullptr;
    const qsizetype separator = id.lastIndexOf(QLatin1Char('/'));
    const QString parentId = separator < 0 ? QString() : id.left(separator);
    for (const auto& section : sections_)
        if (section.id == parentId) { parentItem = section.item; break; }
    auto* item = parentItem ? new QTreeWidgetItem(parentItem, {title}) :
                              new QTreeWidgetItem(categories_, {title});
    item->setData(0, Qt::UserRole, id);
    item->setExpanded(true);
    if (parentItem) parentItem->setExpanded(true);
    auto* heading = new QLabel(title, parent);
    heading->setObjectName(QStringLiteral("SettingsCategoryHeading"));
    heading->setProperty("categoryId", id);
    heading->setProperty("role", QStringLiteral("heading"));
    QFont font = heading->font();
    font.setPointSizeF(font.pointSizeF() + 2);
    font.setBold(true);
    heading->setFont(font);
    rows->addWidget(heading);
    sections_.push_back({id, heading, item});
}

void SettingsPage::scrollToCategory(QTreeWidgetItem* item)
{
    if (!item || item->isHidden()) return;
    const auto section = std::find_if(sections_.cbegin(), sections_.cend(),
        [item](const Category& candidate) { return candidate.item == item; });
    if (section == sections_.cend() || !section->heading || section->heading->isHidden()) return;
    scroll_->widget()->layout()->activate();
    navigating_ = true;
    scroll_->verticalScrollBar()->setValue(section->heading->y());
    markCurrentCategory(item);
    navigating_ = false;
}

void SettingsPage::markCurrentCategory(QTreeWidgetItem* item)
{
    // A collapsed ancestor represents the location of its hidden descendants.
    for (auto* parent = item ? item->parent() : nullptr; parent; parent = parent->parent())
        if (!parent->isExpanded()) item = parent;
    const QSignalBlocker blocker(categories_);
    categories_->setCurrentItem(item);
    for (const auto& section : sections_) {
        auto font = section.item->font(0);
        font.setBold(section.item == item);
        section.item->setFont(0, font);
    }
}

void SettingsPage::syncNavigation()
{
    if (navigating_) return;
    auto* bar = scroll_->verticalScrollBar();
    QTreeWidgetItem* current = nullptr;
    const bool atBottom = bar->value() > 0 && bar->value() == bar->maximum();
    for (const auto& section : sections_) {
        if (!section.heading || section.heading->isHidden()) continue;
        if (!current || atBottom || section.heading->y() <= bar->value() + 4)
            current = section.item;
    }
    markCurrentCategory(current);
}

void SettingsPage::syncControls()
{
    if (language_) if (auto* translations = TranslationManager::applicationInstance()) {
        const QSignalBlocker blocked(language_);
        language_->setCurrentIndex(language_->findData(uiLanguageId(translations->requestedLanguage())));
    }
    if (!manager_) return;
    const auto behavior = manager_->behavior();
    const QSignalBlocker detachBlock(detachKind_);
    const QSignalBlocker positionBlock(newTabPosition_);
    const QSignalBlocker emptyBlock(emptyPolicy_);
    const QSignalBlocker revealBlock(reveal_);
    const QSignalBlocker delayBlock(delay_);
    detachKind_->setCurrentIndex(behavior.dragOutsideKind == WindowKind::Floating ? 0 : 1);
    newTabPosition_->setCurrentIndex(newTabPosition_->findData(int(behavior.newTabPosition)));
    emptyPolicy_->setCurrentIndex(behavior.closeEmptyWorkbench ? 1 : 0);
    reveal_->setChecked(behavior.revealDuringDrag);
    delay_->setValue(behavior.revealDelayMs);
    delay_->setEnabled(behavior.revealDuringDrag);
    syncViewOptions();
    syncTabAppearanceOptions();
}

void SettingsPage::filterRows()
{
    int count = 0;
    for (const auto& row : rows_) {
        const bool visible = matches(row.searchable, search_->text());
        if (row.widget) row.widget->setVisible(visible);
        if (visible) ++count;
    }
    for (const auto& section : sections_) {
        const bool visible = std::any_of(rows_.cbegin(), rows_.cend(), [&section](const Row& row) {
            return row.widget && !row.widget->isHidden() &&
                   (row.category == section.id || row.category.startsWith(section.id + QLatin1Char('/')));
        });
        if (section.heading) section.heading->setVisible(visible);
        section.item->setHidden(!visible);
    }
    results_->setText(count ? QCoreApplication::translate("WindowSystem", "%n settings", nullptr, count) : QCoreApplication::translate("WindowSystem", "No matching settings"));
    scroll_->widget()->layout()->activate();
    syncNavigation();
    // QScrollArea updates its range after child layouts respond to visibility.
    // Re-evaluate then without storing a stale content coordinate.
    QTimer::singleShot(0, this, [this] { syncNavigation(); });
}

} // namespace window_system
