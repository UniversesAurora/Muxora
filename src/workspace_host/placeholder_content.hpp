#pragma once
#include <window_system/window_manager.hpp>
#include <QFont>

namespace muxora {
// What the preview's placeholders say, shared by the Widgets and Quick pages.
QString notesText();
QFont notesFont();
QString welcomeSubtitle();
QString welcomeDescription();
QString welcomeTip();
QVector<QPair<QString, QString>> welcomeActions(); // Label, command.
QString sessionsHeading();
QStringList sessionNames();
QStringList fileColumns();
QVector<QStringList> fileRows(); // Directories first, by column.
QString transfersText();

QWidget* createPlaceholder(const window_system::TabOptions& options,
                           window_system::WindowManager* windows);
// Adds a placeholder tab; the Widgets presentation needs its content now,
// the Quick workspace creates it from the options.
window_system::TabId addPlaceholderTab(window_system::WindowManager& windows, window_system::WindowId window,
                                       const window_system::TabOptions& options, window_system::GroupId group = {});
void populateInitialWorkspace(window_system::WindowManager& windows,
                              window_system::WindowId window);
} // namespace muxora
