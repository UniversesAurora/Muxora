// SPDX-License-Identifier: MIT
#pragma once

#include <QFont>
#include <QObject>
#include <QStringList>
#include <memory>
#include "ui_scale.hpp"

namespace window_system {

struct InterfaceAppearance {
    static constexpr int minimumFontSize = 6;
    static constexpr int maximumFontSize = 96;
    static constexpr double minimumZoomLevel = -8;
    static constexpr double maximumZoomLevel = 8;
    static constexpr int maximumFamilyText = 4096;
    // A CSS-style, comma-separated family list; an empty list uses the
    // platform's interface families. Terminal/editor fonts are independent.
    QString fontFamily;
    int fontSize = 13; // Logical px, before interface and screen scaling.
    double zoomLevel = 0;
    friend bool operator==(const InterfaceAppearance&, const InterfaceAppearance&) = default;
};

[[nodiscard]] bool validInterfaceAppearance(const InterfaceAppearance& value);
[[nodiscard]] double interfaceScaleForZoom(double level);
[[nodiscard]] QStringList interfaceFontFamilies(const QString& value);

// One application-wide controller. Own it before the WindowManagers and
// destroy it after them. It never reads or writes the host's settings files.
class InterfaceAppearanceController final : public QObject {
    Q_OBJECT
public:
    explicit InterfaceAppearanceController(QObject* parent = nullptr);
    ~InterfaceAppearanceController() override;
    [[nodiscard]] static InterfaceAppearanceController* applicationInstance();
    [[nodiscard]] InterfaceAppearance appearance() const;
    bool setAppearance(const InterfaceAppearance& value);
    [[nodiscard]] QFont font() const;
signals:
    void appearanceChanged();
private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace window_system
