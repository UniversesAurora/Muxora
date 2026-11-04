#pragma once
#include <window_system/content_options.hpp>
#include <QFont>
#include <QStringList>

namespace window_system { class WindowManager; }

namespace muxora {
// Text and initial layout of the preview's Quick placeholder pages.
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
QString fileAddressText();
QString fileInformationText(int count);
QString transfersText();

// The Quick workspace creates content from the tab options.
window_system::TabId addPlaceholderTab(window_system::WindowManager& windows, window_system::WindowId window,
                                       const window_system::TabOptions& options, window_system::GroupId group = {});
void populateInitialWorkspace(window_system::WindowManager& windows,
                              window_system::WindowId window);
} // namespace muxora
