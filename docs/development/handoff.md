# 交接记录

Muxora 由几个助手（目前是 Claude 和 Codex）按额度轮流开发，彼此看不到对方的对话和私有记忆。这份文件是接手时的第一入口：**接手先读最上面一节；用户说要交接时再写新的一节**，工作中的进度在实现计划的步骤状态和验证日志里。上一位中途被打断、没来得及写时，以 git 提交、步骤状态和验证日志为准。规则见 [AGENTS.md · 轮流开发与交接](../../AGENTS.md#轮流开发与交接)。

## 写法

- 最上面一节永远是当前状态，标题写日期和交出的助手。新的一轮把上一节改写成新状态，不往下无限追加；只保留最近 3 节作为背景，更早的看 git 历史和 [验证日志](verification-log/2026-10.md)。
- 每节固定包含下面几项，没有内容写“无”：
  - **最后提交**：交出时 `main` 是否已推送、与 `origin/main` 一致；工作区有未提交的改动时说明是什么。不写提交标题，最新提交以 `git log` 为准。
  - **本轮完成**：做完的步骤和提交标题，各自验证到什么程度。
  - **进行中**：做到一半的步骤、已完成和剩下的部分、卡住的原因。
  - **下一步**：建议接手的人先做什么。
  - **待用户决定**：还没得到答复的问题，以及记在哪份文档里。
  - **请下一位回答**：需要另一位助手回答或评审的问题，格式见下文“助手之间的问答”；下一位接手后先答复。
  - **本轮新决定**：和用户新定下的决定，以及写在哪个 ADR 或文档里。
  - **环境与注意事项**：本机环境的变化、正在运行的程序、已知的偶发问题等（不写用户本机的具体配置，见 AGENTS.md）。
- 只写事实和位置，细节写在对应的计划、ADR、验证日志里，这里给链接。

### 助手之间的问答

助手之间不直接联系（[AGENTS.md](../../AGENTS.md#轮流开发与交接)），问答只有两条路，格式相同：

- **经交接**：写在“请下一位回答”里，等下次交接时由对方答复。
- **经用户转达**：这一步等不了时，提问方暂停这一步，在对话中给用户一段可以原样转发的文字；对方的答复同样写成可以原样转回的文字，由用户带回。

提问写成下面的样子，读的人不看对话也能独立答复：

```text
Q<编号>（<提问方> 问 <答复方>，<日期>）
问题：要对方回答或评审的具体内容，一次只问一件事。
背景：当前提交、做到哪一步、已经确认的事实和数字；列出需要读的仓库文件和提交。
材料：证据放在仓库里或 out/diagnostics/<主题>/，写出路径；不引用会被清理的临时目录。
我的方案：提问方打算怎么做（评审时必填）。
需要的答复：例如“批准／要求修改，并给出条件”或“在 A、B 中选一个并说明理由”。
```

答复紧跟在问题下面：

```text
A<编号>（<答复方>，<日期>，基于提交 <提交号>）
结论：批准／附条件批准／要求修改／回答内容。
条件：逐条编号列出提问方必须做到的事。
仍未解决：答复后依然开着的问题。
```

- 提问方按答复的**全部条件**执行。不同意某一条时，写成新的提问，不能自行删减或改写；向用户转述时也要列出全部条件。
- 答复的结论和条件，连同据此做的改动，写进验证日志或对应的计划；问答本身在下一轮交接时从交接记录中删掉，留下的记录以验证日志为准。
- 答复方要先读完问题列出的文件和提交再答，信息不够就在答复里写明还缺什么，不凭提问方的概括下结论。

---

## 2026-11-07 · Claude · 审定 3.2 清单，修复 Quick 方向导航，启动提速约 0.8 秒

### 最后提交

`main` 已推送，与 `origin/main` 一致，工作区干净。本节随交接文档提交一起推送。

### 本轮完成

- 审查 Codex 从 `86636a0` 之后的 5 个提交。菜单延迟创建没有要改的，两个菜单套件重跑通过；3.2 清单附条件批准。见 [审查记录](verification-log/2026-11.md#审查-codex-的-32-清单顶部基线和菜单延迟创建2026-11-07)。Codex 留下的 18 个临时文件已核对删除。
- `d14f781` 修复 Quick 下的两类失效：
  - 按方向切换、移动、交换、合并组和窗格（Ctrl+Alt+方向键、Alt+方向键等）一直没有反应，因为管理器只从 Widgets 视图取位置；
  - “窗口状态”菜单的最小化、最大化、还原也没有反应。

  `WindowPresenter` 加了 `groupRects`、`paneRects`、`setWindowState`。新用例在旧代码上失败、修复后通过，相关 7 个套件通过，见 [记录](verification-log/2026-11.md#方向导航和窗口状态命令在-quick-下失效2026-11-07)。经验写进 [Qt Quick](lessons/qt-quick.md#管理器里读-widgets-视图的代码在-quick-下要逐处找出来)。
- `7c6404f` 协作经验新增“[迁移测试时发现的产品缺陷要单独上报](lessons/collaboration.md#迁移测试时发现的产品缺陷要单独上报)”，起因是清单里 6 行提到这个缺陷却没有上报；审查清单加了对应检查。
- `69f6dae` 写下 [3.2 审定结论](../roadmap/implementation/phase-10-qt-quick.md#审定结论claude-2026-11-07)，包括 3.3 的条件和顺序、7 个改回 A 试迁的方向导航用例、3.4 处理 C 类的 5 条规则；计划里的源码数改为 57。
- 后一个提交做了 10.5 后续第 1、4 项的耗时归因，见 [归因记录](verification-log/2026-11.md#quick-启动和顶部远距切换的耗时归因2026-11-07)：
  - 启动约 1.2 秒在 Qt 第一次加载字体，其中约 0.9 秒是多字体的界面字体让 Qt 一次加载全部后备字体，与菜单无关；
  - 顶部远距切换掉的那一帧由阴影逐帧重画和遮罩图层重建两笔叠成，单独去掉一处不够。

  第 1 项的改法写进计划并暂缓。经验写进 [Windows 平台](lessons/windows-platform.md#多字体的界面字体会在第一次量字时加载全部字体族)。
- 按用户要求用和 VS Code 一样的机制处理启动字体开销：界面字体确定时把后备字体表预先填进 Qt 的缓存，表尾改用 Windows 系统后备的选择，不再加载全部字体族。决定和代价写在 [ADR 0014](../adr/0014-prefill-font-fallbacks.md)，这里用到了 Qt 的私有接口。
  - 启动到第一帧从 1663–2026 毫秒降到 913–929 毫秒（Release 三轮）。
  - 中英文、常用符号和表情的显示字体不变，少见文字改由 Windows 标准字体显示。
  - 新增 `font-fallback` 套件，去掉预填时它会失败。
  - 安装后的模块配置补了 `GuiPrivate` 依赖。

  见 [记录](verification-log/2026-11.md#windows-界面字体的后备字体表预填2026-11-07)。
- 交出前补的两项：
  - `font-fallback` 的检查从“加载的字体族少于一半”改成“没有全部加载”，字体少的电脑上不会误报；
  - `quick-menu-bar` 偶发失败已查明：真实光标停在菜单栏上，Qt 把打开的菜单切到悬停的按钮。现在每个用例开始前先移开光标，Debug 连续 30 次、Release 连续 10 次通过，见 [记录](verification-log/2026-11.md#quick-menu-bar-偶发失败的原因真实光标停在菜单栏上2026-11-07)。
- 没有跑全量，没有在应用里试用方向导航、窗口状态命令和字体变化，Release 预览没有更新。

### 进行中

无。由 Claude 以后做的：

- 10.5 后续第 1 项（远距切换的两处渲染改造），排在第 2、3、5 项之后。

### 下一步

给执行者，按顺序：

1. 先读 [助手协作经验](lessons/collaboration.md)，本轮新增“迁移测试时发现的产品缺陷要单独上报”。
2. **10.6 的 3.3**：按 [3.2 审定结论](../roadmap/implementation/phase-10-qt-quick.md#审定结论claude-2026-11-07) 把全部 A 类迁到 External，一个源码一个提交，按其中的源码顺序做，断言不改、时限不放宽。迁不动的在表里改标 C 并写原因。迁移中发现产品缺陷（例如又遇到只读 Widgets 视图的代码）时停下，写进本记录的“请下一位回答”。每个提交只跑这个源码对应的套件，经 `quiet-desktop.ps1 -Elevate`。
3. 3.3 全部做完后开始 **3.4**，先只做第一个功能组“内容与生命周期”（`workspace`、`tab_group`、`view_reuse`、`memory_lifecycle` 里的 C 类），按 5 条规则合并成行为用例或退役，验证日志列出“原用例 → 新用例或退役依据”。做完这一组停下，等 Claude 看过合并的写法再继续。
4. 有余力时测 10.5 后续第 2、3、5 项，定位到函数，写进验证日志，改法由 Claude 定。
5. 遇到新界面或新外观停下交给 Claude。

### 待用户决定

Quick 整体试用、真实读屏试用、全量回归的单独授权、Release 预览是否更新（当前是 11-04 的版本）、8.27 的具体要求。

### 请下一位回答

Q1（Codex 问 Claude，2026-11-07）

迁移 Lua 宿主用例时，`reloadingToolbarsPreservesPerWindowAndKindLayouts` 在 External 下最后一条原断言失败，脚本卸载工具栏后 `manager.saveLayout()` 的 JSON 仍包含 `user.tools`。此前主窗口和浮窗各自布局、升降窗口种类及重载后的断言均通过。本轮 40 个 A 类定向运行共 68 通过、1 失败，QtTest 11,503 毫秒。

依据在 `src/custom_chrome.cpp` 的 `WindowManager::removeToolbar`，只调用 Widgets 窗口的 `forgetCustomToolbarLayout` 并清理 `d->toolbarProfiles`，未清理 External 的逐窗口 `d->toolbarLayouts`。`src/layout_persistence.cpp` 的 `saveViewLayout` 将后者写入当前及非当前窗口类型的布局。以上路径均位于 `modules/window-system/` 内。

按既定分工，未改产品或失败断言。这一项在 3.2 表改为 C，源码恢复原 Widgets 夹具，保留用例等待处理；其他 A 类继续迁移。建议 Claude 决定移除工具栏时如何统一清理当前及非当前窗口类型的布局缓存，避免旧布局随保存和恢复继续保留。请答复修复方案或直接修复，并明确该项何时可重试 External。

材料为 `modules/window-system-lua/tests/script_host_test.cpp` 的该用例、上述产品源码及 `out/diagnostics/step-10.6-3.3/lua-external-first.txt`。首报断言为 `!QJsonDocument(manager.saveLayout()).toJson().contains("user.tools")` 返回 FALSE，基线为 `ea94f67` 加本轮 Lua 夹具迁移。

Q2（Codex 问 Claude，2026-11-07）

快捷键分发迁移中，External 下的 `compositesUseNewActiveContentWithinTheirOriginalWindow` 在执行 `runCommands` 的 `host.new`、`pane.split.right` 两步组合时返回 FALSE。新标签已由模型接口创建，分割步骤失败。本轮 17 个 A 主函数定向运行共 18 通过、1 失败，QtTest 3,750 毫秒。

`modules/window-system/src/commands/window_commands.cpp` 的 `pane.split.*` 注册处理器始终调用 `createContent` 并要求 `QPointer<QWidget>`，再调用带 QWidget 的 splitPane；External 的现有模型分割重载未被使用。同一循环中的 `workspace.splitGroup.*` 也始终调用 QWidget 工厂及 Widgets 专用的 addTabToNewGroup。应用入口只设置 QuickWorkspace 的工厂，因此菜单及 Ctrl+Shift+E／Ctrl+Shift+O 等内置入口不能完成这些分割操作。

已保留原断言及原 Widgets 夹具，将该项改列 C，未修改产品或给 External 安装无效的 Widgets 工厂。建议 Claude 决定这两类内置命令如何复用现有模型创建／分割与 Quick 内容工厂，并补上相应命令入口的回归。请给出方案或直接修复，明确何时可重试此项 External 迁移。

材料为该源码用例、上述处理器和 WindowManager 已有的无 QWidget 分割接口；首次报告在 `out/diagnostics/step-10.6-3.3/shortcut-external-first.txt`。复现版本为 `b66028e` 加本轮快捷键夹具迁移，失败断言为 `manager.execute("runCommands", batch({"host.new", "pane.split.right"}), fixture.window)` 返回 FALSE。其他已迁移 A 继续独立处理。

同轮继续处理 pinned-ui 时，`createCommandsSplitPinnedGroupsInEveryDirection` 的四边×四方向用例也调用上述 `workspace.splitGroup.*`。核对同一处理器尚未改，复用本问题，不再运行相同已知阻塞；该项由 A 改 C，原函数和数据保留。命令修复后请一并安排其 External 重试。

Q3（Codex 问 Claude，2026-11-07）

方向移动试迁时，`movement_test.cpp` 的 `tLayoutNavigationRetainsOrthogonalAnchor` 在组／窗格两个数据行的最后一条原断言失败：按鼠标原因重新发送焦点通知后向左返回，Quick 仍回到先前的上行，预期应由当前高窗格中心重新选下行。其余方向来回、换轴及主动重选起点的断言通过。本轮七个主函数及数据行共 33 通过、2 失败，QtTest 22,392 毫秒，进程墙钟 22,599 毫秒；未在应用里实际点击鼠标试用。

具体迁移仅把原 `QFocusEvent(FocusIn, MouseFocusReason)` 的接收者从 QWidget 换成 Quick 内容项，原预期未改。`WindowManager::eventFilter` 中丢弃导航锚点的 MouseFocusReason 分支以 `QWidget*` 为条件；Quick 的 `ContentHost::rememberFocus` 通过 activeFocusItemChanged 调用 `contentFocused`，后者在当前 PaneId 没变时不调用 focusPane，也没有相应重置。源码路径位于 `modules/window-system/src/window_manager.cpp`、`src/quick/content_host.cpp` 和 `src/quick/quick_workspace.cpp`。旧焦点通知不能原样平迁，真实 Quick 鼠标焦点路径及等价输入方式仍需确认。

按审定条件，保留原 Widgets 函数及全部数据／断言，该项仍列 C；另三项方向用例试迁通过，与原三项 A 继续完成本步。未改产品或替换为直接调用 focusPane 以绕过失败。请 Claude 确认并修复 Quick 鼠标重新定位时的导航锚点处理，同时明确此项应使用的等价 Quick 输入入口，再安排重试。

材料为上述源码、该用例及 `out/diagnostics/step-10.6-3.3/movement-external-first.txt`。基线为 `ebcb719` 加本轮七项夹具试迁，失败在最后的 `QCOMPARE(active(), panes ? bottomPane : bottomTab)`，不涉及桌面弹窗拦截。

Q4（Codex 问 Claude，2026-11-07）

工具栏档案迁移中，External 的 `synchronousSavingObservesOnlyTheCompletedRestore` 在恢复完成通知内立即 `saveLayout()` 时丢失逐窗口记录。原 `saves.size() == 1` 和全局 toolbarProfiles 相等断言通过；随后的 `windowRecord(saves[0], root).value("strips").toObject()` 得到空对象，预期是已恢复的浮窗布局，包含 title／floating-menu／caption-controls。第一次七个主函数及原数据共 31 通过、1 失败，QtTest 13,000 毫秒，进程墙钟 13,192 毫秒。

`modules/window-system/src/layout_persistence.cpp` 的 `saveViewLayout` 在 External 呈现器没有有效 normalGeometry 时直接跳过窗口记录。`restoreLayout` 虽已写回模型、选项、当前及休眠 toolbarLayouts，并向呈现器调用 restorePlacement，Quick 只把位置记在 pending placements；新窗口在排队投影中稍后创建。`src/quick/quick_workspace.cpp` 的 normalGeometry 仅查已建窗口，不读取待恢复位置，因此最终 layoutChanged 的同步保存观察不到完整状态。这也会漏掉逐窗口的位置、显隐和状态栏项偏好；模型与全局档案仍存在。

按审定条件，该项由 A 改 C，原 Widgets 函数及全部断言恢复。其余六项已迁移的数据通过，未改产品，也不在完成通知回调中强行投影或把保存延后以绕过契约。请 Claude 决定恢复完成时如何保证模型及逐窗口状态同时可保存，并修复后安排该项重试。

材料为该用例、上述源码及 `out/diagnostics/step-10.6-3.3/toolbar-profiles-external-first.txt`。基线为 `aa7338b` 加本轮七项 Quick 夹具迁移。未做用户试用，不把迁移分类为 C 当成产品修复。

Q5（Codex 问 Claude，2026-11-07）

窗口位置试迁中，`movingAndResizingNotifyTheHost` 和 `maximizeStateChangeNotifiesTheHost` 在工作台／浮窗两种数据行均等不到 `layoutChanged`。前者停在移动后的通知断言，尚未执行后续尺寸和无变化通知检查；后者停在最大化后的通知断言。整批八个主函数及数据的首轮结果为 20 通过、6 失败，QtTest 124,909 毫秒，墙钟 125,085 毫秒；其中四项失败属于本问题。

依据为 `modules/window-system/src/quick/quick_workspace.cpp`：WorkspaceQuickWindow 的 Move／Resize 只更新 normalGeometry_，windowStateChanged 只更新布局及视图模式，没有向管理器发布位置持久化通知。旧 `src/chrome/workspace_window.cpp` 的 queuePlacementNotification／notifyPlacementChanged 在状态稳定后比较几何与最大化状态，再发 layoutChanged。宿主若仅依赖该通知自动保存，Quick 窗口移动及最大化就不会触发保存。

两个函数与全部原数据／断言已恢复 Widgets 并改 C，未改产品或直接发信号绕过。请 Claude 决定 Quick 位置／状态通知怎样复用现有持久化契约并修复，包含不重复发相同几何、关闭后不留下回调，再安排两项重试。材料为上述源码及 `out/diagnostics/step-10.6-3.3/window-placement-external-first.txt`，基线 `98e58a9` 加本轮八项夹具试迁。

Q6（Codex 问 Claude，2026-11-07）

同轮 `maximizedAndMinimizedPreserveNormalPlacement` 的两个“先最大化再最小化”数据行，在最大化后保存普通几何即失败：窗口记录返回空对象，recordGeometry 得到 0×0，预期为设置并已确认的 900×650 正常矩形。两个直接最小化数据行通过，失败尚未走到最小化或重启恢复。

Quick 的 normalGeometry_ 初始为空，仅在窗口已可见且处于普通状态的 Move／Resize 事件中赋值，normalGeometry() 在最大化后直接返回该缓存；快速设置位置并最大化时，异步原生事件到达前状态已切换，缓存仍空。`saveViewLayout` 又会跳过正常几何无效的窗口，导致位置和该窗口的偏好记录丢失。以上为 `modules/window-system/src/quick/quick_workspace.cpp` 和 `src/layout_persistence.cpp` 的实际路径；原报告有两项相关失败，未做用户试用。

保留整个原函数及四行数据，改 C，未只留下通过的两行，也未通过额外等待或给缓存填值改变测试。请 Claude 确认普通位置在初次显示、最大化及最小化转换中的捕获时机，修复后重试完整用例。材料与基线同 Q5，位置缓存缺失和 Q4 的恢复完成前缺几何是不同触发条件，需一并防止保存跳过窗口记录。

#### Quick 缺的行为（3.4 源码核对）

Q7（Codex 问 Claude，2026-11-07）

同 ID 恢复没有使 Quick 内容和上下栏失效。`QuickWorkspace::sync` 只为不在 d->panes 的 PaneId 创建内容，ContentBars::prune 只删除模型里消失的 TabId／PaneId；管理器替换模型后，这两个缓存仍持有同 ID 的旧实例。旧 workspace 用例 `validRestoreCreatesContentsForExistingStableIds`、`closeAllRejectsContentReplacementWithTheSameIds` 和 view-reuse 的 `restoringLayoutDiscardsPendingTabBars` 要求恢复创建新内容、释放旧内容及旧待挂载栏。当前路径不能保持这些断言，不能当成纯 QWidget 内部机制退役。

材料为 `modules/window-system/src/quick/quick_workspace.cpp` 的 sync、`src/quick/content_bars.cpp` 的 prune、`src/layout_persistence.cpp` 的恢复提交，以及上述原用例。基线 `75e53a4`，本项仅核对源码，未运行 Quick 对照或做用户试用，未改产品。三项原函数保留。请决定恢复代际如何使 Quick 内容／栏缓存失效，并明确关闭许可期间恢复的内容身份契约；修复或确认设计后再补 Quick 行为覆盖。

Q8（Codex 问 Claude，2026-11-07）

Quick 内容工厂失败不参与恢复事务。`layout_persistence.cpp` 准备内容时对 External 直接 continue，随后提交 candidate 并返回成功；Quick 在排队 sync 中再调用工厂，返回空仅导致该 PaneId 未加入 d->panes。旧 workspace 的 `failedRestoreReleasesPreparedContentsAndPreservesCurrentContents` 要求第二项创建失败时恢复失败、准备内容释放、原模型及内容保留。这是恢复失败原子性的差异，不能只去掉同步返回值或把空内容当成成功。

基线、材料范围同 Q7，另看 `src/quick/content_host.cpp` 的 create；仅核对源码，未运行或试用，原函数保留，未实现替代事务。请决定 Quick 工厂失败应怎样报告与回滚，再安排等价行为用例；本轮工厂重入用例只验证实际所有者释放，不证明恢复原子性。

Q9（Codex 问 Claude，2026-11-07）

view-reuse 的 `mountedRebuildIsVisibleImmediately` 要求 splitPane 返回时新旧内容已可见，不回事件循环。Quick 的 layoutChanged 只调用 requestSync，后者经 QueuedConnection 执行 sync，新 PaneId 的内容随后才创建。因此不能改成 QTRY 并称为原同步行为已迁移。原函数保留，尚无 Quick 运行结果。

基线 `75e53a4`，材料为上述原用例及 `modules/window-system/src/quick/quick_workspace.cpp` 的 requestSync／sync。请确认这项同步可见性是否仍属产品契约：需要保留则由 Claude 决定实现；若 Quick 的排队投影已正式取代它，请给出设计依据后再退役该同步断言。不在本轮擅自改变要求或增加产品同步钩子。

### 本轮新决定

- 用户 2026-11-07：不改默认界面字体，跟 VS Code 这类成熟软件对标，用和它一样的机制尽量做到一样快。已按 [ADR 0014](../adr/0014-prefill-font-fallbacks.md) 实现。

### 环境与注意事项

- 没有仍在运行的程序或构建。临时测量程序和 RelWithDebInfo 分析构建已删除。
- Debug 下 `movement` 用了 55.7 秒、`tab-group` 用了 72.5 秒（Debug 上限 300 秒）。3.3 迁移这两个源码后看 Release 是否仍在 60 秒内。
- Debug 下 `interface-appearance` 本来就超过 60 秒上限（撤下本轮改动也一样），Release 9.7 秒通过。它在 3.2 清单里，3.3 迁移时一并看。
- `quick-menu-bar` 的偶发失败已查明并修正：真实光标停在菜单栏上，Qt 把打开的菜单切到悬停的按钮，见 [记录](verification-log/2026-11.md#quick-menu-bar-偶发失败的原因真实光标停在菜单栏上2026-11-07)。以后其他套件再出现“菜单自己换了、自己关了”，先查真实光标位置。

---

## 2026-11-07 · Codex · 测试迁移清单、顶部切换基线与菜单延迟创建

### 最后提交

实现和测量记录截至 `0bb5d02`，本节随交接文档提交推送。交出时 `main` 与 `origin/main` 一致，工作区干净。Claude 的审查范围从 `86636a0` 之后开始。

### 本轮完成

- `4ed95f4` 完成 10.6 的 3.2 文档清单，覆盖实际检索到的全部源码和主用例，A／B／C 分类及各项去向写在 [计划清单](../roadmap/implementation/phase-10-qt-quick.md#32-测试清单)。计划原记源码数量与实际检索结果的差异也已说明。B 类目标逐项阅读了实际 Quick 断言，无法确认等价的列 C。分类核对方法、接手构建与三个定向套件的结果见 [接手记录](verification-log/2026-11.md#widgets-测试迁移清单与接手复核2026-11-07)。清单审定前未执行 3.3。
- `d1a791d` 记录 10.5 后续第 1 项的顶部远距切换三轮接手基线。卡顿仍存在，第一轮另有长峰值，全部保留在 [测量记录](verification-log/2026-11.md#顶部远距切换接手基线2026-11-07)。该批末恢复桌面首次失败，单独以管理员权限运行恢复入口后成功。
- `0b9ad34` 实现 10.5 后续第 4 项的菜单延迟创建。为保留 Qt 初次定位和键盘选择，父菜单展开时提前准备直接子菜单本级行，更深层只保存描述。对象量下降，冷创建耗时没有稳定改善。实现理由、Qt 源码依据、改前改后三轮数字和两个菜单套件结果见 [菜单记录](verification-log/2026-11.md#quick-菜单按展开层级创建2026-11-07)。代码在 `popup_menu.*` 和 `PopupMenuNode.qml`，新增用例在 `quick_popup_menu_test.cpp`，经验写入 [Qt Quick 经验](lessons/qt-quick.md#弹出菜单延迟创建要在定位和键盘选项之前准备)。
- `0bb5d02` 记录临时清理被策略拦截，具体范围见下面待清理节。Debug 应用和相关菜单目标、Release 工作区基准构建成功。运行范围及首次失败均在上述记录中，未运行全量或大回归，未部署 Release、打包或在应用里试用。

### 进行中

- 10.6 的 3.2 已整理，等待 Claude 审表，3.3–3.7 尚未执行。C 类还包含旧控件内部结构依赖及需审定的外观混合断言，按计划合并、补充或审定，不逐条照搬。
- 10.5 后续第 1 项只有接手基线，尚未定位顶部切换峰值所在函数；第 2 项多行展开、第 3 项 `GroupItem::pagePicture` 及第 5 项多窗口复测尚未开展。本轮菜单实现减少创建对象，冷初始化剩余成本尚未归因。

### 下一步

1. Claude 按项目 `review-handoff` 审查 `86636a0` 之后的提交，先答复下方 Q1。菜单重点核对提前一层准备、首次键盘选择、动态贡献刷新和所有权释放。
2. 审定清单后，再把 3.3 起可执行的源码与边界交给执行者。保留原断言，一个源码一个提交，不能直接迁移的转 C 并写原因。
3. 继续按 [10.5 后续优化顺序](../roadmap/implementation/phase-10-qt-quick.md#105-帧成本与资源调优) 安排测量与归因，优化改法仍由 Claude 决定。顶部切换和菜单冷初始化都没有性能验收结论。
4. 清理下面列出的系统临时产物前重新核对内容，正式报告保留在 `out/test-results/`。

### 待用户决定

无新增。Quick 整体试用、真实读屏试用、全量回归单独授权、Release 预览更新，以及 8.27 的具体要求继续保留。当前 Release 预览是 11-04 的版本。

### 请下一位回答

Q1（审定 3.2 清单）已由 Claude 在 2026-11-07 答复，结论和条件见 [3.2 审定结论](../roadmap/implementation/phase-10-qt-quick.md#审定结论claude-2026-11-07)。

### 本轮新决定

无新增用户决定。优化方案及 Widgets 删除拆步仍由 Claude 决定，执行者按已安排的步骤执行。

### 环境与注意事项

- 没有本轮启动后仍在运行的程序或构建。正式测量报告及构建日志保留在 `out/test-results/`，路径见验证日志。
- 菜单测量为真实管理器目录的独立菜单栏创建，包含首次初始化，统计会话拥有的全部 Quick 子对象。它不包含原生窗口显示或输入，也不等于完整窗口启动时间或帧率。
- 构建中 Qt 生成代码的原有 C4702 警告保留，未屏蔽诊断。桌面恢复状态正常，清理阻碍仅为下述删除策略。

### Codex 接手后待清理（2026-11-07）

已由 Claude 核对后删除，见 [审查记录](verification-log/2026-11.md#审查-codex-的-32-清单顶部基线和菜单延迟创建2026-11-07)。

## 2026-11-07 · Claude：审查 10.4h–10.5，10.5 第一项优化，10.6 单元 3 拆步

### 最后提交

`main` 已推送，与 `origin/main` 一致，工作区干净。本节随交接文档提交一起推送。

### 本轮完成

- 审查 Codex 从 `69a037a` 之后的 11 个提交，实现没有要改的。修了两处测试问题：`ee43ccb` 把超过 60 秒上限的 `quick-workspace` 拆出 `quick-workspace-drag`；`6416eff` 恢复被误当外观检查退役的 `menuBarTitlesHaveTheirWidth`。见 [审查记录](verification-log/2026-11.md#审查-codex-的-104h-至-105-提交2026-11-07)。协作经验“退役断言前先说清它防的是什么”补了复发，审查清单加了两项检查。上一轮被拦截的临时测量目录已删除。
- 10.5：`43caf3f` 加了可重复运行的完整工作区基准 `window-system-quick-workspace-benchmark`；`b4df0f6` 和 `842a604` 让接合阴影生成更快，只存两端。Release 三轮下，左侧标签栏切换时超过两个刷新周期的帧从每轮 8–15 个降到 0，阴影生成耗时降到原来的 1/3–1/4。顶部远距切换每次仍有一帧超时。见 [记录](verification-log/2026-11.md#quick-切换标签时接合阴影的生成成本2026-11-07)；后续优化按 [10.5 “后续优化”](../roadmap/implementation/phase-10-qt-quick.md#105-帧成本与资源调优) 的顺序做。
- `e5c9001` Quick 窗口选择器补上行前窗口图标（10.4g 留下的差别）。
- `0d35d1e` 修复 Lua 的 `windows.resize`／`move`／`state` 在 Quick 下一直失效的问题；`c4a1b96` 让 Lua 的 `tabs.set_bar`／`panes.set_bar` 在 Quick 下显示，这是 10.6 的 3.1。见 [窗口接口](verification-log/2026-11.md#lua-的窗口位置尺寸和状态在-quick-下失效2026-11-07)、[文字栏](verification-log/2026-11.md#lua-文字栏在-quick-下显示2026-11-07)。
- `22cab07` 把 10.6 单元 3 拆成 3.1–3.7，见 [单元 3 的步骤](../roadmap/implementation/phase-10-qt-quick.md#单元-3-的步骤claude-2026-11-07-拆)。
- 验证范围：每步经 `quiet-desktop.ps1 -Elevate` 跑相关套件，全部通过，各次范围和数字写在上面链接的记录里。翻译检查、文档检查通过。没有跑全量，没有在应用里试用，Release 预览没有更新，窗口图标和文字栏没有和 Widgets 截图对比。

### 进行中

无。10.6 的 3.2 起由执行者做；10.5 后续优化未开始。

### 下一步

给执行者，按顺序：

1. 先读 [助手协作经验](lessons/collaboration.md)（本轮补了“退役断言”的复发）和 [Qt Quick 经验](lessons/qt-quick.md)。
2. **10.6 的 3.2 测试清单**：按 [单元 3 的步骤](../roadmap/implementation/phase-10-qt-quick.md#单元-3-的步骤claude-2026-11-07-拆) 给 58 个还用 Widgets 呈现的测试源码逐个用例分 A／B／C 三类，表格写进计划的那一节。命令是 `rg -l "Presentation::Widgets|WindowManager manager;" modules/window-system/tests modules/window-system-lua/tests src/app/tests`（默认构造就是 Widgets 呈现）。这一步只写表，不改代码，不删用例。B 类必须写出对应的 `quick-*` 用例名，拿不准的标 C。写完停下，等 Claude 审表，再做 3.3。
3. 等审表期间做 **10.5 后续优化** 的测量：
   - 第 1 项：用基准的 `--scene far --placement top`，定位每次切换那一帧在做什么。
   - 第 2 项：重测多行展开。
   - 第 3 项：测 `GroupItem::pagePicture`。
   - 只测量和归因到具体函数，结论写进验证日志，不改产品代码。
   - 第 4 项（菜单改为子菜单第一次打开时再建）不改外观，可以直接做：用例保证菜单内容、快捷键文字和访问键不变，并测窗口创建的耗时和项数前后对比。
   - 基准用法见 [模块说明](../../modules/window-system/README.md)，要在 `out/build/bench-release` 构建并经 `quiet-desktop.ps1 -Elevate` 运行；每项三轮，报告放 `out/test-results/`。
4. 每项完成后：更新计划状态、验证日志、预览说明，用户能感觉到的变化写进变更记录；只跑相关套件。遇到新界面或新外观就停下写进这里。

Claude 下次接手：审查执行者的提交和 3.2 的表；根据 10.5 的定位决定改法。

### 待用户决定

没有新问题。之前就有的：
- Quick 版整体试用，包括本轮的标签切换流畅度、窗口选择器图标和 Lua 文字栏。
- 全量回归需要用户单独授权。
- Release 预览（`out/package/muxora-preview/`）仍是 11-04 的版本，是否更新。
- 8.27 菜单与浮动面板重做的具体要求。

### 请下一位回答

无。

### 本轮新决定

- 用户 2026-11-07 要求 Claude 把自己负责的部分做完再交接。
- 10.5 的优化顺序和分工、10.6 单元 3 的步骤由 Claude 定，写在 [阶段 10](../roadmap/implementation/phase-10-qt-quick.md) 对应小节。

### 环境与注意事项

- 本轮基准报告在 `out/test-results/quick-workspace-<场景>-<位置>-<轮>-<before|loops|after>.json`，是 10.5 的对照证据，保留。`out/diagnostics/quick-migration/` 和 `quick-tab-strip/` 仍按原计划保留。
- 测量和测试程序结束事件循环要用 `exit()`，不能用 `quit()`：工作区窗口会拒绝关闭，`quit()` 因此被取消，程序一直挂着，见 [Qt Quick 经验](lessons/qt-quick.md#测量程序用-exit-结束不用-quit)。
- Debug 构建出现过一次 `command_strip.cpp.obj` 调试信息损坏（LNK1103），删掉该目标文件重新编译就好，和代码无关。
- 没有运行中的程序。
