// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 2)
        return 2;
    if (args[1] == QStringLiteral("--wait-for-release")) {
        if (args.size() != 4)
            return 2;
        // Publish a complete PID atomically. The test retains this process's
        // native handle until release has been observed and it has exited.
        QSaveFile marker(args[2]);
        const auto identity = QByteArray::number(QCoreApplication::applicationPid());
        if (!marker.open(QIODevice::WriteOnly) || marker.write(identity) != identity.size() ||
            !marker.commit())
            return 3;
        // The parent owns the release file and always creates it, including
        // after a failed assertion. A hard limit also bounds failed fixtures.
        for (int i = 0; i < 3000 && !QFile::exists(args[3]); ++i)
            QThread::msleep(10);
        return QFile::exists(args[3]) ? 0 : 4;
    }
    QFile file(args[1]);
    if (!file.open(QIODevice::WriteOnly))
        return 3;
    QJsonArray values;
    for (int i = 2; i < args.size(); ++i)
        values.append(args[i]);
    const auto bytes = QJsonDocument(values).toJson();
    return file.write(bytes) == bytes.size() ? 0 : 4;
}
