# 阶段 1：布局性能

**目的**：布局操作的成本只与“变化的部分”有关，而不是与标签总数成正比；内容控件不再因为无关操作改 parent。对应审查记录 R1–R5、R12、R17。

**阶段约束**：

- 每一步都在改动前后运行 [0.1 基准](phase-0-baseline.md#01-规模基准程序)，结果写入 `out/test-results/bench-1.<n>-before.json`／`-after.json`，并在步骤“状态”中写出关键数字（例如 1000 个标签时 close 的 p95）。
- 行为不变：现有测试的断言、超时和负载一律不改。所有对外信号（`layoutChanged`、`allWindowsClosed`、`lastWindowAboutToClose`）的发出次数和顺序保持不变，除非步骤明确说明并经过评审。
- 重入保护必须保留：宿主回调可能在任何 `emit` 或 `setFocus` 中同步销毁 `WindowManager`；每次回调后的 `QPointer` 检查不能删。
- 模型的“候选副本 + 校验”原子性是正确性基础。快速路径只能用于不改变成员关系（哪个标签在哪个组、哪个窗格在哪个标签）的操作，并且要证明不变量不会被破坏。

**步骤顺序**：1.1 → 1.2 → 1.3 → 1.4 → 1.5 → 1.6 → 1.7 → 1.8；1.7 之后依次做 1.11、1.12（两者都改 `tab_content.cpp`／刷新路径，串行）；1.9、1.10 与其余步骤独立。

**基准的实际跑法（1.4–1.6）**：与 1.1–1.3 相同（`bench-release` 构建、offscreen、100／500／1000 档、每场景 30 次、不含 `cycle`），由临时脚本按 `bench.ps1` 的配置构建后运行。1.4-before 在合并后的 `main`（`151f752`）上重新测量：1.3-after 那次运行之后 `main` 又合入了状态栏高度改动，模块代码不完全相同。之后每一步的 before 就是上一步的 after（代码相同，没有重跑）。运行期间不编译，其他工作只读写文件。1.6b 的最终 after 用 `pwsh ./tools/bench.ps1 -Name 1.6b-after` 跑完整基准（含 `cycle`），这时一次只要 30 分钟左右。

**基准的实际跑法（1.1–1.3）**：完整的 `tools/bench.ps1` 一次约 3.4 小时，其中 1000 次“添加 + 关闭”的 `cycle` 场景在 1000 个标签时单独约 2.2 小时，而且不受 `-Repeat` 影响。1.1–1.3 不涉及这个场景，所以按 `bench.ps1` 相同的配置构建 `out/build/bench-release`，再直接运行基准程序：`--tabs 100,500,1000 --repeat 30`，场景为 create、close、move、split、ratio、keys、save（不含 cycle），平台 offscreen。每次约 37 分钟，前后两次在同一台机器上连续运行，运行期间不做大规模编译（1.1-before 期间编译过两次单文件的 `toJson()` 对比程序）。基准程序记录的提交标签是运行时工作区的状态，`-dirty` 表示当时步骤代码尚未提交，内容与随后的提交相同。

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

- [x] 上述套件全部通过，新测试覆盖全部列出的操作。
- [ ] 基准中 1000／2000 个标签的 close、move、split p95 下降（写出数字）。未达到，见状态。
- [x] `toJson()` 输出与改动前逐字节相同（用现有快照测试或一次手工对比确认）。

**风险**：遗漏某条直接修改路径会让索引过期。第 5 条的一致性检查能在测试中暴露这类遗漏；不要因为它“慢”而去掉。

**状态**：已完成（提交标题 “Add LayoutModel reverse lookup indexes”）。实现与上面的差别和核对结果：

- 三张表由同一个私有函数 `scanIndex()` 计算，`rebuildIndex()` 和 `validate()` 的一致性检查都调用它。`ownerOfNode()` 只覆盖中央树、固定树和窗格树的分割节点；固定侧的恢复骨架（`pinnedRestoreLayouts`）节点有独立标识，`setSplitRatio()` 原本就找不到它们，所以不进入索引。
- 第 4 条的 grep 结果：所有公开修改函数都经过 `transact()` 或 `fromJson()`；其余直接写记录的位置都在 `*Unchecked`、`synchronizePinnedLayouts()`、`reconcilePinnedRestoreLayouts()`、`nextTabOrder()` 等私有函数里，只在候选副本上调用。
- 测试通过头文件中声明的 `friend struct LayoutModelTestAccess` 读取索引状态，宿主不定义这个类型。
- 新增 `indexFollowsEveryMutation`、`failedTransactionKeepsIndex`、`validateReportsAStaleIndex`（人为写入错误索引后 `validate()` 返回 “The lookup index is stale.”）。
- 验证：`model`（100 行）、`tab-ordering-model`、`hierarchy-conversion`、`pinned-layout`、`pinned-positions`、`movement-model` 六个套件全部通过。`toJson()` 逐字节对比：用临时驱动程序分别链接 `3431796` 与本步的模型源码，读入同一份 24 个标签、两个窗口、含固定与自动隐藏组的布局，执行 180 次激活、分割比例、重命名、关闭操作并逐次输出 `toJson()` 与 revision，两份输出完全相同；把 `orderCounter` 改到 2^53 − 3 再跑一遍（经过计数回绕）也相同。
- 基准（`bench-1.1-before.json`／`-after.json`，100／500／1000 个标签，每场景 30 次，跳过 `cycle` 场景，见阶段说明）：1000 个标签时 close p95 3.99 s → 3.93 s、move 4.01 s → 4.07 s、split 3.95 s → 4.04 s，属于测量波动，**完成标准中“p95 下降”没有达到**。原因是这三项的耗时几乎全部在 `refresh()` 重建全部标签视图（1.4–1.6 处理），模型查询占比很小；100 个标签时三项 p95 从 89–100 ms 降到 68–71 ms，但前一次运行在构建后立即开始，这个差别同样可能是机器状态造成的。ratio、keys、save 无变化。

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

- [x] 两条快速路径与事务路径结果逐字节一致。
- [x] 基准 ratio 场景在 2000 个标签下的 p95 与 100 个标签时处于同一数量级（写出数字）。只测到 1000 个标签，见状态。

**风险**：如果将来给激活增加新的副作用（例如历史记录），必须同时更新快速路径；在函数注释中写明“新增状态时同时检查 activateTab 快速路径”。

**状态**：已完成（提交标题 “Skip the full transaction for activation and split ratios”）。实现与核对结果：

- 第 4 条按原行为处理：激活已经活动的标签也递增 revision 并返回成功，没有提前返回。三条快速路径只在索引有效时使用；计数需要回绕时（`orderCounter_` 达到 2^53 − 1 且要写新的访问序号）退回事务路径。共用的私有函数 `activateIndexed()` 和头文件注释写明了“激活增加新状态时同时更新快速路径”。
- 逐字节一致的依据：事务路径在提交前还会执行 `synchronizePinnedLayouts()` 与 `reconcilePinnedRestoreLayouts()`。核对两者的实现：对已经通过校验的模型，前者的 `prune()` 保留全部固定组且不插入新节点，后者的允许集合与 `validate()` 的条件相同，两者都返回原指针，因此快速路径不调用它们也得到相同结果。
- 新增 `activationFastPathMatchesTransaction`（同组、跨组、固定组、跨窗口、已活动、窗格切换、错误窗格、空窗格、不存在的标签与窗格、计数回绕前后，每步比较 `toJson()` 字节、revision 和错误文本）与 `ratioFastPathTouchesOnlyOwnerTree`（标签树、中央树、固定树三种所有者，其他树的指针不变；越界、不存在、叶节点返回失败且 revision 不变）。测试通过 `LayoutModelTestAccess::invalidateIndex()` 让副本走原事务路径。
- 验证：`model`（102 行）、`tab-ordering-model`、`hierarchy-conversion`、`pinned-layout`、`pinned-positions`、`movement-model`、`movement`、`focus-mode`、`shortcut-dispatch`、`tab-group` 十个套件全部通过。1.1 的跨版本对比驱动对本步代码再跑一次，180 次操作的输出与 `3431796` 完全相同（含计数回绕）。
- 模型单独计时（临时驱动程序，-O2，1000 个标签、40 组、每个标签两个窗格，各 2000 次）：`activateTab` p50 0.65 ms → 0.1 µs，`activatePane` 0.65 ms → 0.1 µs，`setSplitRatio` 0.68 ms → 0.1 µs。原来每次焦点变化做两次事务，即约 1.3 ms。
- 基准：`bench-1.2-before.json` 是 1.1-after 那次运行的副本（代码与本步之前相同，没有重跑）。ratio 场景 1000 个标签 p50 6.3 ms → 5.9 ms、p95 12.1 ms → 10.4 ms，100 个标签 p95 2.2 ms，处于同一数量级；剩下的耗时主要在 `layout.setRatio` 命令里对所有分割器的 `findChildren`（1.4 处理）。基准没有焦点切换场景，所以 R17 的收益只由上面的模型计时体现。close／move／split 无变化（约 4 s）。2000 个标签这一档没有运行。

---

## 1.3 去掉序列化比较（R5）

**目标**：`addTabToNewGroup()`、`requestCloseAllWindows()` 在回调没有修改模型时，不再为检测重入修改而序列化整个模型。

**前置**：1.1（不依赖，但改同一文件，顺序执行避免冲突）。

**涉及文件**：`window_manager.cpp`（`addTabToNewGroup`、`requestCloseAllWindows`）、`window_manager.hpp`（私有 `saveViewLayout()`）、`layout_persistence.cpp`（`saveLayout()` 拆分）、`layout_model.{hpp,cpp}`（`stateId()`）、`modules/window-system/README.md`、外部宿主夹具、`layout_model_test.cpp`、`workspace_test.cpp`。

**修订（2026-10-19，实施前核对）**：原方案假设“每条成功修改都递增 revision，所以只比 revision 就够”。核对结果是 `LayoutModel` 的公开修改确实都经过 `transact()` 或 `fromJson()` 并递增 revision，但 `WindowManager` 还会整体替换模型，revision 因此会回退或重复：

- `restoreLayout()` 用新建的 `LayoutModel` 读入快照，revision 从 1 重新开始。现有用例 `tab_group_test` 的 `adjacentPreparationPreservesARestoredModelWithTheSameRevision` 正是在回调中恢复出 revision 相同、内容不同的模型，并断言 `addTabToNewGroup()` 必须放弃提交。只比 revision 会让这条用例失败。
- `DragController::cancel()` 取消拖出时把模型赋回拖动开始时的副本，revision 回退到当时的值。

另外，原第 2、4 条打算在 `requestCloseAllWindows()` 中只比较 `d->options` 的大小、不再比较工具条等窗口状态，这与现有用例 `closeAllRejectsChangesDuringCallbacks` 的 `options`（侧栏开关）和 `placement`（移动窗口）两行冲突：它们要求回调修改窗口状态后拒绝关闭；`closeAllAllowsUnchangedFocusAfterApproval` 又要求回调中激活已活动标签（revision 变化、内容不变）后照常关闭。按“不改现有预期”的规则，方案改为：

**实现**：

1. `LayoutModel` 增加 `stateId()`：进程内唯一的状态标识。副本共享同一个值；`transact()` 提交、`fromJson()` 成功以及 1.2 的快速路径都分配新值。标识相同说明记录完全相同，即使 revision 因整体替换而重复。它不进入 `toJson()`，也不参与 1.2 的逐字节比较。
2. `addTabToNewGroup()`：删除 `originalModel = d->model.toJson()` 和对应比较，改为 `revision` 与 `stateId()` 都未变。
3. `requestCloseAllWindows()`：
   - 模型：保存一份隐式共享的副本（O(1)）。`stateId()` 未变即视为未变；只有在回调确实提交过修改时，才把当前模型与副本各序列化一次比较内容（保留“激活已活动标签仍可关闭”的行为）。
   - 窗口状态：`saveLayout()` 拆出不含模型的部分 `saveViewLayout()`（窗口位置、选项、工具条、窗格描述、配置），回调前后比较它。工具条、侧栏开关和窗口位置的变化仍然阻止关闭，与现有用例一致。
   - 原有的窗口与内容控件身份比较不变。
4. `revision` 的语义不变：单个模型内的修改计数。`DragController::cancel()` 与 `restoreLayout()` 的回退行为不改（现有用例断言恢复后的 revision 为 1）。
5. 行为差别：`addTabToNewGroup()` 原来在回调恢复出“revision 相同且内容相同”的模型时会继续提交，现在因为 `stateId()` 不同而放弃。恢复会重建内容控件，放弃提交更稳妥；没有现有用例依赖原行为。

**测试**：

- `layout_model_test`：`stateIdChangesOnEveryCommit`：副本共享标识；每种成功修改（事务、`fromJson`、激活与比例快速路径）都换新值；失败操作不变；用同一份 JSON 读入两个模型得到不同标识。
- `workspace_test`：`closeAllRejectsModelChangeFromGuard`：关闭许可回调中执行 `renameTab`，断言关闭被拒绝、窗口与标题保留；`closeAllRejectsToolbarEditFromGuard`：回调中解除工具条锁定，断言关闭被拒绝（窗口状态仍参与比较）。
- 现有 `tab_group_test` 的回调恢复、修改、销毁用例和 `workspace_test` 的全部关闭用例保持通过。

**验证**：`window-system.tab-creation`、`window-system.tab-group`、`window-system.memory-lifecycle`、`window-system.widgets`、`window-system.window-placement`、应用 `tools/tests/application-exit-test.ps1`。

**状态**：已完成（提交标题 “Compare model identity instead of serializing for reentrancy checks”）。实际结果：

- 先验证修订的必要性：把 `addTabToNewGroup()` 临时改成只比 revision（原方案），现有用例 `adjacentPreparationPreservesARestoredModelWithTheSameRevision` 失败（`addTabToNewGroup` 提交了候选模型），记录在 `out/diagnostics/1.3-revision-only-check.txt`；恢复后通过。
- 新增 `stateIdChangesOnEveryCommit`、`closeAllRejectsModelChangeFromGuard`（批准回调与关闭通知两行）、`closeAllRejectsToolbarEditFromGuard`。现有 `closeAllRejectsChangesDuringCallbacks`（12 行）、`closeAllAllowsUnchangedFocusAfterApproval`、`closeAllRejectsContentReplacementWithTheSameIds` 与 `tab_group_test` 的回调用例保持通过。
- 验证：`model`（103 行）、`widgets`（57 行）、`tab-creation`、`tab-group`（101 行）、`memory-lifecycle`、`window-placement`、`drag-interaction` 全部通过。独立模块构建、安装和外部宿主夹具：独立构建的 52 个套件中 `native-frame`、`chrome-interaction` 失败；`native-frame` 的命中点落在 Claude 桌面应用窗口（`WindowFromPoint` 返回 0x1c005c4，进程 `claude`，矩形 402,6–1799,1032），属于遮挡；`chrome-interaction` 1 行（`overflowToggleRestoresHoverAndPressState`）单独重跑 3 次均 58 行全部通过，未能复现。安装后的外部宿主夹具（含新增的 `stateId()` 调用）通过。首次失败记录在 `out/diagnostics/1.3-standalone-*`。
- 1.1–1.3 完成后的 Release 全量（`tools/build.ps1 -Configuration Release -Test`）：59 个套件中 58 个通过；2178 行通过、20 行失败，失败全部是 `native-frame`，原因同上（同一个 Claude 窗口遮挡），记录在 `out/diagnostics/1.3-full-release-native-frame-first.txt`。由于 `-Test` 失败时脚本不打包，随后单独运行 `-Package` 更新了本工作区的 `out/package/muxora-preview/`，`application-exit-test.ps1` 的三窗口退出、重启和保存失败检查通过。
- 基准：`bench-1.3-before.json` 是 1.2-after 那次运行的副本（代码相同）。1000 个标签时各场景与之前相同（close p95 4.03 s → 3.92 s，属于波动）：基准里 `addTabToNewGroup` 只在创建时调用 40 次，`requestCloseAllWindows` 不在基准中。省下的是这两处在常见路径上对整个模型的两次 `toJson()`；作为量级参考，1000 个标签时 save 场景（`saveLayout()` + `toJson()`）约 11 ms。没有单独测这两个函数。

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

**状态**：已完成（提交 `efd2356`）。实现与核对结果：

- 第 4 条按“公开最小查询”处理：`LayoutModel::NodeOwner` 与 `ownerOfNode()` 移到公开区，头文件注释写明只描述布局树、已提交模型上 O(1)。索引无效时（默认构造的模型、测试中作废索引的副本）改为扫描，不再直接返回空。模块 README 增加一段说明，外部宿主夹具在恢复布局后对未知 ID、各窗口中央树和各标签窗格树的分割节点各调用一次。
- `layout.setRatio` 只查所属标签视图或窗口表面的映射，`isVisibleTo(owner)` 判断和 `QSignalBlocker` 保留。中央树、固定树的分割器都在 `buildTree()` 中登记；固定区域包装用的分割器没有分割节点，不登记。
- 新增 `ratioCommandUpdatesOnlyTheOwningSplitter`（标签节点、中央节点两行：被修改的分割器按比例变化且对象不变，另外两个分割器的 `sizes()` 不变）。
- 验证：`model`（103 行）、`movement`（47 行）、`pane-minimum`、`pinned-ui` 通过。外部宿主夹具在 1.6 完成后与安装导出一起验证（见 1.6 状态）。
- 基准（`bench-1.4-before.json`／`-after.json`）：1000 个标签时 ratio p50 6.2 ms → 5.1 ms、p95 11.3 ms → 9.8 ms；100 个标签 p95 2.3 ms → 2.1 ms。剩下的时间主要是 `setSizes()` 本身带来的子控件布局，不再随视图数查找。close／move／split 不变（约 4.0 s）。

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

- [x] 新测试在本步实现前失败（框和分割器指针会变化），实现后通过。
- [x] 全量无新增失败。1.5 提交时漏跑了 `pinned-ui`，它的两行从本步起失败，见 1.6 状态中的用例修订。
- [x] 基准中 close／move 场景下降，或说明没有下降的原因（`TabContent` 仍在改挂，1.6 之后才见效）。

**风险**：

- 焦点投影（`FocusProjection`）在 `rebuild()` 开头 `restore()`；复用路径也必须先 `restore()` 再 `setFocusedPane()`，否则窗格全屏状态会残留。
- `geometryUpdateDepth` 期间的延迟删除逻辑只在真正重建时需要，复用路径不涉及。

**状态**：已完成（提交 `fd90b18`）。实现与核对结果：

- 记录的构建输入合成一个 `BuiltPane` 列表（窗格、内容、上栏、下栏，按叶子顺序），另记树指针和分割条宽度。`canReuse()` 还要求内容控件仍挂在原来的框里，被别处移走的内容会触发重建。
- 复用路径先 `focusProjection_.restore()`（`rebuild()` 第一行，原样保留），再同步活动窗格和 `setFocusedPane()`。
- `refresh()` 先算出可复用的标签，再只收回其余标签的内容和窗格栏。如果判定可复用之后树又变了（回调中修改模型），`rebuild()` 在删除旧框之前把仍挂在其中的内容和栏收回存储控件，不会经由旧框删除活内容。
- `view_reuse_test.cpp` 新套件标为 `desktop`（后续用例要显示窗口）。在上一提交上运行：`unrelatedTabsKeepTheirViewWidgets`、`changedTreeRebuildsOnlyThatTab` 失败，`themeMetricChangeRebuilds` 通过（它防止复用吞掉主题变化），记录在 `out/diagnostics/1.5-view-reuse-before.txt`。实现后 5 行通过。用例在失败时曾解引用已删除的框（比较 `QSet<QWidget*>` 时 QtTest 会打印对象名），改为通过 `QPointer` 比较。
- 验证：`view-reuse`、`widgets`、`pane-settings`、`pane-bar-policy`、`pane-minimum`、`movement`、`focus-mode`、`tab-creation`、`tab-group`、`memory-lifecycle` 通过。`drag-interaction` 在 ctest 中有一行（`escapeCancelsBeforeAnyContentTransfer`）失败一次，单独重跑整套 3 次、该行 10 次均通过，记录在 `out/diagnostics/1.5-drag-interaction-first.txt`，按间歇性桌面用例处理。
- 基准（`bench-1.5-after.json`）：1000 个标签 close p95 3.97 s → 3.94 s、move 3.96 s → 3.92 s、split 4.02 s → 3.81 s，没有明显下降。原因如计划所料：每次 `refresh()` 仍把全部 `TabContent` 收回存储控件再挂回、并重建全部 `GroupTabs`；1.6 的测量进一步表明，主要成本是这些子树在存储控件和窗口之间改挂（见 1.6 状态）。

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

**状态**：已完成。1.6a 提交 `8dadde4`，1.6b 提交 `a9bd787`，另有一个测试修订提交 `fdd6d9c`（见下文，需要评审）。

1.6a 实现与计划的差别：

- `takePage()` 从标签条移除标签时屏蔽 `currentChanged`（只在表面重建时使用，旧组随即丢弃），页面已经离开栈时返回空。`DragTabBar` 增加 `tabRemoved()`，让颜色装饰的下标跟随标签移动。
- 第 3 条的判断用视图当前的顶层窗口与目标窗口控件比较（`view->window()`），不读属性。
- 没有被新组接收的页面（标签移到了自动隐藏侧栏）在删除旧根之前收回存储控件，`deferredRootDeletionKeepsCarriedPages` 覆盖这种情况。
- 视图保持挂载时，在可见状态下重建窗格树；`QLayout` 插入可见父控件会把新树的显示推迟到下一轮事件循环，所以 `TabContent::rebuild()` 在可见时立即显示新树（`mountedRebuildIsVisibleImmediately`）。

1.6b 实现与计划的差别（较大，需要评审）：

- **组签名比计划多**：除标签列表、活动标签、位置、侧边外，还包括标题、颜色、图标开关、`TabAppearance` 和主题修订号。计划是复用后调用 `setTabMetadata()`、`setAppearance()` 更新元数据，但这两个函数每次都会触发 `updateBarGeometry()`，对 1000 个标签逐个调用正好抵消复用的收益。现在元数据变化（例如重命名、改颜色）只重建所在的组。`setTheme()` 原来不刷新视图，现在递增一个主题修订号，下一次刷新重建全部标签条。拖拽进行中一律重建（标签条上有拖动标签、插入预览、紧凑显示等临时状态）。
- **按计划实现后 close 只降到 3.2 s**（`bench-1.6b-intermediate-b966355.json`，提交 `b966355` 后被并入 `a9bd787`）。临时计时显示：500 个标签时一次表面重建 730 ms，其中 633 ms 是把复用的 `GroupTabs` 加入新分割器。窗口设置了样式表，任何子树改挂都会让 Qt 为整棵子树重新解析样式、字体和调色板；即使在同一窗口内移动，每个标签的页面子树也要约 0.5 ms。这也是原来约 4 s 的主要来源：每次刷新都把全部 `TabContent` 从存储控件挂回窗口。
- **因此增加“结构不变时就地更新”**：表面记录分割结构（中央树和固定树的节点 ID、方向、叶子组，不含比例）。结构不变时保留根、全部分割器和未变化的标签条，变化的组新建后用 `QSplitter::replaceWidget()` 换入原位置，侧栏轨道照旧重建，比例变化的节点直接对现有分割器 `setSizes()`。结构变化（新建或删除组、固定／取消固定、移动组）时才构建新树，并把未变化的标签条移入，这条路径仍与标签总数成正比（见“发现”）。
- **布局过程中的改挂**：宿主可能在内容的 resize 回调里修改布局，这时外层分割器正在遍历自己的子控件列表。把复用的标签条从这样的分割器中移走会改动它正在遍历的列表；`pinned-ui` 的 `restoringPinnedExtentsMayReplaceUiFromContentResize` 因此崩溃过（`out/diagnostics/1.6b-pinned-ui-first.txt`）。处理方式：就地更新只用 `replaceWidget()`（不改列表）；结构性重建时，若处于组分割器的布局过程中，复用的标签条先用 `replaceWidget()` 换出（留下空占位控件随旧根删除），否则直接移动。“布局过程”由新的 `SplitterLayoutPass` 计数：组分割器及其手柄在处理事件期间、`layout.setRatio`、固定区域恢复和焦点投影调用 `setSizes()` 期间计入，`WindowManager` 的几何更新深度也算在内。
- 回调捕获的是组 ID、窗口 ID、表面和标签条自己的 `bar`，复用后不需要重设。`drag_controller.cpp` 不缓存 `GroupTabs*`，只通过表面的查询函数访问。

测试：

- `view-reuse` 共 14 行通过。计划中的 `tabViewsStayInTheirWindow` 要求“组 A、B 所有剩余 `TabContent` 的 `ParentChange` 计数为 0”。组 A 关闭了一个标签，签名变化，它的标签条要重建，剩余页面必然从旧标签条移到新标签条。现在的断言是：所有剩余视图从未以存储控件为 parent、对象不变；未变化的组 B 标签条对象不变、页面 `ParentChange` 为 0。`crossWindowMoveReparentsOnlyTheMovedTab` 同样处理：任何窗格内容控件都没有 `ParentChange`，只有被移动的标签视图跨窗口，未变化的组不受影响。
- `mutationFromSplitterLayoutKeepsReusedStrip`：分别由 `layout.setRatio` 和 Qt 发出的分割器 resize 驱动，回调中“关闭另一组的一个标签”（就地替换）或“关闭另一组的最后一个标签”（结构变化）。去掉 `SplitterLayoutPass` 判断后，两个结构变化行和 `pinned-ui` 崩溃（`out/diagnostics/1.6b-*-without-layout-pass.txt`）。
- `reusedStripFollowsMetadataAndTheme`：重命名只重建所在组，另一组的标签条不变；改主题后下一次刷新重建全部标签条，图标尺寸跟随新主题。

现有用例修订（提交 `fdd6d9c`，需要评审）：`pinned_ui_test` 的 `restoringPinnedExtentsMayReplaceUiFromContentResize(rebuild-surface)` 在固定窗格的 resize 回调中新建相邻组，原断言该窗格的 `PaneHost` 被替换（隐藏、内容移出、稍后删除）。新建相邻组不改变这个标签的窗格树，而“无关操作不重建、不改挂内容”正是本阶段的目标，所以这三条断言测试的是旧的全量重建实现。依据：阶段目标和 1.5 的计划；首次失败记录 `out/diagnostics/1.6a-pinned-ui-first.txt`。保留覆盖的方式：新增 `rebuild-tab` 行，在同一回调中拆分该标签自己的窗格，原有的“几何更新期间延迟删除旧框”断言原样作用于这一行；`rebuild-surface` 行改为断言框保留、内容仍在其中，且表面的固定区域分割器仍在布局返回后被替换。1.6b 就地更新后，`rebuild-tab` 行的固定区域改为断言保留（只有窗格树变化，分割结构不变），这一行是本组新增的，`rebuild-surface` 行仍要求替换。其他行不变。

验证：

- 1.6a：全部 55 个窗口模块套件中 52 个通过；`native-frame` 被 Claude 桌面窗口遮挡（`WindowFromPoint` 命中 0x1c005c4，进程 `claude`，矩形 402,6–1799,1032）；`chrome-interaction` 的 `overflowToggleRestoresHoverAndPressState` 失败一次，单独重跑 58 行通过；`pinned-ui` 两行即上述修订。首次失败记录 `out/diagnostics/1.6a-*-first.txt`。
- 1.6b：全部 55 个窗口模块套件通过（这次 `native-frame` 也通过）。
- 本组完成后的 Release 全量（`tools/build.ps1 -Configuration Release -Test`）：60 个套件中 59 个通过，2195 行通过、20 行失败，失败全部在 `native-frame`，同样命中 Claude 窗口 0x1c005c4，记录在 `out/diagnostics/1.6-full-release-native-frame-first.txt` 与 `out/test-results/phase1-1.6-full-release-summary.json`。随后 `-Package` 覆盖了本工作区的 `out/package/muxora-preview/`，`application-exit-test.ps1` 通过。独立模块按 Release 构建（不含测试）并安装，外部宿主夹具（含 `ownerOfNode()` 调用）构建并通过。

基准（1000 个标签 p50／p95；`bench-1.4-before.json` 为改动前）：

| 场景 | 改动前 | 1.6a 后 | 1.6b 后 |
| --- | --- | --- | --- |
| 关闭中间一个标签 | 3.88 s／4.01 s | 3.60 s／3.63 s | 55 ms／65 ms |
| 移动一个标签到另一组 | 3.91 s／4.00 s | 3.59 s／3.62 s | 80 ms／103 ms |
| 拆分一个窗格 | 3.90 s／4.02 s | 3.63 s／3.74 s | 67 ms／89 ms |
| 创建到 1000 个标签（1 次） | 1303 s | 1257 s | 92 s |
| 一次“添加 + 关闭”（1000 次） | 8.04 s／8.28 s（阶段 0 基线） | 未测 | 97 ms／126 ms |

- 100、500 个标签时 close p95 为 22 ms、46 ms（改动前 72 ms、972 ms）。1.6a 单独时 100 个标签反而略慢（close p95 72 ms → 84 ms），因为它仍重建全部标签条，又多了逐页取出的开销。
- 1.6b 后 ratio 场景 1000 个标签 p50 2.2 ms、p95 17.4 ms（1.4 后为 5.1／9.8 ms），save 场景 p95 23 ms（之前约 12 ms，这部分代码没有改动）。两者只在这一次运行中出现，原因没有查明，记为待复测，不算作改进或退化。
- 1000 个标签时每标签私有提交 122 KiB（改动前）→ 83 KiB，控件数不变（每标签 3.92 个）。
- 完成标准中“close p95 显著低于基线”已达到；`ParentChange` 计数测试按上面的解释通过。结构变化（新建或删除组）的成本仍与标签总数成正比，没有单独计时。

### 发现（1.4–1.6）

- **改挂成本**：在设置了样式表的窗口里，任何控件子树改挂都会让 Qt 为整棵子树重新解析样式、字体和调色板。1.7 及以后修改视图时，避免为“暂存”而把控件移出窗口。结构变化时的整体重建仍按标签总数付这笔成本；如果“新建／删除组”在大量标签下仍然偏慢，可以把就地更新扩展到结构变化（只替换变化的分割子树）；[性能计划 §4](../review-2026-10/performance-plan.md#4-主题从样式表转向调色板与-proxystyle) 的去样式表方案也会降低这笔成本。
- **布局过程中的重入**：组分割器在布局子控件期间，宿主回调可能同步修改布局。`SplitterLayoutPass` 只覆盖组级分割器。标签内部的窗格分割器原来就存在同类风险：回调修改同一标签的窗格树时，`TabContent::rebuild()` 会在 `geometryUpdateDepth` 为 0 时同步删除旧树。这与本组改动无关，没有处理，也没有专门的用例。
- **循环内存**：阶段 0 记录的“1000 个标签循环 1000 次后私有提交增加 37 MiB”不是泄漏，而是分配器保留。依据见 [性能计划 §6](../review-2026-10/performance-plan.md#6-测量与预算)。

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
4. `GroupTabs`：允许页面为空。每个尚未创建视图的标签在 `QStackedWidget` 中放一个无布局的轻量 `QWidget` 占位；首次切到该索引时，通过 `pageRequested(TabId)` 让 `DockSurface` 调 `ensureTabView()`，再用真实页面替换占位控件。`pageRequested` 在设置当前索引之前安装，活动页首次构建不会停在占位页。
   - 一个占位 `QWidget` 仍然比 `TabContent + 分割器 + 窗格框 + 内容挂接` 小得多；如果 0.1 基准显示占位本身仍是负担，再考虑改用不依赖 `QStackedWidget` 的页面容器。
5. `refresh()` 中第 442–448 行“为每个模型标签创建并重建视图”改为：只对已有视图的标签执行 `rebuild()`；每个组的活动标签调用 `ensureTabView()`（活动标签必须立即可见）。
6. 内容的可见性：未创建视图的标签，其内容控件留在存储控件中、隐藏，Qt 不会绘制它们。
7. **内容可见性通知**（新增公开 API，供以后终端释放 GPU 资源）：

   ```cpp
   struct ContentVisibilityChange { PaneId pane; bool visible; };
   // Called after a refresh/activation settles, coalesced per event-loop turn.
   void setContentVisibilityHandler(std::function<void(const QVector<ContentVisibilityChange>&)> handler);
   ```

   实现：在 `refresh()`、`activateTab()`、窗口最小化／恢复（`WorkspaceWindow::changeEvent`）、侧栏面板开关之后，计算每个窗格“当前是否显示”（内容控件 `isVisible()`），与上次记录比较，把变化放入待发送列表，用 `QTimer::singleShot(0, ...)` 合并发出。跨顶层窗口移动是同步例外：先隐藏旧视图并报告相关 Pane 为隐藏，再改挂视图；通知可重入，后续每次使用它之前重新检查模型和管理器。处理函数可能销毁管理器，按现有方式保留并检查 `QPointer`。

**测试**：

- `restoredInactiveTabsCreateViewsOnFirstActivation`：一组 100 个标签，只激活最后一个；断言 `TabPage` 数量为 1；通过标签条首次激活另一个标签后增加到 2，再逐个激活时继续递增，宿主内容实例不变。
- `pendingTabBarsFollowLazyActivationAndRemoval`、`pendingTabBarsSurviveHierarchyConversion`、`restoringLayoutDiscardsPendingTabBars`：首次显示前设置、替换、清空和层级转换附加栏，关闭标签或恢复新布局时释放未交付栏。
- `contentVisibilityNotificationsAreCoalesced`、`contentVisibilityTracksWindowStateAndPeek`、`crossWindowMoveReportsHiddenBeforeReparent`：安装后发送完整快照；同一事件循环内多次切换只通知一次；最小化、恢复、临时侧栏和跨窗口改挂前报告预期显隐。
- 现有所有测试必须通过；特别是依赖“所有标签都有视图”的测试，如果断言了非活动标签的框，说明测试依赖实现细节，按测试规范提交独立证据后修订，并保留等效覆盖。

**验证**：全量一次；基准 `restore` 和 memory 场景。`create` 会逐个添加并激活标签，按“激活过的标签保留视图”的语义不能衡量本步骤收益，因此基准另建完整模型快照，再调用一次 `restoreLayout()`。改前和改后使用同一基准源码，1000 个标签各跑三次；产物为 `bench-1.7-before*.json` 和 `bench-1.7-after*.json`。

**完成标准**：

- [x] 基准中 1000 个标签的每标签控件数和私有内存下降（写出数字）。
- [x] 外部宿主夹具调用一次 `setContentVisibilityHandler()`。
- [x] `modules/window-system/README.md` 说明可见性通知的时机。
- [x] [通用窗口系统设计](../../design/window-system-design.md) 写明内容控件的挂接契约：同一顶层窗口内移动不改 parent；跨顶层窗口只改一次 parent，并在此之前通过可见性通知让宿主释放 GPU 资源（来自 [性能计划 §5](../review-2026-10/performance-plan.md#5-gpu-渲染与终端视图契约)，终端接入前必须成立）。

**风险**：拖拽中的标签预览（`dragLabelPixmap`）和完整标签列表可能读取非活动标签的视图；逐个检查 `drag_controller.cpp`、`tab_strip.cpp` 中对页面的访问。

**状态**：已完成。实现提交 `bc16ecf`，基准场景与按场景运行入口提交 `b504ee8`。

实现与计划的差别：

- 除各组活动标签外，临时展开的自动隐藏标签和组全屏、窗格全屏的目标也在显示时创建视图。
- 基准改用新增的 `restore` 场景（见上文“验证”），`create` 测不出本步骤的收益。

1000 个标签完整恢复的三次中位数从 4687.85 ms 降到 1744.71 ms，每标签私有提交从 94658.56 B 降到 71479.30 B，每标签控件从 3.92 个降到 2.96 个，见 [验证记录](../../development/verification-log/2026-10.md#17-非活动标签按需创建视图2026-10-24)。

---

## 1.8 启动时恢复会话对象，界面按需创建

> 用户要求恢复后所有连接都在（[启动时恢复会话设计](../../design/session-restore-design.md) §3），所以：**会话对象（连接、终端状态）在恢复时为全部标签立即创建，只有界面控件按需创建。**

**目标**：恢复布局时，窗口系统把全部标签的内容描述一次性交给宿主，宿主据此立即创建全部会话对象；每个窗格的界面控件在第一次显示时才通过工厂创建。

**前置**：1.7。

**涉及文件**：`window_manager.hpp`（新类型与重载）、`layout_persistence.cpp`（`restoreLayout`）、`window_manager.cpp`（`createContent`、新增 `ensurePaneContent`）、`src/app/main.cpp`（启用）、`src/workspace_host/placeholder_content.cpp`（占位内容拆成“对象 + 视图”，用来验证路径）、外部宿主夹具。

**实现**：

1. 新增：

   ```cpp
   struct RestoredContent {
       TabId tab; PaneId pane; TabOptions options;
       bool visible = false;          // 恢复后是否立即显示（各窗口的活动标签、可见窗格）
   };
   struct RestoreOptions {
       bool deferWidgets = false;     // true：界面控件在第一次显示时才创建
       // 模型提交后、创建任何控件之前调用一次。顺序：可见的在前，其余按 visitedOrder 从近到远。
       std::function<void(const QVector<RestoredContent>&)> contentRestored;
   };
   OperationResult restoreLayout(const QJsonObject& snapshot, const RestoreOptions& options);
   ```

   原 `restoreLayout(snapshot)` 等价于 `deferWidgets = false` 且不调用 `contentRestored`，现有宿主行为不变。

2. `contentRestored` 的契约（写入头文件注释）：只调用一次；回调中宿主可以创建业务对象，但不能修改布局（检测到 `stateId()` 变化时恢复失败并回滚）；回调可能销毁管理器，按现有方式检查 `QPointer`。
3. 推迟的窗格：`d->descriptors` 保存描述，`d->panes` 中没有条目。私有 `QWidget* ensurePaneContent(PaneId)`：没有控件时调用工厂 `ContentFactory(options, pane)`——宿主用 `pane` 找到恢复时已经创建的会话对象并返回它的视图；然后按 `addTab` 中的方式设置属性、焦点策略、parent，插入 `d->panes`。工厂返回空指针时放一个“无法创建内容”的占位并通过 `commandFailed` 报告。
4. `ensureTabView()`（1.7）在构建视图前，对该标签的每个窗格调用 `ensurePaneContent()`。
5. `paneWidget(PaneId)`：启用 `deferWidgets` 时，从未显示过的窗格返回 `nullptr`。宿主不能用它判断会话是否存在。
6. 关闭许可回调收到的标签可能包含没有控件的窗格；`approveClose()` 中 `contents` 记录为空指针，比较逻辑不变。关闭从未显示的标签时工厂不被调用；宿主在关闭通知中按 `PaneId` 释放会话对象。
7. 宿主获得优先级变化：1.7 的内容可见性通知（`setContentVisibilityHandler`）在窗格第一次显示时发出，宿主据此把该会话提到连接队列队首（阶段 7.10）。
8. 应用 `main.cpp`：恢复时传 `{.deferWidgets = true, .contentRestored = ...}`。占位内容拆成一个轻量对象（保存标题、类型、可编辑文字）和一个视图，用来验证“对象先建、视图后建、内容身份稳定”的整条路径。

**测试**：

- `restoredContentIsReportedOnceInPriorityOrder`：保存含 50 个标签、两个窗口的布局，恢复时 `contentRestored` 被调用一次，包含 50 项，可见项在前，其余按访问顺序。
- `widgetsAreCreatedOnFirstShow`：工厂只为可见窗格调用；逐个激活其他标签后调用次数增加，同一窗格只调用一次，内容身份稳定。
- `closingNeverShownTabsSkipsTheFactory`：关闭从未显示的标签，工厂不被调用，关闭通知包含其 `PaneId`。
- `layoutChangesInsideContentRestoredAreRejected`：回调中修改布局，恢复失败且模型回到恢复前。
- `managerDestroyedInsideContentRestored`：回调中销毁管理器，不崩溃。
- 外部宿主夹具使用新重载一次。

**验证**：`window-system.widgets`、`window-system.view-state`、`window-system.tab-creation`、新测试、应用部署夹具（重启恢复）；基准 create 场景前后对比。

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

**状态**：已完成（提交标题 “Index commands by ID”）。`State` 增加按稳定 ID 指向命令向量下标的 `QHash`；新增命令追加后登记，删除后按剩余顺序重建，查找不再遍历全部命令。注册、删除、回调重入和菜单相关套件通过，验证见 [记录](../../development/verification-log/2026-10.md#110-命令注册表-id-索引2026-10-24)。

---

## 1.11 结构变化与同步批量关闭的成本（先测量）

**目标**：知道“新建／删除组”这类结构变化，以及没有事件循环时连续关闭大量标签的实际成本，再决定是否需要改实现。来源：[1.4–1.6 发现](#发现1416) 中“结构变化仍按标签总数付成本，没有单独计时”，以及 [审查记录 · 关于内存泄漏](../review-2026-10/code-review.md#关于内存泄漏) 中同步批量关闭的峰值。

**前置**：1.7（它改变哪些标签有视图，先做 1.7 再测才有意义）。

**涉及文件**：`modules/window-system/benchmarks/layout_scale_benchmark.cpp`（`timed()`、`settleEventLoop()`、`recordCycle()`）、`tools/bench.ps1`；若决定优化，再加 `docking/dock_surface.cpp`（`updateInPlace()`、`rebuild()`）。

**实现**：

1. 基准新增场景 `group`：100／500／1000 个标签时，把一个标签移到新组（新建组）再移回（删除组），各记录单次 `refresh()` 的 p50／p95。
2. 基准新增场景 `batch-close`：在同一个函数调用里连续关闭 200 个标签，期间不调用 `processEvents()`，记录关闭前、全部关闭后（尚未处理延迟删除）、`settleEventLoop()` 之后三个时刻的私有提交和 `QObject`／`QWidget` 数。现有 `settleEventLoop()` 每次都会处理延迟删除，覆盖不到这种情况。
3. 按 [工程设计 §4](../../development/engineering-design.md#4-响应速度与性能验收) 的 p95 ≤ 50 ms 判断：`group` 在 1000 个标签时超过就把就地更新扩展到结构变化（只替换变化的分割子树），先在本节补写子步骤再实现；`batch-close` 的峰值若明显高于关闭后的稳定值（写出倍数和绝对值），评估在没有嵌套刷新时直接删除，写明生命周期理由。都没有超标时只记录数字，不改实现。

**测试**：基准场景本身；若改实现，按 1.6 的方式补“未变化的组和视图指针不变”的用例。

**验证**：`pwsh tools/bench.ps1` 的前后两次结果，写进步骤状态和验证日志。

**状态**：未开始。

---

## 1.12 窗格分割器布局期间的重入

**目标**：宿主在标签内部窗格分割器布局子控件期间（例如内容的 `resizeEvent`）同步修改同一标签的窗格树时，不删除正在布局的控件。来源：[1.4–1.6 发现](#发现1416) 中“布局过程中的重入”。

**前置**：1.11（都改刷新路径，串行）。

**涉及文件**：`panes/tab_content.cpp`（`rebuild()`）、`window_manager.cpp`（`geometryUpdateDepth`、`refresh()`）、组级分割器已有的 `SplitterLayoutPass`。

**实现**：

1. 先写失败用例：两个窗格的标签，内容在第一次 `resizeEvent` 里调用 `splitPane()` 或 `closePane()` 修改同一标签；在修复前确认它崩溃或在诊断构建（6.4）下报告释放后使用。
2. 参照 `SplitterLayoutPass`，窗格分割器布局期间把旧树的删除推迟到布局结束（`deleteLater()` 或在 pass 结束后重建），保证正在布局的分割器和窗格框在本次调用返回前仍然有效。

**测试**：`paneLayoutReentryKeepsWidgetsAlive`：上述场景在 Release 和 Debug 下都不崩溃，模型通过 `validate()`，最终窗格数正确。

**验证**：新用例所在套件、`pane-settings`、`memory-lifecycle`。

**状态**：未开始。
