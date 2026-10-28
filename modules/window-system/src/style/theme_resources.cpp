// SPDX-License-Identifier: MIT
#include <window_system/theme.hpp>

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <utility>

namespace window_system {
namespace {
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
        QStringLiteral("chevron-up"), QStringLiteral("chevron-down"), QStringLiteral("sticky"),
        QStringLiteral("sticky-vertical"), QStringLiteral("tab-list")
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
        } else if (name_ == QStringLiteral("tab-list")) {
            // All tabs: a short list, distinct from the dropdown arrows.
            for (const qreal y : {4.5, 8.0, 11.5}) {
                painter->drawLine(QPointF(3, y), QPointF(4, y));
                painter->drawLine(QPointF(6, y), QPointF(13, y));
            }
        } else if (name_ == QStringLiteral("sticky") || name_ == QStringLiteral("sticky-vertical")) {
            // Kept at the front: a bar with an arrow pointing to it. It is
            // not the pin of pinned sidebars (tab strip design 5).
            painter->save();
            if (name_ == QStringLiteral("sticky-vertical")) { painter->translate(8, 8); painter->rotate(90); painter->translate(-8, -8); }
            painter->drawLine(QPointF(3.5, 3), QPointF(3.5, 13));
            painter->drawLine(QPointF(13, 8), QPointF(6.5, 8));
            painter->drawPolyline(QPolygonF{{9.5, 5}, {6.5, 8}, {9.5, 11}});
            painter->restore();
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

QIcon iconFor(QString name, const Theme& theme, QSize size)
{
    name = canonicalIconName(std::move(name));
    if (!supportedIcon(name)) return {};
    if (size.isEmpty()) size = QSize(theme.metrics.iconSize, theme.metrics.iconSize);
    return QIcon(new VectorIconEngine(std::move(name), theme, size));
}

} // namespace window_system
