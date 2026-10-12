#include "state_paths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace muxora {

QString resolveStateDirectory(const QString& explicitDirectory, const QString& executableDirectory)
{
    if (!explicitDirectory.isEmpty()) return explicitDirectory;

    const auto portableDirectory = QDir(executableDirectory).filePath("data");
    if (QFileInfo(portableDirectory).isDir()) return portableDirectory;

    return QDir(QStandardPaths::writableLocation(QStandardPaths::StateLocation))
        .filePath("window-preview");
}

} // namespace muxora
