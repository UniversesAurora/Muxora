// SPDX-License-Identifier: MIT
#pragma once

#include "commands/panel_rows.hpp"
#include <window_system/floating_panel.hpp>
#include <window_system/theme.hpp>
#include <QColor>
#include <QFont>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <functional>
#include <qqmlintegration.h>

class QQuickWindow;
class QWindow;

namespace window_system::quick {

// A search panel over a list, as the Widgets command palette and window
// selector: a frameless window centred on its owner, a search field, a
// list with column headers, a hint and two buttons. Up, Down, Page Up and
// Page Down move over the enabled rows; Enter or a double click accepts,
// Esc or an outside press (when the options say so) dismisses. It deletes
// itself once closed.
class CommandPanel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the Quick workspace")
    Q_PROPERTY(QStringList columns READ columns CONSTANT)
    Q_PROPERTY(QVariantList columnWidths READ columnWidths NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    Q_PROPERTY(bool canAccept READ canAccept NOTIFY currentChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY currentChanged)
    Q_PROPERTY(QString placeholder READ placeholder CONSTANT)
    Q_PROPERTY(QString acceptText READ acceptText CONSTANT)
    Q_PROPERTY(QString rejectText READ rejectText CONSTANT)
    Q_PROPERTY(QFont font READ font CONSTANT)
    Q_PROPERTY(qreal unit READ unit CONSTANT)
    Q_PROPERTY(qreal rowHeight READ rowHeight CONSTANT)
    Q_PROPERTY(qreal controlHeight READ controlHeight CONSTANT)
    Q_PROPERTY(QVariantMap colors READ colors CONSTANT)
public:
    struct Setup {
        QString name, title, placeholder, acceptText, rejectText;
        QStringList columns;
        QSize size;
        QString initial;
        // The hint under the list; for the palette it follows the selection.
        std::function<QString(const PanelRow* selected)> hint;
        std::function<QVector<PanelRow>(const QString& query)> rows;
        // Whether the selected row can be accepted now.
        std::function<bool(const PanelRow& row)> acceptable;
        std::function<void(const QString& id)> accepted;
    };
    CommandPanel(QWindow* owner, const Theme& theme, const FloatingPanelOptions& options, Setup setup);
    ~CommandPanel() override;
    [[nodiscard]] QQuickWindow* window() const;
    void refresh();

    QStringList columns() const { return setup_.columns; }
    QVariantList columnWidths() const { return columnWidths_; }
    QVariantList rows() const { return rowValues_; }
    int current() const { return current_; }
    bool canAccept() const;
    QString hint() const;
    QString placeholder() const { return setup_.placeholder; }
    QString acceptText() const { return setup_.acceptText; }
    QString rejectText() const { return setup_.rejectText; }
    QFont font() const { return font_; }
    qreal unit() const;
    qreal rowHeight() const;
    qreal controlHeight() const;
    QVariantMap colors() const;

    Q_INVOKABLE void search(const QString& query);
    Q_INVOKABLE void move(int step);
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void accept();
    Q_INVOKABLE void reject();

signals:
    void rowsChanged();
    void currentChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setCurrent(int index);
    void close();
    Setup setup_;
    Theme theme_;
    FloatingPanelOptions options_;
    QFont font_;
    QString query_;
    QVector<PanelRow> rows_;
    QVariantList rowValues_, columnWidths_;
    int current_ = -1;
    QPointer<QQuickWindow> window_;
    bool closing_ = false;
};

} // namespace window_system::quick
