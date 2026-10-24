// SPDX-License-Identifier: MIT
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QTranslator>
#include <window_system/localization.hpp>

namespace translation_test {

// These fixtures intentionally exercise Chinese labels, search terms and
// geometry. Make that locale explicit instead of relying on the source text.
// A raw translator leaves the host language-preference controller unchanged.
class ChineseUiCatalog final {
public:
    bool install()
    {
        window_system::TranslationManager resources;
        return translator_.load(QStringLiteral(":/window_system/i18n/window_system_zh_CN.qm"))
            && QCoreApplication::installTranslator(&translator_);
    }
    ~ChineseUiCatalog() { QCoreApplication::removeTranslator(&translator_); }
private:
    QTranslator translator_;
};

struct Message {
    QString context, source, comment, type;
    QStringList forms;
    bool plural = false;
};
struct Catalog {
    QString sourceLanguage, language, error;
    QList<Message> messages;
};

inline QString repositoryPath()
{
    QDir root(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    root.cdUp(); root.cdUp(); root.cdUp();
    return root.absolutePath();
}

inline Catalog readCatalog(const QString& path)
{
    Catalog result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = file.errorString(); return result; }
    QXmlStreamReader xml(&file);
    QString context;
    Message message;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QStringLiteral("message")) result.messages.append(message);
        if (!xml.isStartElement()) continue;
        if (xml.name() == QStringLiteral("TS")) {
            result.sourceLanguage = xml.attributes().value(QStringLiteral("sourcelanguage")).toString();
            result.language = xml.attributes().value(QStringLiteral("language")).toString();
        } else if (xml.name() == QStringLiteral("name")) context = xml.readElementText();
        else if (xml.name() == QStringLiteral("message")) {
            message = {}; message.context = context;
            message.plural = xml.attributes().value(QStringLiteral("numerus")) == QStringLiteral("yes");
        } else if (xml.name() == QStringLiteral("source")) message.source = xml.readElementText();
        else if (xml.name() == QStringLiteral("comment")) message.comment = xml.readElementText();
        else if (xml.name() == QStringLiteral("translation")) {
            message.type = xml.attributes().value(QStringLiteral("type")).toString();
            if (message.plural) {
                while (xml.readNextStartElement()) {
                    if (xml.name() == QStringLiteral("numerusform")) message.forms.append(xml.readElementText());
                    else xml.skipCurrentElement();
                }
            } else message.forms.append(xml.readElementText());
        }
    }
    if (xml.hasError()) result.error = xml.errorString();
    return result;
}

inline QString validateCatalog(const Catalog& catalog)
{
    if (!catalog.error.isEmpty()) return catalog.error;
    if (catalog.messages.isEmpty()) return QStringLiteral("No messages");
    const auto placeholders = [](const QString& text) {
        QStringList result;
        auto matches = QRegularExpression(QStringLiteral("%L?(?:[1-9][0-9]?|n)")).globalMatch(text);
        while (matches.hasNext()) result.append(matches.next().captured());
        result.sort(); return result;
    };
    for (const auto& message : catalog.messages) {
        if (!message.type.isEmpty()) return QStringLiteral("Unfinished: ") + message.source;
        const int expected = message.plural && catalog.language == QStringLiteral("en") ? 2 : 1;
        if (message.forms.size() != expected) return QStringLiteral("Missing plural forms: ") + message.source;
        for (const auto& form : message.forms) {
            if (form.isEmpty()) return QStringLiteral("Empty: ") + message.source;
            if (placeholders(form) != placeholders(message.source)) return QStringLiteral("Placeholders: ") + message.source;
        }
    }
    return {};
}

inline QString checkSourceCoverage(const QString& catalog)
{
    const auto program = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
    if (program.isEmpty()) return QStringLiteral("pwsh is required for source extraction coverage");
    QProcess checker;
    checker.setProcessChannelMode(QProcess::MergedChannels);
    checker.start(program, {QStringLiteral("-NoProfile"), QStringLiteral("-File"),
        QDir(repositoryPath()).filePath(QStringLiteral("tools/check-translations.ps1")),
        QStringLiteral("-Catalog"), catalog, QStringLiteral("-QtRoot"), QLibraryInfo::path(QLibraryInfo::PrefixPath)});
    if (!checker.waitForStarted()) return checker.errorString();
    if (!checker.waitForFinished(30'000)) {
        checker.kill(); checker.waitForFinished();
        return QStringLiteral("Source extraction coverage timed out");
    }
    if (checker.exitStatus() != QProcess::NormalExit || checker.exitCode() != 0)
        return QString::fromUtf8(checker.readAll());
    return {};
}

} // namespace translation_test
