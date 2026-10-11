#pragma once
#include <QFrame>
#include <QJsonObject>
#include <QHash>
#include <QVector>

class QHBoxLayout;
namespace window_system {
class CommandStripHost final : public QFrame {
public:
    explicit CommandStripHost(QWidget* parent = nullptr);
    void addStrip(QString id, QWidget* content, int row);
    void setLocked(bool locked);
    [[nodiscard]] bool isLocked() const;
    [[nodiscard]] QJsonObject saveLayout() const;
    void restoreLayout(const QJsonObject& json);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
private:
    void arrange();
    QHash<QString, QWidget*> strips_;
    QVector<QString> rows_[2];
    QHBoxLayout* layouts_[2];
    bool locked_ = true;
};
} // namespace window_system
