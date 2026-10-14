// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

namespace window_system {

struct FloatingPanelOptions {
    bool dismissOnOutsidePress = true;
    bool modal = false;
};

// Reusable transient panel. show() returns immediately for both policies;
// modal panels block only their owner, without starting a nested event loop.
// Qt parent ownership applies. An outside press is delivered to its original
// target after a non-modal panel closes; it is never replayed or synthesized.
class FloatingPanel final : public QDialog {
    Q_OBJECT
public:
    explicit FloatingPanel(QWidget* parent = nullptr, const FloatingPanelOptions& options = {});
    ~FloatingPanel() override;
    void setOptions(const FloatingPanelOptions& options);
    [[nodiscard]] FloatingPanelOptions options() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    [[nodiscard]] bool ownsWidget(const QWidget* widget) const;
    void observeApplication(bool observe);
    FloatingPanelOptions options_;
    bool observing_ = false;
};

} // namespace window_system
