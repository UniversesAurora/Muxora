# 阶段 1：布局性能

**目的**：布局操作的成本只与“变化的部分”有关，而不是与标签总数成正比；内容控件不再因为无关操作改 parent。对应审查记录 R1–R5、R12、R17。

**阶段约束**：

- 每一步都在改动前后运行 [0.1 基准](phase-0-baseline.md#01-规模基准程序)，结果写入 `out/test-results/bench-1.<n>-before.json`／`-after.json`，并在步骤“状态”中写出关键数字（例如 1000 个标签时 close 的 p95）。
- 行为不变：现有测试的断言、超时和负载一律不改。所有对外信号（`layoutChanged`、`allWindowsClosed`、`lastWindowAboutToClose`）的发出次数和顺序保持不变，除非步骤明确说明并经过评审。
- 重入保护必须保留：宿主回调可能在任何 `emit` 或 `setFocus` 中同步销毁 `WindowManager`；每次回调后的 `QPointer` 检查不能删。
- 模型的“候选副本 + 校验”原子性是正确性基础。快速路径只能用于不改变成员关系（哪个标签在哪个组、哪个窗格在哪个标签）的操作，并且要证明不变量不会被破坏。

**步骤顺序**：1.1 → 1.2 → 1.3 → 1.4 → 1.5 → 1.6 → 1.7 → 1.8；1.9、1.10 与其余步骤独立。

---

## 1.1 模型反向索引

**目标**：`groupForTab()`、`tabForPane()` 以及新增的 `ownerOfNode()` 为 O(1)。

**前置**：0.1。

**涉及文件**：`modules/window-system/include/window_system/layout_model.hpp`、`modules/window-system/src/model/layout_model.cpp`（`groupForTab`、`tabForPane`、`transact`、`fromJson`、`validate`）、`modules/window-system/tests/layout_model_test.cpp`。

**实现**：

1. 在 `LayoutModel` 私有区增加：

   ```cpp
   struct NodeOwner {
       enum class Kind : quint8 { Central, Pinned, Tab } kind = Kind::Central;
       QUuid owner;          // WindowId（Central/Pinned）或 TabId（Tab）
       Edge edge = Edge::Left; // 仅 Pinned 使用
   };
   void rebuildIndex();
   QHash<TabId, GroupId> tabGroup_;
   QHash<PaneId, TabId> paneTab_;
   QHash<QUuid, NodeOwner> nodeOwner_;   // 分割节点 ID → 所在树
   bool indexValid_ = false;
   ```

   这些成员不进入 `toJson()`，也不参与相等比较。

2. `rebuildIndex()`：清空三个表；遍历 `groups_`，对每个 `tabs` 中的 TabId 写 `tabGroup_`；遍历 `tabs_`，用现有的 `collect()` 收集窗格写 `paneTab_`，同时遍历其窗格树的非叶节点写 `nodeOwner_{Tab, tabId}`；遍历 `windows_` 的 `central` 和 `pinnedLayouts[edge]` 的非叶节点写 `nodeOwner_`。最后 `indexValid_ = true`。

3. 查询改为：

   ```cpp
   GroupId LayoutModel::groupForTab(TabId id) const
   {
       if (indexValid_) return tabGroup_.value(id);
       /* 原来的线性扫描，保持不变 */
   }
   ```

   `tabForPane()` 同样处理。新增 `std::optional<NodeOwner> ownerOfNode(QUuid node) const`（私有，供 1.2 使用）。

4. **索引失效规则**（关键）：
   - `transact()` 中 `LayoutModel candidate = *this;` 之后立即 `candidate.indexValid_ = false;`。操作期间候选模型直接改 `groups_`、`tabs_`，查询会退回扫描，保证正确。
   - 操作成功后，在 `synchronizePinnedLayouts()`、`reconcilePinnedRestoreLayouts()` 之后、`validate()` 之前调用 `candidate.rebuildIndex()`。
   - `fromJson()` 在 `candidate.validate()` 之前调用 `candidate.rebuildIndex()`。
   - 用下面的命令找出所有在 `transact` lambda 之外修改记录的地方，逐个确认它们要么在候选副本上（会在提交前重建），要么在调用后显式 `rebuildIndex()`：

     ```bash
     grep -n "windows_\[\|groups_\[\|tabs_\[\|\.insert(\|\.remove(\|\.erase(" modules/window-system/src/model/*.cpp
     ```

5. `validate()` 末尾增加一致性检查：若 `indexValid_`，用扫描方式重算三张表并比较，不一致返回 `failure("The lookup index is stale.")`。这个检查是 O(N)，`validate()` 本身已经是 O(N)，不改变复杂度。

**测试**（`layout_model_test.cpp`，新增）：

- `indexFollowsEveryMutation`：构造含两个窗口、中央与固定组、多窗格标签的模型；依次执行 `addTab`、`moveTab`、`moveTabToEdge`（固定与非固定）、`splitPane`、`movePane`、`extractPane`、`mergeGroups`、`convertGroupToTab`、`convertTabToGroup`、`detachTab`、`closeWindow`、`fromJson(toJson())`；每一步之后对所有标签和窗格比较 `groupForTab`／`tabForPane` 与按 `groups()`、`panes()` 扫描得到的结果，并断言 `validate().ok`。
- `failedTransactionKeepsIndex`：执行一个必然失败的操作（例如移动到不存在的组），断言索引查询结果与失败前一致。

**验证**：`window-system.model`、`window-system.tab-ordering-model`、`window-system.hierarchy-conversion`、`window-system.pinned-layout`、`window-system.pinned-positions`、`window-system.movement-model`，然后基准。

**完成标准**：

- [ ] 上述套件全部通过，新测试覆盖全部列出的操作。
- [ ] 基准中 1000／2000 个标签的 close、move、split p95 下降（写出数字）。
- [ ] `toJson()` 输出与改动前逐字节相同（用现有快照测试或一次手工对比确认）。

**风险**：遗漏某条直接修改路径会让索引过期。第 5 条的一致性检查能在测试中暴露这类遗漏；不要因为它“慢”而去掉。

**状态**：未开始。

---

## 1.2 激活与分割比例快速路径（R17）

**目标**：点击标签、焦点变化、拖动分隔条不再复制整个模型并全量校验。

**背景**：`LayoutModel::activateTab()`、`activatePane()` 都走 `transact()`：复制全部记录、同步固定树、全量 `validate()`。`WindowManager::eventFilter` 在每次 `FocusIn` 时调用 `activatePane()`，`updateActive()` 再调用一次 `activateTab()`，所以每次焦点变化做两次完整事务。`setSplitRatio()` 同样走事务，并遍历所有窗口和所有标签的树。

**前置**：1.1。

**涉及文件**：`layout_model.cpp`（`activateTab`、`activatePane`、`setSplitRatio`、`nextTabOrder` 所在的 `tab_ordering.cpp`）、`layout_model_test.cpp`、`tab_ordering_model_test.cpp`。

**实现**：

1. `activateTab(TabId id)`：

   ```cpp
   if (!indexValid_) return transact(/* 原实现 */);
   const auto groupId = tabGroup_.value(id);
   if (groupId.isNull()) return failure("The tab does not exist.");
   auto& group = groups_[groupId];
   auto& window = windows_[group.windowId];
   if (group.activeTab == id && window.activeGroup == groupId) return success(); // 见第 4 条
   if (group.activeTab != id) {
       if (orderCounter_ >= maximumTabOrder) return transact(/* 原实现 */); // 计数回绕需要整体重排
       tabs_[id].visitedOrder = nextTabOrder();
   }
   group.activeTab = id;
   window.activeGroup = groupId;
   ++revision_;
   return success();
   ```

   不变量说明（写进代码注释）：只改 `activeTab`（取自同组成员）、`activeGroup`（取自该窗口的组）和单调递增的 `visitedOrder`，成员关系、树结构和标识都没有变化，所以 `validate()` 的结论不变，固定树同步也不需要执行。

2. `activatePane(TabId, PaneId)`：先用 `paneTab_` 确认窗格属于该标签，再做与第 1 条相同的组和窗口更新，同时设置 `tabs_[tabId].activePane`。

3. `setSplitRatio(QUuid node, double ratio)`：
   - 范围检查不变（0.05–0.95，有限值）。
   - 用 `nodeOwner_` 定位所在树，只对这一棵树调用 `replaceRatio()`（路径复制），其余树不动。
   - 找不到节点返回原来的失败信息。
   - `++revision_`，不走 `transact()`。
   - `indexValid_ == false` 时退回原实现。

4. **“未变化时是否递增 revision”**：先阅读原实现在激活同一标签时的行为。原实现通过 `transact()` 总是递增 revision。为保持行为一致，快速路径在“已经是活动标签”时也要 `++revision_` 并返回成功。上面代码中的提前返回只有在确认没有调用方依赖 revision 变化后才能保留；默认按原行为递增。

5. `nextTabOrder()` 的回绕重排（`tab_ordering.cpp`）会修改所有标签，必须留在事务路径中，第 1 条已经处理。

**测试**：

- `activationFastPathMatchesTransaction`：对同一个初始模型分别执行快速路径和强制事务路径（测试中先 `fromJson(toJson())` 得到索引无效的副本即可走旧路径；或增加一个仅测试可见的开关），比较两者 `toJson()` 和 `revision()` 完全相同。覆盖：同组切换、跨组切换、跨窗口、已经活动、窗格切换、计数接近上限（把 `orderCounter` 设为 `2^53 - 2`）。
- `ratioFastPathTouchesOnlyOwnerTree`：修改一个标签内分割节点的比例，断言其他标签和窗口的树指针（`LayoutTree` 的 `get()`）不变；修改中央树节点，断言所有标签的窗格树指针不变。
- 原有 `movement_test` 中 `layout.setRatio` 的非法值断言保持通过。

**验证**：模型类套件、`window-system.movement`、`window-system.focus-mode`、`window-system.shortcut-dispatch`、`window-system.tab-group`；基准中 keys、ratio 场景前后对比。

**完成标准**：

- [ ] 两条快速路径与事务路径结果逐字节一致。
- [ ] 基准 ratio 场景在 2000 个标签下的 p95 与 100 个标签时处于同一数量级（写出数字）。

**风险**：如果将来给激活增加新的副作用（例如历史记录），必须同时更新快速路径；在函数注释中写明“新增状态时同时检查 activateTab 快速路径”。

**状态**：未开始。

---

## 1.3 去掉序列化比较（R5）

**目标**：`addTabToNewGroup()`、`requestCloseAllWindows()` 不再为检测重入修改而序列化整个模型或布局。

**前置**：1.1（不依赖，但改同一文件，顺序执行避免冲突）。

**涉及文件**：`window_manager.cpp`（`addTabToNewGroup` 约 178 行；`requestCloseAllWindows` 约 406–427 行）。

**实现**：

1. `addTabToNewGroup()`：删除 `originalModel = d->model.toJson()` 和 `d->model.toJson() != originalModel` 比较，只保留 `d->model.revision() != revision`。
2. `requestCloseAllWindows()`：`unchanged()` 中的 `saveLayout() != before` 替换为：模型 `revision()` 未变、`d->options`／`d->viewStates` 的大小未变，以及原有的窗口和内容控件身份比较。
3. **前提核对**：这两处比较存在，是因为担心回调改动了模型却没有递增 revision。实施前逐个检查 `LayoutModel` 的所有修改路径（1.1 中的 grep 结果），确认每条成功路径都递增 `revision_`。若发现不递增的路径，先修那条路径（单独提交并加测试），再做本步。
4. 窗口级状态（`saveLayout()` 中除模型之外的部分：工具条、栏可见性、行为设置）在回调中变化，是否也应阻止关闭？阅读 `requestCloseAllWindows` 的注释与 `memory_lifecycle_test`、`application-exit-test.ps1` 的意图：关闭许可只需保证“被批准的内容就是将要关闭的内容”。工具条布局变化不影响这一点，因此可以不比较；在代码注释中写明这个判断。

**测试**：在 `tab_creation_test.cpp` 和应用退出相关测试中已有重入用例；新增 `closeAllRejectsModelChangeFromGuard`：关闭许可回调中执行 `renameTab`，断言关闭被拒绝、窗口保留；以及 `closeAllIgnoresToolbarEditFromGuard`：回调中修改工具条宽度，断言关闭照常完成。

**验证**：`window-system.tab-creation`、`window-system.memory-lifecycle`、`window-system.widgets`、应用 `tools/tests/application-exit-test.ps1`。

**状态**：未开始。

---

## 1.4 分隔条到控件的映射

**目标**：拖动分隔条时不再在所有标签视图中 `findChildren<QSplitter*>()`。

**前置**：1.2。

**涉及文件**：`panes/tab_content.{hpp,cpp}`（`buildNode`）、`docking/dock_surface.{hpp,cpp}`（`buildTree`、`buildPinned`）、`commands/window_commands.cpp`（`layout.setRatio` 约 513–537 行）、`window_manager_p.hpp`。

**实现**：

1. `TabContent` 增加 `QHash<QUuid, QPointer<QSplitter>> splitters_;`，`buildNode()` 创建分割器时登记，`rebuild()` 开头清空。提供 `QSplitter* splitterFor(QUuid node) const`。
2. `DockSurface` 同样登记中央树和固定树的分割器（`buildTree` 中创建的 `GroupSplitter`），`rebuild()` 开头清空。提供同名函数。
3. `layout.setRatio` 处理函数：用 1.1 的 `ownerOfNode()`（需要在 `LayoutModel` 公开一个只读查询，或在 `WindowManager::Private` 中根据所有者类型定位）找到所在标签或窗口：
   - Tab 所有者：`d->tabs.value(owner)->splitterFor(node)`；
   - Central／Pinned 所有者：`d->windows.value(owner)->surface()->splitterFor(node)`。
   保留原有的 `isVisibleTo(owner)` 判断和 `QSignalBlocker`。
4. 如果把 `ownerOfNode()` 设为公开 API，在头文件注释中说明它只返回布局树信息；同时在外部宿主夹具中调用一次。若不想扩大公开 API，就在 `LayoutModel` 中声明 `friend class WindowManager;` 之外的方式都不可取——优先公开一个最小的查询。

**测试**：`movement_test` 已有拖动和命令修改比例的用例；新增 `ratioCommandUpdatesOnlyTheOwningSplitter`：两个标签各有一个分割，修改其中一个，断言另一个分割器的 `sizes()` 不变，且被修改的分割器尺寸按比例更新。

**验证**：`window-system.movement`、`window-system.pane-minimum`、`window-system.pinned-ui`；基准 ratio 场景。

**状态**：未开始。

---

## 1.5 只重建变化的标签视图

**目标**：`refresh()` 中窗格树和窗格栏都没有变化的标签，不重建 `QSplitter`／`PaneHostFrame`，其内容控件也不改 parent。

**前置**：1.4。

**涉及文件**：`panes/tab_content.{hpp,cpp}`、`window_manager.cpp`（`refresh()`）、新测试文件 `modules/window-system/tests/view_reuse_test.cpp`、`modules/window-system/tests/CMakeLists.txt`。

**实现**：

1. `TabContent` 记录上次构建的输入：

   ```cpp
   LayoutTree builtTree_;                 // record.panes 的指针
   QVector<QPair<PaneId, QWidget*>> builtContents_; // 每个窗格当时挂入的内容控件
   QVector<QPair<PaneId, QWidget*>> builtTopBars_, builtBottomBars_;
   int builtSplitterWidth_ = -1;          // 主题度量变化也要重建
   ```

   新增 `bool canReuse(const TabRecord&, contents, topBars, bottomBars) const`：树指针相同，且每个窗格对应的内容控件、上下栏控件与记录一致，且 `manager_->theme().metrics.splitterWidth` 未变。

2. `rebuild()` 开头：若 `canReuse(...)` 为真，只执行“活动窗格同步”（`activePane_ = record.activePane` 并更新各框的 `active` 属性，复用 `setActivePane()`）和 `setFocusedPane(focusedPane_)`，然后返回。

3. `refresh()` 的调整（这是本步的关键）：
   - 现在第 417–420 行把**所有**内容控件、窗格栏、`TabContent` 收回存储控件。改为先计算 `reusableTabs`：模型中仍存在、视图存在且 `canReuse()` 为真的标签。
   - 对 `reusableTabs` 中标签的内容控件和窗格栏，**不**调用 `setParent(d->storage)`／`hide()`。
   - `TabContent` 本身仍然要收回存储控件，因为 `DockSurface::rebuild()` 会删除旧的 `GroupTabs`。这一点在 1.6 解决；本步只省掉分割器和窗格框的重建，以及内容控件在 `TabContent` 内部的改挂。
   - 注意顺序：`reconcileWindowViewState()` 在重建前调用，可能改变焦点；保持原来的调用位置。

4. 一个窗格移到别的标签时，原标签的树指针一定变化（叶子被移除），所以不会被误判为可复用；新标签的树也变化。写一条断言级注释说明这一点。

**测试**（新建 `view_reuse_test.cpp`，加入 `foreach(suite ...)` 列表，标签按 0.2 规则判断）：

- `unrelatedTabsKeepTheirViewWidgets`：三个标签，第三个标签拆成两个窗格。记录第二、第三个标签的 `PaneHostFrame`（通过 `findChildren` 按 `paneId` 属性取得）、`PaneSplitter` 指针和内容控件的 parent。然后关闭第一个标签、把一个新标签移入、重命名第二个标签。断言记录的指针仍然存活（`QPointer` 非空）且相同，内容控件 parent 不变。
- `changedTreeRebuildsOnlyThatTab`：拆分第二个标签，断言第二个标签的框集合变化、第三个标签的不变。
- `themeMetricChangeRebuilds`：改 `splitterWidth` 后 `setTheme()`，断言分割器宽度更新（防止复用逻辑吞掉主题变化）。
- 内容身份断言沿用现有夹具写法：`manager.paneWidget(pane) == 原指针`。

**验证**：新套件、`window-system.widgets`、`window-system.pane-settings`、`window-system.pane-bar-policy`、`window-system.pane-minimum`、`window-system.focus-mode`、`window-system.memory-lifecycle`、`window-system.drag-interaction`；全量一次；基准。

**完成标准**：

- [ ] 新测试在本步实现前失败（框和分割器指针会变化），实现后通过。
- [ ] 全量无新增失败。
- [ ] 基准中 close／move 场景下降，或说明没有下降的原因（`TabContent` 仍在改挂，1.6 之后才见效）。

**风险**：

- 焦点投影（`FocusProjection`）在 `rebuild()` 开头 `restore()`；复用路径也必须先 `restore()` 再 `setFocusedPane()`，否则窗格全屏状态会残留。
- `geometryUpdateDepth` 期间的延迟删除逻辑只在真正重建时需要，复用路径不涉及。

**状态**：未开始。

---

## 1.6 窗口内保留标签视图与组控件

**目标**：标签所属窗口不变时，`TabContent` 不再收回存储控件；组的标签列表和位置都不变时，复用 `GroupTabs`。这样一次普通的关闭或移动只影响相关的组。

**前置**：1.5。

**涉及文件**：`window_manager.cpp`（`refresh()`）、`docking/dock_surface.{hpp,cpp}`（`rebuild`、`buildGroup`、`buildTree`、`buildPinned`、`buildEdge`）、`docking/tab_strip.{hpp,cpp}`（`GroupTabs::addTab`，新增取出页面的方法）、`view_reuse_test.cpp`。

**分两次提交**：

### 1.6a 同窗口内直接转移 `TabContent`

1. `GroupTabs` 增加 `QWidget* takePage(TabId id)`：从 `stack_` 中 `removeWidget()` 并返回页面，不删除；同时从标签条移除对应标签。
2. `DockSurface::rebuild()` 开头（删除旧 `root_` 之前），对旧 `groups_` 中的每个页面：如果该标签仍属于本窗口，调用 `takePage()` 并暂存到 `QHash<TabId, QPointer<TabContent>> carried`；新 `buildGroup()` 优先使用 `carried` 中的页面。页面在同一个顶层窗口内改挂，不经过存储控件。
3. `refresh()` 中，只把“所属窗口将要变化”或“将被删除”的标签视图收回存储控件。判断方法：在修改前记录每个视图当前所在的窗口（`view->window()->property("windowId")`），与新模型中该标签的窗口比较。
4. 旧 `root_` 仍用 `deleteLater()`。删除执行时，被转移的页面已经不是它的子控件，不会被删除。加注释说明这一保证，并在测试中检查。

### 1.6b 复用未变化的 `GroupTabs`

1. 定义组签名：

   ```cpp
   struct GroupSignature {
       QVector<TabId> tabs; TabId activeTab; GroupLocation location; Edge edge;
       bool operator==(const GroupSignature&) const = default;
   };
   ```

   `DockSurface` 保存 `QHash<GroupId, GroupSignature> builtGroups_`。

2. 重建时，若某组签名不变，把旧 `GroupTabs` 从旧分割器中取出（`setParent(nullptr)` 之前先 `hide()`，或直接 `newSplitter->addWidget(oldGroup)`，Qt 会处理改挂）放入新树；只调用 `setTabMetadata()` 更新标题、颜色等元数据，以及 `setAppearance()`、`setTabBarVisible()`。签名变化的组照旧新建。
3. 回调（`newTabRequested`、`drag` 等）在 `buildGroup()` 中设置，捕获的是组 ID 和窗口 ID，复用时不需要重设；但要确认没有捕获已被删除的对象（例如旧的 `bar` 指针在复用时仍然有效，因为 `GroupTabs` 没变）。
4. 视口状态：`captureStripScrollOffsets()`／`restoreStripScrollOffsets()` 对复用的组可以跳过，因为控件没变。保留原逻辑也无害，先保留。

**测试**（追加到 `view_reuse_test.cpp`）：

- `tabViewsStayInTheirWindow`：两个组各三个标签，在组 A 内关闭一个标签；断言组 B 的 `GroupTabs` 指针不变，组 A、B 所有剩余 `TabContent` 指针不变、从未以存储控件为 parent（用 `QObject::installEventFilter` 监听 `QEvent::ParentChange`，计数为 0）。
- `crossWindowMoveReparentsOnlyTheMovedTab`：两个窗口，移动一个标签到另一窗口；断言只有该标签内容收到 `ParentChange`。
- `deferredRootDeletionKeepsCarriedPages`：重建后立即 `QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)`，断言转移的页面仍然存活。
- 现有拖拽、固定、焦点模式测试全部保持通过（它们覆盖了视口位置、插入预览等细节）。

**验证**：全部 `docking`、`pinned`、`edge`、`drag`、`strip`、`tab-*`、`sidebar-*`、`focus-mode`、`view-*` 相关套件；全量一次；基准。

**完成标准**：

- [ ] 基准中 1000 个标签的 close p95 显著低于基线（写出数字），并且 `ParentChange` 计数测试通过。
- [ ] 全量无新增失败。

**风险**：

- `DragController` 持有对组控件或预览的引用。复用后 `releaseForMutation()` 的语义不变，但需要检查 `drag_controller.cpp` 中是否缓存了 `GroupTabs*`；若有，确认复用不会让它指向错误状态。
- 焦点投影把非目标分支隐藏；复用的组可能处于被隐藏状态，重建后要通过 `applyFocusScope()` 重新计算，不能假设可见。

**状态**：未开始。

---

## 1.7 非活动标签按需创建视图

**目标**：只有激活过的标签才有 `TabContent`；从未显示的标签只占标签条上的一项。内容控件不受影响（仍由宿主创建，停在存储控件中）。

**前置**：1.6。

**涉及文件**：`window_manager.cpp`（`refresh`、`activateTab`、`focusPane`）、`hierarchy_api.cpp`、`navigation/workspace_navigation.cpp`（使用 `d->tabs` 的位置，共 11 处）、`docking/tab_strip.cpp`（`GroupTabs` 支持空页面）、`docking/dock_surface.cpp`（`buildGroup`）、`panes/tab_content.cpp`。

**实现**：

1. 列出所有 `d->tabs` 使用点：

   ```bash
   grep -rn "d->tabs\.value(\|d->tabs\[\|->d->tabs" modules/window-system/src
   ```

   逐个分类：只读查询（视图不存在时返回默认值即可）、需要视图（先调用新函数 `ensureTabView(TabId)`）。

2. 新增 `TabContent* WindowManager::ensureTabView(TabId tab)`（私有）：视图不存在则创建并 `rebuild()`，返回指针。所有“需要视图”的调用点改用它。
3. **标签栏**：`setTabTopBar()`／`setTabBottomBar()` 现在在视图不存在时返回 `false`。改为：视图不存在时把栏存入 `d->pendingTabTopBars`／`pendingTabBottomBars`（`QHash<TabId, QPointer<QWidget>>`，parent 设为存储控件并隐藏），创建视图时交给它。标签关闭时删除未交付的栏（与现有 `refresh()` 中删除窗格栏的方式一致）。这样公开 API 的成功语义不变。
4. `GroupTabs`：允许页面为空。`addTab(nullptr, ...)` 时在 `stack_` 放一个共享的空白占位控件吗？不行——`QStackedWidget` 的索引与标签一一对应，同一控件不能加入两次。做法：为每个空页面放一个极轻量的 `QWidget`（无布局、无样式类名），首次 `currentChanged` 到该索引时，通过新回调 `pageRequested(TabId)` 让 `DockSurface` 调 `ensureTabView()`，再用真实页面替换占位控件（`stack_->insertWidget(index, real); stack_->removeWidget(placeholder); delete placeholder;`）。
   - 一个占位 `QWidget` 仍然比 `TabContent + 分割器 + 窗格框 + 内容挂接` 小得多；如果 0.1 基准显示占位本身仍是负担，再考虑改用不依赖 `QStackedWidget` 的页面容器。
5. `refresh()` 中第 442–448 行“为每个模型标签创建并重建视图”改为：只对已有视图的标签执行 `rebuild()`；每个组的活动标签调用 `ensureTabView()`（活动标签必须立即可见）。
6. 内容的可见性：未创建视图的标签，其内容控件留在存储控件中、隐藏，Qt 不会绘制它们。
7. **内容可见性通知**（新增公开 API，供以后终端释放 GPU 资源）：

   ```cpp
   struct ContentVisibilityChange { PaneId pane; bool visible; };
   // Called after a refresh/activation settles, coalesced per event-loop turn.
   void setContentVisibilityHandler(std::function<void(const QVector<ContentVisibilityChange>&)> handler);
   ```

   实现：在 `refresh()`、`activateTab()`、窗口最小化／恢复（`WorkspaceWindow::changeEvent`）、侧栏面板开关之后，计算每个窗格“当前是否显示”（内容控件 `isVisible()`），与上次记录比较，把变化放入待发送列表，用 `QTimer::singleShot(0, ...)` 合并发出。处理函数可能销毁管理器，按现有方式保留并检查 `QPointer`。

**测试**：

- `inactiveTabsHaveNoView`：一组 100 个标签，只激活最后一个；断言 `findChildren<TabContent*>()`（或按对象名 `TabPage`）数量为 1；逐个激活后数量递增。
- `barsSetBeforeFirstActivationAreDelivered`：对未激活标签调用 `setTabTopBar()` 返回 `true`，激活后栏出现在视图中；关闭未激活标签时栏被删除（`QPointer` 变空）。
- `visibilityChangesAreCoalesced`：切换标签、最小化窗口、打开和关闭侧栏面板，断言处理函数收到的变化列表与预期一致，且同一事件循环内的多次切换只通知一次。
- 现有所有测试必须通过；特别是依赖“所有标签都有视图”的测试，如果断言了非活动标签的框，说明测试依赖实现细节，按测试规范提交独立证据后修订，并保留等效覆盖。

**验证**：全量一次；基准 create、memory 场景。

**完成标准**：

- [ ] 基准中 1000 个标签的每标签控件数和私有内存下降（写出数字）。
- [ ] 外部宿主夹具调用一次 `setContentVisibilityHandler()`。
- [ ] `modules/window-system/README.md` 说明可见性通知的时机。

**风险**：拖拽中的标签预览（`dragLabelPixmap`）和完整标签列表可能读取非活动标签的视图；逐个检查 `drag_controller.cpp`、`tab_strip.cpp` 中对页面的访问。

**状态**：未开始。

---

## 1.8 延迟恢复内容

**目标**：恢复上百个标签的布局时，宿主只为活动标签创建内容；其他标签首次激活时再创建。

**前置**：1.7。

**涉及文件**：`window_manager.hpp`（新重载）、`layout_persistence.cpp`（`restoreLayout`）、`window_manager.cpp`（`createContent`、新增 `ensurePaneContent`）、`src/app/main.cpp`（启用）。

**实现**：

1. 新增：

   ```cpp
   struct RestoreOptions { bool deferInactiveContent = false; };
   OperationResult restoreLayout(const QJsonObject& snapshot, const RestoreOptions& options);
   ```

   原 `restoreLayout(snapshot)` 等价于 `deferInactiveContent = false`。

2. 推迟的窗格：`d->descriptors` 中保存描述，`d->panes` 中**没有**条目。新增私有 `QWidget* ensurePaneContent(PaneId)`：若没有内容，调用 `createContent(descriptor, pane)`，按 `addTab` 中的方式设置属性、焦点策略、parent，然后插入 `d->panes`。工厂返回空指针时，放一个显示“无法创建内容”的占位标签，并通过 `commandFailed` 报告，避免空窗格。
3. `ensureTabView()`（1.7）在构建视图前，对该标签的每个窗格调用 `ensurePaneContent()`。
4. `paneWidget(PaneId)` 的契约：启用延迟恢复时，未激活标签的窗格返回 `nullptr`。在头文件注释中写明；不改变默认行为。
5. 关闭许可回调（`CloseGuard`）收到的标签可能包含尚未创建内容的窗格；`approveClose()` 中 `contents` 记录为空指针，比较逻辑不变。
6. 应用 `main.cpp` 中恢复时传 `{.deferInactiveContent = true}`。占位内容很便宜，这一步对占位应用的意义在于验证路径；真正受益的是以后的终端和会话内容。

**测试**：`deferredRestoreCreatesContentOnActivation`：保存含 50 个标签的布局，用计数工厂恢复，断言工厂只被调用“组数”次；逐个激活后调用次数增加、内容身份稳定；关闭从未激活的标签时工厂不被调用。

**验证**：`window-system.widgets`、`window-system.view-state`、`window-system.tab-creation`、应用部署夹具（重启恢复）。

**状态**：未开始。

---

## 1.9 布局保存移出 GUI 线程（应用侧）

**目标**：保存时 GUI 线程只生成快照；JSON 编码与写盘在工作线程完成。

**前置**：0.1（先看 save 场景数据；若 2000 个标签时 `toJson` 加写盘仍低于 5 ms，可以只做紧凑格式，不做线程化，并在状态中写明理由）。

**涉及文件**：`src/app/main.cpp`（`save`），或 6.1 之后的 `src/app/layout_store.cpp`。

**实现**：

1. 编码改为 `QJsonDocument::Compact`。读取端不受影响。
2. 新增 `LayoutStore` 类：`void saveAsync(QJsonObject snapshot)`、`bool saveNow(QJsonObject snapshot)`。
   - `saveAsync`：递增序号，把快照交给 `QThreadPool::globalInstance()->start()`；工作线程中编码、`QSaveFile` 写入。写完通过 `QMetaObject::invokeMethod(store, ..., Qt::QueuedConnection)` 回到 GUI 线程报告结果。
   - 序号保证新快照不会被旧任务覆盖：工作线程写入前检查 `latestSequence` 是否仍等于自己的序号，不是则放弃。用 `std::atomic<quint64>`。
   - 同一时间最多一个写任务在运行，新的请求只记录“最新快照”，当前任务结束后再写（容量为 1 的队列）。
   - `saveNow`：等待正在运行的任务结束（`QThreadPool::waitForDone` 不合适，因为线程池是全局的；用 `QFuture` 或自己的 `QMutex` + `QWaitCondition`，等待有超时），然后同步写入。
3. 自动保存（700 ms 定时器）用 `saveAsync`；`lastWindowAboutToClose`、`aboutToQuit`、`application.exit`、`layout.save` 命令用 `saveNow`，保持“退出前保存完成”的现有保证。

**测试**：应用侧新增 `muxora.layout-store`（model 层）：连续 100 次 `saveAsync` 后 `saveNow`，文件内容等于最后一次快照；模拟写入失败（只读目录）时报告失败且不崩溃；旧任务晚完成时不覆盖新内容（用可注入的延迟钩子）。

**验证**：新套件、`application-exit-test.ps1`、`lua-deployment-test.ps1`。

**状态**：未开始。

---

## 1.10 命令注册表 ID 索引（R12）

**目标**：`CommandRegistry::find()` 为 O(1)。

**涉及文件**：`commands/command_registry.cpp`。

**实现**：`State` 中增加 `QHash<QString, qsizetype> indexById;`。`add()` 追加后插入；`remove()` 删除后重建（删除使后续下标移动，命令数量小，重建 O(n) 可接受）；`find()` 查表。`commands` 向量的顺序不变（菜单和分发依赖注册顺序）。

**测试**：`shortcut_dispatch_test` 中已有注册、删除、回调中修改注册表的用例；新增 `findSurvivesRemovalInTheMiddle`：注册 A、B、C，删除 B，`find("C")` 返回正确命令，`commands()` 顺序为 A、C。

**状态**：未开始。
