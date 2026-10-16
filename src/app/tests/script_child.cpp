// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 2)
        return 2;
    QFile file(args[1]);
    if (!file.open(QIODevice::WriteOnly))
        return 3;
    QJsonArray values;
    for (int i = 2; i < args.size(); ++i)
        values.append(args[i]);
    const auto bytes = QJsonDocument(values).toJson();
    return file.write(bytes) == bytes.size() ? 0 : 4;
}
