// SPDX-License-Identifier: MIT
#include "window_system/theme.hpp"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace window_system {
namespace {

QString color(const QColor& value)
{
    return value.name(QColor::HexRgb);
}

QString canonicalIconName(QString name)
{
    name = name.trimmed().toLower();
    if (name == QStringLiteral("x")) return QStringLiteral("close");
    if (name == QStringLiteral("new")) return QStringLiteral("add");
    if (name == QStringLiteral("folder")) return QStringLiteral("files");
    if (name == QStringLiteral("float")) return QStringLiteral("detach");
    if (name == QStringLiteral("promote")) return QStringLiteral("window");
    if (name == QStringLiteral("dock")) return QStringLiteral("return");
    return name;
}

bool supportedIcon(const QString& name)
{
    static const QStringList names = {
        QStringLiteral("close"), QStringLiteral("minimize"),
        QStringLiteral("maximize"), QStringLiteral("restore"), QStringLiteral("add"),
        QStringLiteral("terminal"), QStringLiteral("files"), QStringLiteral("settings"),
        QStringLiteral("search"), QStringLiteral("pin"), QStringLiteral("unpin"),
        QStringLiteral("pin-all"), QStringLiteral("unpin-all"),
        QStringLiteral("lock"), QStringLiteral("unlock"), QStringLiteral("split-horizontal"),
        QStringLiteral("split-vertical"), QStringLiteral("detach"), QStringLiteral("window"),
        QStringLiteral("return"), QStringLiteral("save"), QStringLiteral("command"),
        QStringLiteral("help"), QStringLiteral("chevron-left"), QStringLiteral("chevron-right"),
        QStringLiteral("chevron-up"), QStringLiteral("chevron-down")
    };
    return names.contains(name);
}

class VectorIconEngine final : public QIconEngine {
public:
    VectorIconEngine(QString name, Theme theme, QSize preferredSize)
        : name_(std::move(name)), theme_(std::move(theme)), preferredSize_(preferredSize)
    {
    }

    [[nodiscard]] QIconEngine* clone() const override
    {
        return new VectorIconEngine(name_, theme_, preferredSize_);
    }

    [[nodiscard]] QString key() const override { return QStringLiteral("WindowSystemVector"); }
    [[nodiscard]] QString iconName() override { return name_; }
    [[nodiscard]] bool isNull() override { return false; }

    [[nodiscard]] QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override
    {
        return {preferredSize_};
    }

    [[nodiscard]] QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    [[nodiscard]] QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode,
                                       QIcon::State state, qreal scale) override
    {
        if (size.isEmpty() || !std::isfinite(scale) || scale <= 0.0) return {};
        const QSize pixels(std::max(1, qRound(size.width() * scale)),
                           std::max(1, qRound(size.height() * scale)));
        QPixmap result(pixels);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return result;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        if (!painter || rect.isEmpty()) return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal side = std::min(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2.0,
                           rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 16.0, side / 16.0);
        QColor stroke = theme_.text;
        if (mode == QIcon::Disabled) stroke = theme_.mutedText;
        else if (mode == QIcon::Selected) stroke = theme_.selectionText;
        else if (mode == QIcon::Active || state == QIcon::On) stroke = theme_.accent;
        painter->setPen(QPen(stroke, 1.25, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        painter->setBrush(Qt::NoBrush);

        if (name_ == QStringLiteral("close")) {
            painter->drawLine(QPointF(4, 4), QPointF(12, 12));
            painter->drawLine(QPointF(12, 4), QPointF(4, 12));
        } else if (name_ == QStringLiteral("minimize")) {
            painter->drawLine(QPointF(3, 10), QPointF(13, 10));
        } else if (name_ == QStringLiteral("maximize")) {
            painter->drawRect(QRectF(3.5, 3.5, 9, 9));
        } else if (name_ == QStringLiteral("restore")) {
            painter->drawRect(QRectF(3.5, 5.5, 7, 7));
            painter->drawLine(QPointF(5.5, 3.5), QPointF(12.5, 3.5));
            painter->drawLine(QPointF(12.5, 3.5), QPointF(12.5, 10.5));
        } else if (name_ == QStringLiteral("add")) {
            painter->drawLine(QPointF(3, 8), QPointF(13, 8));
            painter->drawLine(QPointF(8, 3), QPointF(8, 13));
        } else if (name_ == QStringLiteral("terminal")) {
            painter->drawRoundedRect(QRectF(2, 3, 12, 10), 1, 1);
            painter->drawPolyline(QPolygonF{{4, 6}, {6, 8}, {4, 10}});
            painter->drawLine(QPointF(8, 10), QPointF(11, 10));
        } else if (name_ == QStringLiteral("files")) {
            painter->drawPolyline(QPolygonF{{2, 12.5}, {2, 3.5}, {6, 3.5}, {8, 5.5}, {14, 5.5}, {14, 12.5}, {2, 12.5}});
            painter->drawLine(QPointF(2, 7.5), QPointF(14, 7.5));
        } else if (name_ == QStringLiteral("settings")) {
            for (int index = 0; index < 8; ++index) {
                painter->save();
                painter->translate(8, 8);
                painter->rotate(index * 45);
                painter->drawLine(QPointF(0, -4.5), QPointF(0, -6));
                painter->restore();
            }
            painter->drawEllipse(QRectF(3.5, 3.5, 9, 9));
            painter->drawEllipse(QRectF(6, 6, 4, 4));
        } else if (name_ == QStringLiteral("search")) {
            painter->drawEllipse(QRectF(2.5, 2.5, 7.5, 7.5));
            painter->drawLine(QPointF(9, 9), QPointF(13, 13));
        } else if (name_ == QStringLiteral("pin") || name_ == QStringLiteral("unpin") ||
                   name_ == QStringLiteral("pin-all") || name_ == QStringLiteral("unpin-all")) {
            painter->save();
            if (name_.endsWith(QStringLiteral("-all"))) painter->scale(0.8, 0.8);
            if (name_.startsWith(QStringLiteral("unpin"))) {
                painter->translate(8, 8);
                painter->rotate(45);
                painter->translate(-8, -8);
            }
            painter->drawPolyline(QPolygonF{{5, 3}, {11, 3}, {10, 5}, {10, 8}, {12, 10}, {4, 10}, {6, 8}, {6, 5}, {5, 3}});
            painter->drawLine(QPointF(8, 10), QPointF(8, 14));
            painter->restore();
            if (name_.endsWith(QStringLiteral("-all"))) {
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(QRectF(11, 10, 3.5, 3.5));
                painter->drawRect(QRectF(9.5, 11.5, 3.5, 3.5));
            }
        } else if (name_ == QStringLiteral("lock") || name_ == QStringLiteral("unlock")) {
            painter->drawRect(QRectF(3.5, 7.5, 9, 6));
            QPainterPath shackle;
            shackle.moveTo(5, 7);
            shackle.lineTo(5, 5);
            shackle.cubicTo(5, 1, 11, 1, 11, 5);
            if (name_ == QStringLiteral("lock")) shackle.lineTo(11, 7);
            painter->drawPath(shackle);
            painter->drawLine(QPointF(8, 10), QPointF(8, 11.5));
        } else if (name_ == QStringLiteral("split-horizontal") || name_ == QStringLiteral("split-vertical")) {
            painter->drawRect(QRectF(2.5, 3.5, 11, 9));
            if (name_ == QStringLiteral("split-horizontal")) painter->drawLine(QPointF(8, 4), QPointF(8, 12));
            else painter->drawLine(QPointF(3, 8), QPointF(13, 8));
        } else if (name_ == QStringLiteral("detach")) {
            painter->drawPolyline(QPolygonF{{11, 9}, {11, 13}, {3, 13}, {3, 5}, {7, 5}});
            painter->drawPolyline(QPolygonF{{9, 3}, {13, 3}, {13, 7}});
            painter->drawLine(QPointF(8, 8), QPointF(13, 3));
        } else if (name_ == QStringLiteral("window")) {
            painter->drawRect(QRectF(2.5, 3.5, 11, 9));
            painter->drawLine(QPointF(3, 6), QPointF(13, 6));
        } else if (name_ == QStringLiteral("return")) {
            painter->drawPolyline(QPolygonF{{6, 4}, {2, 8}, {6, 12}});
            painter->drawPolyline(QPolygonF{{2, 8}, {11, 8}, {11, 4}});
        } else if (name_ == QStringLiteral("save")) {
            painter->drawPolyline(QPolygonF{{3, 2.5}, {11, 2.5}, {13, 4.5}, {13, 13.5}, {3, 13.5}, {3, 2.5}});
            painter->drawRect(QRectF(5, 2.5, 5, 4));
            painter->drawRect(QRectF(5, 9, 6, 4.5));
        } else if (name_ == QStringLiteral("command")) {
            painter->drawPolyline(QPolygonF{{3, 4}, {7, 8}, {3, 12}});
            painter->drawLine(QPointF(8, 12), QPointF(13, 12));
        } else if (name_ == QStringLiteral("help")) {
            painter->drawEllipse(QRectF(2, 2, 12, 12));
            QPainterPath question;
            question.moveTo(6, 6);
            question.cubicTo(6, 3.5, 11, 3.5, 10, 6.5);
            question.cubicTo(9.5, 8, 8, 7.5, 8, 9);
            painter->drawPath(question);
            painter->drawPoint(QPointF(8, 11));
        } else {
            painter->translate(8, 8);
            if (name_ == QStringLiteral("chevron-left")) painter->rotate(90);
            else if (name_ == QStringLiteral("chevron-right")) painter->rotate(-90);
            else if (name_ == QStringLiteral("chevron-up")) painter->rotate(180);
            painter->drawPolyline(QPolygonF{{-4, -2}, {0, 2}, {4, -2}});
        }
        painter->restore();
    }

private:
    QString name_;
    Theme theme_;
    QSize preferredSize_;
};

} // namespace

Theme defaultTheme()
{
    return {};
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
QWidget#WindowHeader, QWidget#CommandStripHost { background: @header; color: @headerText; }
QWidget#WindowHeader QLabel { color: @headerText; }
QWidget#TabGroup { background: @background; }
QWidget#TabPage, QWidget#PaneHost { background: @surface; }
QWidget#PaneHeader { background: @raised; border-bottom: 1px solid @border; }
QWidget#PaneHeader[active="true"] { background: @soft; border-bottom: 1px solid @accent; }
QWidget#EdgeBar { background: @background; }
QWidget#PeekPanel { background: @surface; border: 1px solid @accent; }
QWidget#DockOverlay { background: transparent; }
QWidget#CommandPalette { background: @surface; border: 1px solid @accent; }
QLabel[role="hint"] { color: @muted; }
QLabel[role="heading"] { color: @accent; font-weight: 600; }
QPushButton, QToolButton { background: @raised; border: 1px solid @border; border-radius: @radiuspx; padding: 2px 7px; min-height: @controlInnerpx; }
QToolButton { background: transparent; border-color: transparent; padding: 2px 4px; }
QPushButton:hover, QToolButton:hover { background: @soft; border-color: @accent; }
QPushButton:pressed, QToolButton:pressed, QToolButton:checked { background: @selection; border-color: @accent; }
QPushButton:focus, QToolButton:focus { border-color: @focus; }
QPushButton:default { border-color: @accent; }
QPushButton:disabled, QToolButton:disabled { color: @muted; border-color: @border; background: @raised; }
QToolButton#CaptionButton { color: @headerText; background: transparent; border: none; border-radius: 0; padding: 0; }
QToolButton#CaptionButton:hover { background: @soft; }
QToolButton#CaptionButton[role="close"]:hover { background: @danger; color: white; }
QToolButton#EdgeButton { color: @statusText; border: none; border-radius: 0; padding: 3px 6px; }
QToolButton#EdgeButton:hover, QToolButton#EdgeButton:checked { background: @accent; color: white; }
QMenuBar, QMenuBar#MenuStrip { background: transparent; border: none; padding: 0; color: @headerText; }
QMenuBar::item { background: transparent; padding: 5px 8px; }
QMenuBar::item:selected { background: @soft; }
QMenuBar::item:pressed { background: @selection; }
QMenu { background: @surface; color: @text; border: 1px solid @border; padding: 3px; }
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
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit { background: @surface; color: @text; border: 1px solid @border; border-radius: @radiuspx; padding: 2px 5px; selection-background-color: @selection; selection-color: @selectionText; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height: @controlInnerpx; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: @focus; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background: @raised; color: @muted; }
QComboBox { padding-right: 22px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 19px; border: none; }
QSpinBox, QDoubleSpinBox { padding-right: 20px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: padding; subcontrol-position: top right; width: 18px; border: 1px solid transparent; background: transparent; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: padding; subcontrol-position: bottom right; width: 18px; border: 1px solid transparent; background: transparent; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { border-color: @accent; background: @soft; }
QComboBox QAbstractItemView { background: @surface; color: @text; border: 1px solid @border; selection-background-color: @selection; selection-color: @selectionText; }
QAbstractItemView { background: @surface; alternate-background-color: @raised; color: @text; border: 1px solid @border; selection-background-color: @selection; selection-color: @selectionText; outline: 0; }
QAbstractItemView::item { padding: 3px 4px; min-height: 18px; }
QAbstractItemView::item:hover { background: @soft; }
QAbstractItemView::item:selected { background: @selection; color: @selectionText; }
QAbstractItemView:focus { border-color: @focus; }
QHeaderView::section { background: @raised; color: @text; padding: 4px 6px; border: none; border-right: 1px solid @border; border-bottom: 1px solid @border; }
QTabBar, QTabBar#TabBar { background: @background; }
QTabBar::tab { background: @inactive; color: @text; border: 1px solid @border; border-bottom: 2px solid @border; padding: 3px 11px; min-height: @tabInnerpx; margin-right: 1px; }
QTabBar::tab:hover { background: @soft; }
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
QStatusBar, QWidget#StatusBar { background: @status; color: @statusText; border: none; padding: 1px 6px; }
QStatusBar QLabel, QWidget#StatusBar QLabel { color: @statusText; }
QStatusBar::item { border: none; }
QStatusBar QToolButton, QWidget#StatusBar QToolButton { color: @statusText; }
QToolTip { background: @surface; color: @text; border: 1px solid @border; padding: 4px 6px; }
QCheckBox, QRadioButton { spacing: 5px; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; }
QCheckBox::indicator { background: @surface; border: 1px solid @border; border-radius: @radiuspx; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked, QCheckBox::indicator:indeterminate { background: @accent; border-color: @accent; }
QCheckBox::indicator:disabled { background: @raised; border-color: @border; }
QCheckBox::indicator:checked:disabled { background: @muted; }
QGroupBox { background: transparent; border: 1px solid @border; margin-top: 10px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 4px; left: 7px; }
QProgressBar { border: 1px solid @border; background: @raised; text-align: center; color: @text; }
QProgressBar::chunk { background: @accent; }
)qss");
    const std::pair<const char*, QColor> colors[] = {
        {"@background", theme.background}, {"@surface", theme.surface},
        {"@raised", theme.raisedSurface}, {"@headerText", theme.headerText},
        {"@header", theme.headerBackground}, {"@text", theme.text},
        {"@muted", theme.mutedText}, {"@border", theme.border},
        {"@accent", theme.accent}, {"@soft", theme.accentSoft},
        {"@active", theme.activeTab}, {"@inactive", theme.inactiveTab},
        {"@statusText", theme.statusText}, {"@status", theme.statusBackground},
        {"@selectionText", theme.selectionText}, {"@selection", theme.selection},
        {"@focus", theme.focus}, {"@danger", theme.danger}
    };
    // Long names precede their prefixes (headerText/header, selectionText/selection).
    for (const auto& [token, value] : colors) rules.replace(QString::fromLatin1(token), color(value));
    rules.replace(QStringLiteral("@radius"), QString::number(std::max(0, theme.metrics.cornerRadius)));
    rules.replace(QStringLiteral("@controlInner"), QString::number(std::max(14, theme.metrics.controlHeight - 6)));
    rules.replace(QStringLiteral("@tabInner"), QString::number(std::max(14, theme.metrics.tabHeight - 9)));
    rules.replace(QStringLiteral("@splitter"), QString::number(std::max(1, theme.metrics.splitterWidth)));
    return rules;
}

void applyTheme(QWidget* root, const Theme& theme)
{
    if (!root) return;
    // QStyleSheetStyle can derive and replace widget palette roles during
    // polish. Keep the explicit paint tokens on the root for vector controls;
    // reparented content resolves the nearest root rather than keeping a style
    // pointer or stale colors from its previous window.
    root->setProperty("windowSystemThemeColors", QVariantMap{
        {"text", theme.text}, {"mutedText", theme.mutedText}, {"accent", theme.accent},
        {"accentSoft", theme.accentSoft}, {"selection", theme.selection},
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
    root->update();
}

QIcon iconFor(QString name, const Theme& theme, QSize size)
{
    name = canonicalIconName(std::move(name));
    if (!supportedIcon(name)) return {};
    if (size.isEmpty()) size = QSize(theme.metrics.iconSize, theme.metrics.iconSize);
    return QIcon(new VectorIconEngine(std::move(name), theme, size));
}

} // namespace window_system
