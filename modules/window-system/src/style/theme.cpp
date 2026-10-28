// SPDX-License-Identifier: MIT
#include "window_system/theme.hpp"
#include <window_system/interface_appearance.hpp>

#include <QApplication>
#include <QFontMetrics>
#include <QPalette>
#include <QRegularExpression>
#include <QSplitter>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

#include <algorithm>

namespace window_system {
namespace {

QString color(const QColor& value)
{
    return value.name(QColor::HexRgb);
}

} // namespace

Theme scaledInterfaceTheme(const Theme& theme)
{
    auto resolved = theme;
    auto& m = resolved.metrics;
    for (auto* value : {&m.controlHeight, &m.headerHeight, &m.tabHeight, &m.statusHeight,
         &m.edgeThickness, &m.paneHeaderHeight, &m.splitterWidth, &m.cornerRadius,
         &m.padding, &m.spacing, &m.iconSize, &m.groupSplitterWidth}) *value = uiPixels(*value);
    // Font size is independent of interface zoom. Rails must fit the current
    // interface font, and their geometry must agree with DockSurface's insets.
    if (qApp) m.edgeThickness = std::max(m.edgeThickness, QFontMetrics(QApplication::font()).height() + uiPixels(4));
    return resolved;
}

QString styleSheet(const Theme& theme)
{
    // A root stylesheet naturally follows newly added/reparented children. A
    // shared QWidget::setStyle proxy does not, and would retain cross-window
    // style pointers when content is moved between separately owned roots.
    QString rules = QString::fromLatin1(R"qss(
QWidget { color: @text; }
QWidget:disabled { color: @muted; }
QDialog { background: @surface; }
QWidget#WorkspaceWindow { background: @background; border: 1px solid @border; }
QWidget#WorkspaceWindow[windowSystemNativeSides="true"] { border-width: 1px 0 0 0; }
QWidget#WorkspaceWindow[windowSystemExpanded="true"] { border: 0; }
QWidget#WindowHeader, QWidget#CommandStripHost, QWidget#CommandStrip, QLabel#StripDragPreview { background: @header; color: @headerText; }
QWidget#WindowHeader QLabel { color: @headerText; }
QWidget#TabGroup, QWidget#TabGroupHeader, QWidget#TabStripCanvas, QWidget#TabCreateButtons { background: transparent; }
QScrollArea#TabScrollViewport { background: transparent; }
QWidget#TabPage, QWidget#PaneHost { background: @surface; }
QLabel#TabGroupName, QLabel#TabGroupTitle { color: @muted; background: transparent; }
QWidget#PaneHeader { background: @raised; border-bottom: 1px solid @border; }
QWidget#PaneHeader[active="true"] { background: @soft; border-bottom: 1px solid @accent; }
QWidget#DockSurfaceRoot { background: transparent; }
QWidget#EdgeBar, QWidget#EdgeTabTrack, QScrollArea#EdgeScrollViewport { background: transparent; }
QWidget#PeekPanel { background: @surface; border: 1px solid @accent; }
QWidget#DragEdgeContent { background: @surface; border: 1px solid @border; }
QWidget#DragPeekTitle { background: @header; color: @headerText; }
QWidget#WorkspaceWindow[windowSystemDragPresentation="true"] { border: 0; }
QWidget#WorkspaceWindow[windowSystemDragPresentation="true"] QFrame#TabGroup { border: 0; }
QWidget#DockOverlay { background: transparent; }
QWidget#CommandPalette { background: @surface; border: 1px solid @accent; }
QLabel[role="hint"] { color: @muted; }
QLabel[role="heading"] { color: @accent; font-weight: 600; }
QPushButton, QToolButton { background: @raised; border: 1px solid @border; border-radius: 0; padding: 2px 7px; min-height: @controlInnerpx; }
QToolButton { background: transparent; border-color: transparent; padding: 2px 4px; }
QToolButton#TabListButton, QToolButton#UnpinCurrentButton, QToolButton#UnpinAllButton,
QToolButton#PinPeekTabButton, QToolButton#ClosePeekTabButton, QToolButton#PinEdgeButton {
    padding: 0; min-height: 0; min-width: 0;
}
QPushButton:hover, QToolButton:hover { background: @buttonHover; border-color: @accent; }
QPushButton:pressed, QToolButton:pressed, QToolButton:checked { background: @selection; border-color: @accent; }
QPushButton:focus, QToolButton:focus { border-color: @focus; }
QPushButton:default { border-color: @accent; }
QPushButton:disabled, QToolButton:disabled { color: @muted; border-color: @border; background: @raised; }
QToolButton#StripCommandButton:disabled, QToolButton#StripOverflowButton:disabled {
    color: @muted; background: transparent; border-color: transparent;
}
QToolButton#CaptionButton { color: @headerText; background: transparent; border: none; border-radius: 0; padding: 0; }
QToolButton#CaptionButton:hover { background: @buttonHover; }
QToolButton#CaptionButton[role="close"]:hover { background: @danger; color: white; }
QToolButton#EdgeButton { color: @statusText; border: none; border-radius: 0; padding: 3px 6px; }
QToolButton#EdgeButton:hover, QToolButton#EdgeButton:checked { background: @buttonHover; color: @text; }
QWidget[windowSystemSplitButton="true"] QToolButton {
    background: @raised; border: none; padding: 0; min-height: 0; min-width: 0;
}
QWidget[windowSystemSplitButton="true"] QToolButton[splitSide="left"] {
    border-top-left-radius: 6px; border-bottom-left-radius: 6px;
    border-top-right-radius: 0; border-bottom-right-radius: 0;
}
QWidget[windowSystemSplitButton="true"] QToolButton[splitSide="right"] {
    border-top-left-radius: 0; border-bottom-left-radius: 0;
    border-top-right-radius: 6px; border-bottom-right-radius: 6px;
}
QWidget[windowSystemSplitButton="true"][emphasized="true"] QToolButton { background: @buttonHover; }
QWidget[windowSystemSplitButton="true"] QToolButton:pressed { background: @selection; }
QWidget[windowSystemSplitButton="true"][popupOpen="true"] QToolButton[splitPart="primary"] { background: @raised; }
QWidget[windowSystemSplitButton="true"][popupOpen="true"] QToolButton[splitPart="menu"] { background: @selection; }
QMenuBar, QMenuBar#MenuStrip { background: transparent; border: none; padding: 0; color: @headerText; }
QMenuBar::item { background: transparent; padding: 5px 8px; }
QMenuBar::item:selected { background: @soft; }
QMenuBar::item:pressed { background: @selection; }
QMenu { background: @surface; color: @text; border: 1px solid @border; border-radius: 0; padding: 3px; }
QMenu::item { padding: 5px 24px 5px 22px; }
QMenu::item:selected { background: @selection; color: @selectionText; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 3px 5px; }
QWidget#SettingsRows, QWidget#SettingsViewport { background: @surface; color: @text; }
QTreeView#SettingsCategories { background: @surface; border: none; outline: none; }
QTreeView#SettingsCategories:focus { border: none; }
QTreeView#SettingsCategories::item, QTreeView#SettingsCategories::item:hover, QTreeView#SettingsCategories::item:selected { background: transparent; color: @text; border: none; }
QFrame#SettingsCategoryDivider { background: @border; border: none; }
QToolBar { background: @header; border: none; spacing: 3px; padding: 2px; }
QToolBar::separator { width: 1px; background: @border; margin: 3px 4px; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit { background: @surface; color: @text; border: 1px solid @border; border-radius: 0; padding: 2px 5px; selection-background-color: @selection; selection-color: @selectionText; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height: @controlInnerpx; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: @focus; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background: @raised; color: @muted; }
QComboBox { padding-right: 22px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 19px; border: none; }
QSpinBox, QDoubleSpinBox { padding-right: 20px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: padding; subcontrol-position: top right; width: 18px; border: 1px solid transparent; background: transparent; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: padding; subcontrol-position: bottom right; width: 18px; border: 1px solid transparent; background: transparent; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { border-color: @accent; background: @buttonHover; }
QSlider[windowSystemIntegerSlider="true"] { min-height: @controlInnerpx; }
QSlider[windowSystemIntegerSlider="true"]::groove:horizontal { height: 4px; background: @border; border: none; }
QSlider[windowSystemIntegerSlider="true"]::sub-page:horizontal { background: @accent; }
QSlider[windowSystemIntegerSlider="true"]::handle:horizontal { width: 8px; margin: -6px 0; background: @surface; border: 1px solid @accent; border-radius: 0; }
QSlider[windowSystemIntegerSlider="true"]::handle:horizontal:hover { background: @buttonHover; }
QSlider[windowSystemIntegerSlider="true"]::handle:horizontal:pressed { background: @selection; }
QSlider[windowSystemIntegerSlider="true"]::handle:horizontal:focus { border-color: @focus; }
QSlider[windowSystemIntegerSlider="true"]::groove:horizontal:disabled, QSlider[windowSystemIntegerSlider="true"]::sub-page:horizontal:disabled { background: @raised; }
QSlider[windowSystemIntegerSlider="true"]::handle:horizontal:disabled { background: @raised; border-color: @muted; }
QComboBox QAbstractItemView { background: @surface; color: @text; border: 1px solid @border; selection-background-color: @selection; selection-color: @selectionText; }
QAbstractItemView { background: @surface; alternate-background-color: @raised; color: @text; border: 1px solid @border; selection-background-color: @selection; selection-color: @selectionText; outline: 0; }
QAbstractItemView::item { padding: 3px 4px; min-height: 18px; }
QAbstractItemView::item:hover { background: @soft; }
QAbstractItemView::item:selected { background: @selection; color: @selectionText; }
QAbstractItemView:focus { border-color: @focus; }
QHeaderView::section { background: @raised; color: @text; padding: 4px 6px; border: none; border-right: 1px solid @border; border-bottom: 1px solid @border; }
QTabBar, QTabBar#TabBar { background: @header; }
QTabBar::tab { background: @inactive; color: @text; border: none; border-bottom: 2px solid @border; border-radius: 0; padding: 3px 6px; min-height: @tabInnerpx; margin-right: 0; }
QTabBar::tab:hover { background: @buttonHover; }
QTabBar::tab:selected { background: @active; border-bottom: 2px solid @accent; }
QTabBar::tab:disabled { color: @muted; background: @raised; }
QTabBar::close-button { subcontrol-position: right; }
QTabWidget::pane { border: 1px solid @border; background: @surface; }
QScrollBar:vertical { background: @raised; width: 12px; margin: 0; border: none; }
QScrollBar:horizontal { background: @raised; height: 12px; margin: 0; border: none; }
QScrollBar::handle:vertical { background: @border; min-height: 25px; margin: 2px; border-radius: 2px; }
QScrollBar::handle:horizontal { background: @border; min-width: 25px; margin: 2px; border-radius: 2px; }
QScrollBar::handle:hover { background: @accent; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QSplitter::handle { background: @background; }
QSplitter::handle:hover { background: @accent; }
QSplitter::handle:horizontal { width: @splitterpx; }
QSplitter::handle:vertical { height: @splitterpx; }
QSplitter[windowSystemGroupSplitter="true"]::handle:horizontal { width: @groupSplitterpx; }
QSplitter[windowSystemGroupSplitter="true"]::handle:vertical { height: @groupSplitterpx; }
QSplitter[windowSystemGroupSplitter="true"]::handle { background: transparent; }
QSplitter[windowSystemGroupSplitter="true"]::handle:hover { background: transparent; }
QSplitter#PaneSplitter::handle { background: @border; }
QWidget#PaneHost QAbstractItemView, QWidget#PaneHost QPlainTextEdit, QWidget#PaneHost QTextEdit { border: none; }
QStatusBar, QWidget#StatusBar { background: @status; color: @statusText; border: none; padding: 1px 6px; }
QStatusBar QLabel, QWidget#StatusBar QLabel { color: @statusText; }
QStatusBar::item { border: none; }
QStatusBar QToolButton, QWidget#StatusBar QToolButton { color: @statusText; }
QWidget#StatusBar QToolButton#CustomStatusCommandItem { background: transparent; border: none; padding: 0 4px; min-height: 0; }
QWidget#StatusBar QToolButton#CustomStatusCommandItem:hover, QWidget#StatusBar QToolButton#CustomStatusCommandItem:focus { background: @soft; color: @text; }
QToolTip { background: @surface; color: @text; border: 1px solid @border; border-radius: @radiuspx; padding: 4px 6px; }
QCheckBox, QRadioButton { spacing: 5px; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; }
QCheckBox::indicator { background: @surface; border: 1px solid @border; border-radius: 0; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked, QCheckBox::indicator:indeterminate { background: @accent; border-color: @accent; }
QCheckBox::indicator:disabled { background: @raised; border-color: @border; }
QCheckBox::indicator:checked:disabled { background: @muted; }
QGroupBox { background: transparent; border: 1px solid @border; margin-top: 10px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 4px; left: 7px; }
QProgressBar { border: 1px solid @border; background: @raised; text-align: center; color: @text; }
QProgressBar::chunk { background: @accent; }
)qss");
    // Only authored dimensions are transformed. Resolved metric tokens below
    // already include interface zoom, and screen DPR is never applied here.
    if (!qFuzzyCompare(interfaceScale(), 1.0)) {
        static const QRegularExpression pixels(QStringLiteral("(-?\\d+(?:\\.\\d+)?)px\\b"));
        auto matches = pixels.globalMatch(rules);
        QVector<QRegularExpressionMatch> replacements;
        while (matches.hasNext()) replacements.append(matches.next());
        for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
            rules.replace(it->capturedStart(), it->capturedLength(), QString::number(uiPixelsF(it->captured(1).toDouble())) + QStringLiteral("px"));
    }
    const std::pair<const char*, QColor> colors[] = {
        {"@background", theme.background}, {"@surface", theme.surface},
        {"@raised", theme.raisedSurface}, {"@headerText", theme.headerText},
        {"@header", theme.headerBackground}, {"@text", theme.text},
        {"@muted", theme.mutedText}, {"@border", theme.border},
        {"@accent", theme.accent}, {"@buttonHover", theme.buttonHover}, {"@soft", theme.accentSoft},
        {"@active", theme.activeTab}, {"@inactive", theme.inactiveTab},
        {"@statusText", theme.statusText}, {"@status", theme.statusBackground},
        {"@selectionText", theme.selectionText}, {"@selection", theme.selection},
        {"@focus", theme.focus}, {"@danger", theme.danger}
    };
    // Long names precede their prefixes (headerText/header, selectionText/selection).
    for (const auto& [token, value] : colors) rules.replace(QString::fromLatin1(token), color(value));
    rules.replace(QStringLiteral("@radius"), QString::number(std::max(0, theme.metrics.cornerRadius)));
    rules.replace(QStringLiteral("@controlInner"), QString::number(std::max(14, theme.metrics.controlHeight - 6)));
    rules.replace(QStringLiteral("@tabInner"), QString::number(std::max(14, theme.metrics.tabHeight - 8)));
    rules.replace(QStringLiteral("@splitter"), QString::number(std::max(1, theme.metrics.splitterWidth)));
    rules.replace(QStringLiteral("@groupSplitter"), QString::number(std::max(1, theme.metrics.groupSplitterWidth)));
    return rules;
}

namespace {
// Qt reuses a visible tooltip for the next widget and re-polishes it without
// the window style sheet, dropping its padding and border, so a tip shown
// right after another one is smaller. Keep the tooltip rule in a marked block
// of the application style sheet as well; reused tips then keep the same
// style. Host rules outside the block are left untouched.
void applyApplicationToolTipStyle(const Theme& theme)
{
    if (!qApp) return;
    QString rule;
    for (const auto& line : styleSheet(theme).split(QLatin1Char('\n')))
        if (line.startsWith(QStringLiteral("QToolTip "))) { rule = line; break; }
    const QString begin = QStringLiteral("\n/* window_system tooltip */\n");
    const QString end = QStringLiteral("\n/* window_system tooltip end */");
    QString sheet = qApp->styleSheet();
    const auto first = sheet.indexOf(begin);
    const auto last = first < 0 ? -1 : sheet.indexOf(end, first);
    if (first >= 0 && last >= 0) sheet.remove(first, last + end.size() - first);
    sheet += begin + rule + end;
    // Setting the application style sheet re-polishes every widget: only do
    // it when the tooltip theme actually changed.
    if (sheet != qApp->styleSheet()) qApp->setStyleSheet(sheet);
}
} // namespace

void applyTheme(QWidget* root, const Theme& theme)
{
    if (!root) return;
    // QStyleSheetStyle can derive and replace widget palette roles during
    // polish. Keep the explicit paint tokens on the root for vector controls;
    // reparented content resolves the nearest root rather than keeping a style
    // pointer or stale colors from its previous window.
    root->setProperty("windowSystemThemeColors", QVariantMap{
        {"text", theme.text}, {"mutedText", theme.mutedText}, {"accent", theme.accent},
        {"surface", theme.surface}, {"accentSoft", theme.accentSoft}, {"buttonHover", theme.buttonHover},
        {"headerBackground", theme.headerBackground}, {"inactiveTab", theme.inactiveTab},
        {"edgeTabBackground", theme.edgeTabBackground}, {"edgeTabHover", theme.edgeTabHover}, {"edgeTabText", theme.edgeTabText},
        {"selection", theme.selection}, {"selectionText", theme.selectionText},
        {"checkedIndicatorText", theme.checkedIndicatorText}});
    QPalette palette = root->palette();
    palette.setColor(QPalette::Window, theme.background);
    palette.setColor(QPalette::WindowText, theme.text);
    palette.setColor(QPalette::Base, theme.surface);
    palette.setColor(QPalette::AlternateBase, theme.raisedSurface);
    palette.setColor(QPalette::Text, theme.text);
    palette.setColor(QPalette::Button, theme.raisedSurface);
    palette.setColor(QPalette::ButtonText, theme.text);
    palette.setColor(QPalette::BrightText, theme.checkedIndicatorText);
    palette.setColor(QPalette::Midlight, theme.accentSoft);
    palette.setColor(QPalette::Highlight, theme.selection);
    palette.setColor(QPalette::HighlightedText, theme.selectionText);
    palette.setColor(QPalette::Link, theme.accent);
    palette.setColor(QPalette::ToolTipBase, theme.surface);
    palette.setColor(QPalette::ToolTipText, theme.text);
    palette.setColor(QPalette::PlaceholderText, theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.mutedText);
    root->setPalette(palette);
    root->setAutoFillBackground(true);
    root->setStyleSheet(styleSheet(theme));
    applyApplicationToolTipStyle(theme);
    const auto updateSplitter = [&](QSplitter* splitter) {
        if (splitter && splitter->property("windowSystemGroupSplitter").toBool())
            splitter->setHandleWidth(std::max(1, theme.metrics.groupSplitterWidth));
        else if (splitter && splitter->objectName() == "PaneSplitter")
            splitter->setHandleWidth(std::max(1, theme.metrics.splitterWidth));
    };
    updateSplitter(qobject_cast<QSplitter*>(root));
    for (auto* splitter : root->findChildren<QSplitter*>()) updateSplitter(splitter);
    root->update();
}

} // namespace window_system
