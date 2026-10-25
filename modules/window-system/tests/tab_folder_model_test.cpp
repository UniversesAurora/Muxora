// SPDX-License-Identifier: MIT
// Tab folders in the layout model (tab strip design 12): membership, the
// adjacency invariant under every operation, carrying folders between
// groups and windows, and persistence.
#include <window_system/layout_model.hpp>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QTest>
using namespace window_system;

namespace {
struct Strip {
    LayoutModel model;
    WindowId window;
    GroupId group;
    QVector<TabId> tabs;
};
Strip strip(int count, int sticky = 0)
{
    Strip result;
    result.window = result.model.createWindow();
    for (int index = 0; index < count; ++index)
        result.tabs.append(result.model.addTab(result.window, QString(QChar('a' + index))));
    result.group = result.model.groupForTab(result.tabs.first());
    for (int index = 0; index < sticky; ++index) result.model.setTabSticky(result.tabs.at(index), true);
    return result;
}
// The strip as letters, folder tabs bracketed: "a[bc]d".
QString shape(const LayoutModel& model, GroupId groupId)
{
    QString text;
    FolderId open;
    for (const auto id : model.group(groupId)->tabs) {
        const auto* tab = model.tab(id);
        if (tab->folder != open) {
            if (!open.isNull()) text += ']';
            if (!tab->folder.isNull()) text += '[';
            open = tab->folder;
        }
        text += tab->title;
    }
    if (!open.isNull()) text += ']';
    return text;
}
QVector<TabId> pick(const Strip& s, const QString& letters)
{
    QVector<TabId> result;
    for (const auto letter : letters) result.append(s.tabs.at(letter.unicode() - 'a'));
    return result;
}
}

class TabFolderModelTest final : public QObject {
    Q_OBJECT
private slots:
    void creatingGathersTabsAtTheFirstOnesPlace_data()
    {
        QTest::addColumn<QString>("chosen"); QTest::addColumn<QString>("expected");
        QTest::newRow("one") << "c" << "ab[c]de";
        QTest::newRow("adjacent") << "bc" << "a[bc]de";
        QTest::newRow("apart") << "be" << "a[be]cd";
        QTest::newRow("listed out of order") << "db" << "a[bd]ce";
        QTest::newRow("all") << "abcde" << "[abcde]";
    }
    void creatingGathersTabsAtTheFirstOnesPlace()
    {
        QFETCH(QString, chosen); QFETCH(QString, expected);
        auto s = strip(5);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, chosen), id, "work", "blue"));
        QCOMPARE(shape(s.model, s.group), expected);
        QCOMPARE(s.model.folder(id)->name, QString("work"));
        QCOMPARE(s.model.folder(id)->colorId, QString("blue"));
        QCOMPARE(s.model.groupForFolder(id), s.group);
    }
    void creatingRejectsWhatCannotBeAFolder()
    {
        auto s = strip(3, 1);
        const auto other = s.model.createWindow();
        const auto stranger = s.model.addTab(other, "x");
        QVERIFY(!s.model.createFolder({}, QUuid::createUuid()));
        QVERIFY(!s.model.createFolder({s.tabs.at(1)}, {}));
        QVERIFY(!s.model.createFolder({s.tabs.at(0)}, QUuid::createUuid()));            // sticky
        QVERIFY(!s.model.createFolder({s.tabs.at(1), stranger}, QUuid::createUuid()));  // two groups
        QVERIFY(!s.model.createFolder({s.tabs.at(1), s.tabs.at(1)}, QUuid::createUuid()));
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder({s.tabs.at(1)}, id));
        QVERIFY(!s.model.createFolder({s.tabs.at(2)}, id));                               // identity in use
        QVERIFY(s.model.moveTabToEdge(s.tabs.at(2), Edge::Left, false));
        QVERIFY(!s.model.createFolder({s.tabs.at(2)}, QUuid::createUuid()));            // auto-hide rail
        QVERIFY(s.model.validate());
    }
    // A dropped tab joins a folder between two of its tabs and leaves it
    // outside; at the folder's edge an explicit choice decides.
    void movingATabDecidesItsFolderByPosition_data()
    {
        QTest::addColumn<QString>("moved"); QTest::addColumn<int>("index");
        QTest::addColumn<int>("choice"); // -1: none, 0: no folder, 1: the folder
        QTest::addColumn<QString>("expected");
        // Start: a[bcd]ef. Indices address the strip after removing the tab.
        QTest::newRow("outside into the middle") << "a" << 2 << -1 << "[bcad]ef";
        QTest::newRow("outside to just before") << "f" << 1 << -1 << "af[bcd]e";
        QTest::newRow("outside to just after") << "a" << 3 << -1 << "[bcd]aef";
        QTest::newRow("member within") << "b" << 2 << -1 << "a[cbd]ef";
        QTest::newRow("member past the end") << "b" << 4 << -1 << "a[cd]ebf";
        QTest::newRow("member to the edge, staying") << "b" << 3 << 1 << "a[cdb]ef";
        QTest::newRow("member to the edge, leaving") << "d" << 3 << 0 << "a[bc]def";
        QTest::newRow("outside at the edge, joining") << "f" << 4 << 1 << "a[bcdf]e";
    }
    void movingATabDecidesItsFolderByPosition()
    {
        QFETCH(QString, moved); QFETCH(int, index); QFETCH(int, choice); QFETCH(QString, expected);
        auto s = strip(6);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "bcd"), id));
        const auto folder = choice < 0 ? std::optional<FolderId>{} : std::optional<FolderId>(choice ? id : FolderId{});
        QVERIFY(s.model.moveTab(pick(s, moved).first(), s.group, Placement::Center, index, {}, folder));
        QCOMPARE(shape(s.model, s.group), expected);
        QVERIFY(s.model.validate());
    }
    void stickyAndRailTabsLeaveTheirFolder()
    {
        auto s = strip(4);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "bcd"), id));
        QVERIFY(s.model.setTabSticky(s.tabs.at(1), true));
        QCOMPARE(shape(s.model, s.group), QString("ba[cd]"));
        QVERIFY(s.model.moveTabToEdge(s.tabs.at(2), Edge::Right, false));
        QCOMPARE(shape(s.model, s.group), QString("ba[d]"));
        QVERIFY(s.model.tab(s.tabs.at(2))->folder.isNull());
        QVERIFY(s.model.removeTab(s.tabs.at(3)));
        QVERIFY(!s.model.folder(id)); // the last tab gone, the folder too
        QVERIFY(s.model.validate());
    }
    void membershipOperations()
    {
        auto s = strip(5);
        const auto first = QUuid::createUuid(), second = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "ab"), first));
        QVERIFY(s.model.createFolder(pick(s, "d"), second));
        QCOMPARE(shape(s.model, s.group), QString("[ab]c[d]e"));
        QVERIFY(s.model.addTabToFolder(s.tabs.at(4), first));
        QCOMPARE(shape(s.model, s.group), QString("[abe]c[d]"));
        QVERIFY(s.model.removeTabFromFolder(s.tabs.at(0)));
        QCOMPARE(shape(s.model, s.group), QString("[be]ac[d]"));
        QVERIFY(s.model.mergeFolder(second, first));
        QCOMPARE(shape(s.model, s.group), QString("[bed]ac"));
        QVERIFY(!s.model.folder(second));
        QVERIFY(s.model.ungroupFolder(first));
        QCOMPARE(shape(s.model, s.group), QString("bedac"));
        QVERIFY(s.model.group(s.group)->folders.isEmpty());
    }
    void updatingKeepsMembershipAndValidatesFields()
    {
        auto s = strip(2);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "a"), id));
        auto record = *s.model.folder(id);
        record.name = "n"; record.colorId = "red"; record.collapsed = true;
        QVERIFY(s.model.updateFolder(record));
        QCOMPARE(*s.model.folder(id), record);
        record.name = QString(257, 'x');
        QVERIFY(!s.model.updateFolder(record));
        record.id = QUuid::createUuid();
        QVERIFY(!s.model.updateFolder({record.id}));
        QCOMPARE(shape(s.model, s.group), QString("[a]b"));
    }
    // Moving a folder steps over whole items, folders and tabs alike.
    void movingAFolderWithinItsStripCycles_data()
    {
        QTest::addColumn<int>("steps"); QTest::addColumn<QString>("expected");
        // Start: a[bc]d[ef]g (items: a, [bc], d, [ef], g).
        QTest::newRow("forward") << 1 << "ad[bc][ef]g";
        QTest::newRow("forward twice") << 2 << "ad[ef][bc]g";
        QTest::newRow("backward") << -1 << "[bc]ad[ef]g";
        QTest::newRow("backward past the start") << -2 << "ad[ef]g[bc]";
        QTest::newRow("full cycle") << 5 << "a[bc]d[ef]g";
    }
    void movingAFolderWithinItsStripCycles()
    {
        QFETCH(int, steps); QFETCH(QString, expected);
        auto s = strip(7, 0);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "bc"), id));
        QVERIFY(s.model.createFolder(pick(s, "ef"), QUuid::createUuid()));
        QVERIFY(s.model.moveFolderWithinGroup(id, steps));
        QCOMPARE(shape(s.model, s.group), expected);
    }
    void stickyTabsStayAheadOfMovedFolders()
    {
        auto s = strip(4, 1);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "bc"), id));
        QVERIFY(s.model.moveFolderWithinGroup(id, -1));
        QCOMPARE(shape(s.model, s.group), QString("ad[bc]"));
        QCOMPARE(s.model.group(s.group)->stickyTabs, 1);
    }
    void foldersTravelWithTheirRecords()
    {
        auto s = strip(3);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "ab"), id, "work", "green"));
        QVERIFY(s.model.updateFolder({id, "work", "green", true}));
        const auto other = s.model.createWindow();
        const auto x = s.model.addTab(other, "x");
        const auto target = s.model.groupForTab(x);
        QVERIFY(s.model.moveFolderToGroup(id, target, 0));
        QCOMPARE(shape(s.model, target), QString("[ab]x"));
        QCOMPARE(shape(s.model, s.group), QString("c"));
        QCOMPARE(*s.model.folder(id), (TabFolderRecord{id, "work", "green", true}));
        QVERIFY(s.model.group(s.group)->folders.isEmpty());
        // Moving every tab of a group ends that group; the folder moves on.
        auto lone = strip(1);
        const auto loneFolder = QUuid::createUuid();
        QVERIFY(lone.model.createFolder(lone.tabs, loneFolder));
        const auto y = lone.model.addTab(lone.model.createWindow(), "y");
        QVERIFY(lone.model.moveFolderToGroup(loneFolder, lone.model.groupForTab(y)));
        QVERIFY(!lone.model.group(lone.group));
        QCOMPARE(shape(lone.model, lone.model.groupForTab(y)), QString("y[a]"));
        // In its own group the index addresses the other tabs.
        QVERIFY(s.model.moveFolderToGroup(id, target, 1));
        QCOMPARE(shape(s.model, target), QString("x[ab]"));
        const auto window = s.model.detachFolder(id);
        QVERIFY(!window.isNull());
        const auto detached = s.model.groupForFolder(id);
        QCOMPARE(s.model.group(detached)->windowId, window);
        QCOMPARE(shape(s.model, detached), QString("[ab]"));
        QVERIFY(s.model.validate());
    }
    void wholeWindowsAndMergedGroupsKeepTheirFolders()
    {
        auto s = strip(2);
        const auto floating = s.model.createWindow(WindowKind::Floating, s.window);
        const auto p = s.model.addTab(floating, "p"), q = s.model.addTab(floating, "q"), r = s.model.addTab(floating, "r");
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder({q, r}, id, "carried"));
        QVERIFY(s.model.moveAllTabs(floating, s.group));
        QCOMPARE(shape(s.model, s.group), QString("abp[qr]"));
        QCOMPARE(s.model.folder(id)->name, QString("carried"));
        // A whole window gathered into one new folder.
        const auto again = s.model.createWindow(WindowKind::Floating, s.window);
        s.model.addTab(again, "y"); s.model.addTab(again, "z");
        const auto gathered = QUuid::createUuid();
        QVERIFY(s.model.moveAllTabs(again, s.group, Placement::Center, -1, {}, gathered));
        QCOMPARE(shape(s.model, s.group), QString("abp[qr][yz]"));
        // Merging groups keeps the source group's folders.
        const auto beside = s.model.addTabToNewGroup(s.window, "m", s.group, Placement::Right);
        const auto besideGroup = s.model.groupForTab(beside);
        QVERIFY(s.model.createFolder({beside}, QUuid::createUuid(), "kept"));
        QVERIFY(s.model.moveGroupToCenter(besideGroup, s.group, Placement::Center));
        QCOMPARE(shape(s.model, s.group), QString("abp[qr][yz][m]"));
        QVERIFY(s.model.validate());
    }
    void groupsAndFoldersConvertIntoEachOther()
    {
        auto s = strip(3);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "bc"), id, "f"));
        const auto created = s.model.convertFolderToGroup(id, Placement::Right);
        QVERIFY(!created.isNull());
        QCOMPARE(shape(s.model, created), QString("bc"));
        QCOMPARE(shape(s.model, s.group), QString("a"));
        QVERIFY(!s.model.folder(id));
        QVERIFY(s.model.renameGroup(created, "named"));
        const auto back = QUuid::createUuid();
        QVERIFY(s.model.convertGroupToFolder(created, s.group, back));
        QCOMPARE(shape(s.model, s.group), QString("a[bc]"));
        QCOMPARE(s.model.folder(back)->name, QString("named"));
        QVERIFY(!s.model.group(created));
    }
    void newTabsJoinTheRequestedFolder()
    {
        auto s = strip(2);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "a"), id));
        const auto added = s.model.addTab(s.window, "n", s.group, 1, id);
        QCOMPARE(s.model.tab(added)->folder, id);
        QCOMPARE(shape(s.model, s.group), QString("[an]b"));
    }
    void sortingKeepsFoldersTogether()
    {
        LayoutModel model;
        const auto window = model.createWindow();
        QVector<TabId> tabs;
        for (const auto* title : {"e", "a", "d", "c", "b"}) tabs.append(model.addTab(window, title));
        const auto group = model.groupForTab(tabs.first());
        QVERIFY(model.createFolder({tabs.at(2), tabs.at(3)}, QUuid::createUuid())); // d, c
        QVERIFY(model.sortTabs(group, TabSortOrder::AlphabeticalAscending));
        QCOMPARE(shape(model, group), QString("ab[cd]e"));
    }
    void persistenceRoundTrips()
    {
        auto s = strip(4);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "bc"), id, "name", "purple"));
        QVERIFY(s.model.updateFolder({id, "name", "purple", true}));
        LayoutModel restored;
        QVERIFY(restored.fromJson(s.model.toJson()));
        QCOMPARE(*restored.folder(id), *s.model.folder(id));
        QCOMPARE(shape(restored, s.group), QString("a[bc]d"));
    }
    void persistenceRejectsBrokenFolders_data()
    {
        QTest::addColumn<QString>("damage");
        QTest::newRow("tab names an unknown folder") << "unknown";
        QTest::newRow("folder split in two") << "split";
        QTest::newRow("sticky tab in a folder") << "sticky";
        QTest::newRow("folder without tabs") << "empty";
        QTest::newRow("folder field of the wrong type") << "type";
    }
    void persistenceRejectsBrokenFolders()
    {
        QFETCH(QString, damage);
        auto s = strip(4);
        const auto id = QUuid::createUuid();
        QVERIFY(s.model.createFolder(pick(s, "bc"), id));
        auto json = s.model.toJson();
        auto groups = json.value("groups").toArray();
        auto tabs = json.value("tabs").toArray();
        const auto tabIndex = [&](TabId tab) {
            for (int index = 0; index < tabs.size(); ++index)
                if (QUuid(tabs.at(index).toObject().value("id").toString()) == tab) return index;
            return -1;
        };
        // A null ID removes the tab's folder.
        const auto setFolder = [&](TabId tab, FolderId folder) {
            const int index = tabIndex(tab); auto object = tabs.at(index).toObject();
            if (folder.isNull()) object.remove("folder"); else object.insert("folder", folder.toString(QUuid::WithoutBraces));
            tabs.replace(index, object);
        };
        auto group = groups.at(0).toObject();
        if (damage == "unknown") setFolder(s.tabs.at(3), QUuid::createUuid());
        if (damage == "split") { setFolder(s.tabs.at(3), id); setFolder(s.tabs.at(2), {}); }
        if (damage == "sticky") group.insert("stickyTabs", 2);
        if (damage == "empty") {
            auto folders = group.value("folders").toArray();
            folders.append(QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"name", ""}, {"colorId", ""}, {"collapsed", false}});
            group.insert("folders", folders);
        }
        if (damage == "type") {
            auto folders = group.value("folders").toArray(); auto folder = folders.at(0).toObject();
            folder.insert("collapsed", "yes"); folders.replace(0, folder); group.insert("folders", folders);
        }
        groups.replace(0, group);
        json.insert("groups", groups); json.insert("tabs", tabs);
        LayoutModel restored;
        QVERIFY(!restored.fromJson(json));
    }
    // Seeded random operations: every committed state keeps each folder's
    // tabs adjacent, outside the sticky run, and every folder non-empty
    // (validate() checks these on every commit; it must never reject one).
    void randomOperationsKeepTheInvariant_data()
    {
        QTest::addColumn<quint32>("seed");
        for (quint32 seed : {1u, 7u, 42u, 2026u, 31337u}) QTest::newRow(qPrintable(QString::number(seed))) << seed;
    }
    void randomOperationsKeepTheInvariant()
    {
        QFETCH(quint32, seed);
        QRandomGenerator random{seed};
        auto s = strip(8, 1);
        const auto other = s.model.createWindow();
        s.model.addTab(other, "o");
        const auto otherGroup = s.model.window(other)->activeGroup;
        const auto anyTab = [&] {
            const auto& tabs = s.model.group(s.group)->tabs;
            return tabs.at(random.bounded(int(tabs.size())));
        };
        const auto anyFolder = [&]() -> FolderId {
            const auto& folders = s.model.group(s.group)->folders;
            return folders.isEmpty() ? FolderId{} : folders.at(random.bounded(int(folders.size()))).id;
        };
        for (int step = 0; step < 400; ++step) {
            if (s.model.group(s.group)->tabs.size() < 4) s.model.addTab(s.window, "n", s.group);
            const int size = int(s.model.group(s.group)->tabs.size());
            switch (random.bounded(10)) {
            case 0: s.model.createFolder({anyTab(), anyTab()}, QUuid::createUuid()); break;
            case 1: s.model.addTabToFolder(anyTab(), anyFolder()); break;
            case 2: s.model.removeTabFromFolder(anyTab()); break;
            case 3: s.model.moveTab(anyTab(), s.group, Placement::Center, random.bounded(size)); break;
            case 4: s.model.setTabSticky(anyTab(), random.bounded(2)); break;
            case 5: s.model.moveFolderWithinGroup(anyFolder(), random.bounded(5) - 2); break;
            case 6: s.model.ungroupFolder(anyFolder()); break;
            case 7: s.model.mergeFolder(anyFolder(), anyFolder()); break;
            case 8: s.model.moveTab(anyTab(), otherGroup, Placement::Center, -1); break;
            case 9: s.model.sortTabs(s.group, TabSortOrder(random.bounded(6))); break;
            }
            QVERIFY2(s.model.validate(), qPrintable(QString("step %1: %2").arg(step).arg(shape(s.model, s.group))));
        }
    }
};

QTEST_GUILESS_MAIN(TabFolderModelTest)
#include "tab_folder_model_test.moc"
