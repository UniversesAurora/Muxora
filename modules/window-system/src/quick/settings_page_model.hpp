// SPDX-License-Identifier: MIT
#pragma once

#include "settings/setting_description.hpp"
#include "settings_style.hpp"
#include <window_system/content_options.hpp>
#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

class QQuickItem;

namespace window_system {
class WindowManager;
}

namespace window_system::quick {

// The Quick settings page's state, from presentation-free descriptions: the
// category tree, one entry per heading or setting in page order, each
// setting's current value and whether it can be edited, and the search.
class SettingsPageModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the Quick workspace")
    Q_PROPERTY(QVariantList categories READ categories NOTIFY filterChanged)
    Q_PROPERTY(QVariantList entries READ entries CONSTANT)
    Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QVariantList editable READ editable NOTIFY valuesChanged)
    Q_PROPERTY(QVariantList shown READ shown NOTIFY filterChanged)
    Q_PROPERTY(QString results READ results NOTIFY filterChanged)
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(window_system::quick::SettingsStyle* style READ style CONSTANT)
    Q_PROPERTY(QString pageTitle READ pageTitle CONSTANT)
    Q_PROPERTY(QString pageDescription READ pageDescription CONSTANT)
    Q_PROPERTY(QString searchPlaceholder READ searchPlaceholder CONSTANT)
public:
    SettingsPageModel(QVector<SettingCategory> categories, QVector<SettingDescription> settings, const Theme& theme,
                      QObject* parent = nullptr);

    QVariantList categories() const;
    QVariantList entries() const { return entries_; }
    QVariantList values() const { return values_; }
    QVariantList editable() const { return editable_; }
    QVariantList shown() const { return shown_; }
    QString results() const;
    QString current() const { return current_; }
    void setCurrent(const QString& category);
    SettingsStyle* style() const { return style_; }
    QString pageTitle() const;
    QString pageDescription() const;
    QString searchPlaceholder() const;

    Q_INVOKABLE void search(const QString& query);
    // A control's value for one setting, by its index in the settings.
    Q_INVOKABLE void write(int setting, const QVariant& value);
    // Applies a JSON setting's edited text: {applied, message}.
    Q_INVOKABLE QVariantMap apply(int setting, const QString& text);
    // Reads every setting again, after a change made elsewhere.
    Q_INVOKABLE void refresh();

signals:
    void valuesChanged();
    void filterChanged();
    void currentChanged();

private:
    QVector<SettingCategory> categories_;
    QVector<SettingDescription> settings_;
    SettingsStyle* style_;
    QVariantList entries_, values_, editable_, shown_;
    QVector<QString> searchable_;
    QString query_, current_;
    int count_ = 0;
};

// The built-in settings page as Quick content, or none for other types.
[[nodiscard]] QQuickItem* createSettingsContent(WindowManager& manager, const TabOptions& options, PaneId pane);

} // namespace window_system::quick
