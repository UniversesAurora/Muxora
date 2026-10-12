#include "state_paths.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class StatePathsTest : public QObject {
    Q_OBJECT
private slots:
    void portableDirectoryIsRelativeToExecutable();
    void ordinaryBuildUsesSystemStateDirectory();
    void explicitDirectoryTakesPriority();
    void dataFileDoesNotEnablePortableMode();
};

void StatePathsTest::portableDirectoryIsRelativeToExecutable()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto executableDirectory = temporary.filePath("portable application");
    const auto dataDirectory = QDir(executableDirectory).filePath("data");
    QVERIFY(QDir().mkpath(dataDirectory));

    QCOMPARE(muxora::resolveStateDirectory({}, executableDirectory), dataDirectory);
}

void StatePathsTest::ordinaryBuildUsesSystemStateDirectory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(!QFile::exists(temporary.filePath("data")));

    const auto expected = QDir(QStandardPaths::writableLocation(QStandardPaths::StateLocation))
        .filePath("window-preview");
    QCOMPARE(muxora::resolveStateDirectory({}, temporary.path()), expected);
    QVERIFY(!QFile::exists(temporary.filePath("data")));
}

void StatePathsTest::explicitDirectoryTakesPriority()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QDir().mkpath(temporary.filePath("data")));
    const auto explicitDirectory = temporary.filePath("explicit state");

    QCOMPARE(muxora::resolveStateDirectory(explicitDirectory, temporary.path()), explicitDirectory);
    QCOMPARE(muxora::resolveStateDirectory("relative-state", temporary.path()), QString("relative-state"));
    QVERIFY(!QFile::exists(explicitDirectory));
}

void StatePathsTest::dataFileDoesNotEnablePortableMode()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QFile data(temporary.filePath("data"));
    QVERIFY(data.open(QIODevice::WriteOnly));
    data.close();

    const auto expected = QDir(QStandardPaths::writableLocation(QStandardPaths::StateLocation))
        .filePath("window-preview");
    QCOMPARE(muxora::resolveStateDirectory({}, temporary.path()), expected);
}

QTEST_GUILESS_MAIN(StatePathsTest)
#include "state_paths_test.moc"
