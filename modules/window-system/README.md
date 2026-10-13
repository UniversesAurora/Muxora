# WindowSystem

可独立构建的 Qt Widgets 工作区模块。Muxora 通过公开 API 使用它；其他项目也可通过 CMake package 接入，模块不依赖 SSH、终端、文件业务或 Muxora 配置。

当前提供两种窗口壳、逻辑窗口族、中央标签组、标签内窗格、四边自动隐藏／固定、单标签与整窗移动、顶部／底部工具区、独立栏显隐、三种全屏范围与禅模式、命令面板、快捷键入口和布局快照。实际验证与待完善项见 [预览说明](../../docs/development/window-system-preview.md)。

## 构建边界

| Target | 职责 |
| --- | --- |
| `WindowSystem::Model` | 布局模型、稳定 ID、窗口族、事务和严格 JSON 校验；只依赖 Qt Core。 |
| `WindowSystem::Widgets` | 原生窗口、停靠视图、窗格、命令、主题与平台适配；依赖 Model 和 Qt Widgets。 |

可通过 `add_subdirectory()` 嵌入，也可安装后使用 `find_package(WindowSystem CONFIG REQUIRED)`。独立模块构建不探测 Muxora 或远程业务依赖；安装后接入由 `tests/fixtures/external-host/` 验证，不额外交付示例产品。

## 公开 API

入口为 [WindowManager](include/window_system/window_manager.hpp)，独立模型见 [LayoutModel](include/window_system/layout_model.hpp)。实际宿主组装见 [Muxora 启动入口](../../src/app/main.cpp) 与 [内容接入](../../src/workspace_host/placeholder_content.cpp)。

成功添加内容后模块接管 QWidget parent；添加失败保持调用者所有权。观察内容用稳定 ID 或 QPointer。移动、拆出及窗口形态转换保留原内容实例；关闭许可由宿主回调控制，完整根关闭先检查整个窗口族。

`setPaneTopBar()` / `setPaneBottomBar()` 为 Pane 设置专属附加栏，焦点变化时显示在 Tab 的共用上下位置；`setTabTopBar()` / `setTabBottomBar()` 作为默认栏。`pinTab()` 只处理当前标签，`pinEdge()` 明确处理该侧全部标签。标签图标可通过 `TabOptions::showIcon` 或 `setTabIconVisible()` 控制。

拖拽使用真实轻量窗口，达到阈值即移走内容并清理空组；Esc 通过模型快照恢复，保留控件实例。标签条按释放位置插入，内容区不隐式合并。详见 [交互修订](../../docs/design/window-interaction-design.md)。

侧栏同样按释放位置插入。`moveTabToEdgeBar(tab, targetWindow, edge, index)` 和 `moveAllTabsToEdgeBar(sourceWindow, targetWindow, edge, index)` 操作该侧可见的自动隐藏标签序列，内部标签组边界不影响视觉顺序。`index` 为排除待移动内容后的最终索引，`-1` 追加到末尾；整窗移动保留载荷顺序。模型可通过 `autoHiddenTabs()` 查询这一序列，普通组与侧栏都可使用 `workspace.reorderTab.previous` / `workspace.reorderTab.next` 命令和默认 `Ctrl+Shift+PgUp` / `Ctrl+Shift+PgDown` 调整顺序。

菜单、命令面板和快捷键共用 [CommandRegistry](include/window_system/command_registry.hpp)。宿主可以注册自己的操作；主题使用 [Theme](include/window_system/theme.hpp) 令牌，默认不添加应用 logo。布局快照由宿主选择保存位置，模块不写 Muxora 用户文件。

`Command::menuPath` 指定主菜单与子菜单，例如 `{"文件"}` 或 `{"工具", "扩展"}`；`menuOrder` 控制同组顺序。未指定路径的宿主命令按类别放入扩展分组。菜单增量接收新命令，显示当前快捷键，执行时仍走注册表；内部带参数的操作及工具条定制项不重复堆进主菜单。完整窗口默认采用“文件、视图、布局、窗口、工具、帮助”，设置统一从“工具 → 设置”打开。标签、窗格和窗口右键菜单复用同一组语义分类。

轻量浮窗也提供菜单，默认放在标题同一行最左侧；执行使用本浮窗的 Window／Tab／Pane 上下文。它保留内容、布局和窗口操作，省去退出整个应用、保存／重置完整工作区等入口，不创建完整工作区按钮条或全局状态栏。浮窗条带 ID 为 `floating-menu`，与完整窗口的 `menu` 分开保存；升降级保留完整菜单的位置和定制，恢复浮窗时局部菜单与标题同行。

`requestCloseAllWindows()` 为整个工作区申请一次关闭许可，再原子关闭所有窗口族与轻量窗口；任一标签被拒绝或许可回调改变布局时，保留整个工作区。最后关闭前发出 `lastWindowAboutToClose`，此时全部窗口与内容仍可保存；最终只发出一次 `allWindowsClosed`。模块不调用应用退出，宿主负责保存、处理写入失败并决定结束进程。Muxora 的“文件 → 退出”使用该接口。

`movePane(sourceTab, pane, targetTab, targetPane, placement)` 将原窗格合入另一个标签；`swapPanes()` 交换同标签的树叶位置。方向和循环版本包括 `movePaneToAdjacentTab()`、`movePaneToAdjacentGroup()`、`movePaneToWindowCycle()`、`swapPaneDirection()`；标签提供 `reorderTab()`、`moveTabToAdjacentGroup()`、`moveTabToWindowCycle()`。已有窗口也可通过 `movePaneToWindow()` / `moveTabToWindow()` 显式指定。移动保留内容与专属栏，合入现有标签采用目标的共享策略；空目标继承源策略。

对应命令为 `pane.moveToTab`、`pane.moveToGroup.left/right/up/down`、`pane.moveToWindow`、`pane.swap.left/right/up/down` 及 `workspace.moveTabToGroup.left/right/up/down`、`workspace.moveTabToWindow`。标签和窗口选择命令支持 ID 参数或键盘选择菜单，`.previous` / `.next` 提供循环移动。窗格前后标签移动默认 `Alt+PgUp` / `Alt+PgDown`；标签重排继续使用 `Ctrl+Shift+PgUp` / `Ctrl+Shift+PgDown`。全部命令可从快捷键编辑器绑定。

`WindowManager::resetToolbarWidth(window, stripId)` 重置指定工具条的宽度，保留排列顺序和其他条带的请求宽度。工具条选项中的“重置尺寸”调用统一命令 `window.toolbar.resetSize`；指定 `strip` 可直接执行，不带参数则显示工具条列表，支持键盘选择。目标在弹性左侧或无弹性行中保持左端，在弹性右侧保持右端，在相应方向分配或收回空间；空间不足时遵守各自最小宽度。重置隐藏条不改变可见布局，重置弹性条仍保留弹性策略。

每行最多一条弹性工具条。标题条必须保持弹性，同一行的其他条带不能设为弹性；把弹性条拖入标题行时，它会改为普通条。宿主约束优先于保存布局，旧布局中的普通标题会恢复弹性。窗口宽度变化时先调整弹性空间，右侧条带直接相对右边缘排列；缩窄时的临时压缩不改写请求宽度，放大后恢复。标题在自身条带内居中，过长时右侧省略，内容区至少 96 个逻辑像素。

重置优先级以可选 `naturalWidthPriority` 字段进入布局快照，每行最多一个；未启用时省略该字段，旧布局仍可读取。手动调整宽度、改变尺寸策略或移动条带会清除受影响行的优先级；Esc 取消拖动恢复原值。

`moveToolbar(window, stripId, ToolbarArea::Top/Bottom, row, index = -1)` 将已有工具条放入指定区域与行，保留其内容、按钮选择和尺寸策略。顶部与底部均支持任意多行，底部工具区位于下侧收纳条之下、全局状态栏之上。各区从外侧的第 0 行向工作区增加，顶部向下、底部向上；`index = -1` 追加到行末。允许已有行或末尾新一行，错误目标和越界索引返回 `false`，不创建空控件或改变布局。

解锁拖动到最后一行时，工作区一侧出现一行投放预览；其他时候空行没有高度，移走最后一条后该行立即消失。上下区域共用锁定、弹性、压缩和溢出规则。`window.toolbar.move` 支持 `strip`、`area: "top" / "bottom"`、`row`、`index`；不带参数时提供工具条和区域／行的键盘选择菜单，位于“视图 → 工具条 → 移动工具条…”。区域随布局中的 `rows`／`bottomRows` 保存，旧快照沿用顶部区域。

当前 API 为 0.1 初版，尚在交互迭代阶段，不承诺稳定二进制 ABI。完整 JSONC 用户键位、条件表达式和后续扩展仍按设计逐步接入。

`setTabBarsVisible(window, visible)` / `tabBarsVisible(window)` 按窗口控制中央标签组的标签栏，隐藏时内容与布局模型保持。侧边收纳条、固定面板标签栏及临时展开标题继续显示。窗口创建选项 `WindowOptions.tabBarsVisible` 默认为 `true`；命令 `window.tabBars.toggle` 可从菜单、命令面板或 `Ctrl+Alt+T` 调用，设置随布局保存。

`setSideBarsVisible(window, visible)` / `sideBarsVisible(window)` 独立控制四边收纳条，隐藏时关闭当前临时面板，保留固定分区。`WindowOptions.sideBarsVisible` 默认为 `true`；命令 `window.sideBars.toggle` 默认绑定 `Ctrl+Alt+B`，状态随窗口布局保存。隐藏后仍可通过明确的标签激活操作打开临时面板。没有该字段的旧快照默认显示侧栏；非法字段拒绝恢复，不修改现有布局。

`setToolbarsVisible(window, visible)` 控制顶部与底部工具区，`setStatusBarVisible(window, visible)` 控制完整窗口的全局状态栏；对应 `WindowOptions.toolbarsVisible` / `statusBarVisible` 默认均为 `true`。命令 `window.toolbars.toggle` / `window.statusBar.toggle` 默认使用 `Ctrl+Alt+H` / `Ctrl+Alt+S`。这四项显隐偏好按窗口保存，与是否全屏无关，其他窗口保持自己的设置。

`setViewMode(window, ViewMode::Normal/WindowFullscreen/GroupFullscreen/PaneFullscreen)` 选择全屏范围。窗口范围保留整个工作区；组与窗格范围投影当前目标的原控件，不复制内容、不改布局树。组范围仍可切换本组标签，窗格范围按 Pane ID 跟随同窗口移动；激活范围外内容会退回窗口全屏，目标关闭或跨窗口移走后安全退出。退出恢复原分割尺寸和进入前的正常位置／最大化状态。中央标签栏开关在这些模式中继续只影响中央组，固定／收纳标题保持原规则。

`setZenMode(window, enabled)` 仅允许在全屏范围开启。禅模式临时关闭四栏的有效显示，另隐藏固定标题及所有 Tab／Pane 上下附加栏，保留原控件和四项偏好。显示任一栏会退出禅模式，恢复其他偏好并应用本次显示；关闭禅模式或退出全屏同样恢复。`preferredBarVisibility()` 返回保存的偏好，`barVisibility()` 返回有效显示值，`windowViewState()` 另包含模式、禅状态与稳定目标 ID。

| 命令 | 默认键位 |
| --- | --- |
| `window.fullscreen.toggle` | `F11` |
| `window.fullscreen.group` | `Ctrl+Shift+F11` |
| `window.fullscreen.pane` | `Shift+F11` |
| `window.zen.toggle` | 依次按 `Ctrl+K`、`Z` |
| `window.fullscreen.exit` | `Esc` |

全屏状态和目标通过 `viewState` 随窗口快照保存；正常矩形仍独立保存，不使用全屏尺寸替代。恢复先检查目标属于该窗口。普通窗口没有禅模式，旧快照缺失的新显示字段采用默认值；无效字段拒绝恢复并保留当前工作区。

侧栏标签首次固定时，新分区位于该侧最靠外的位置；此前取消固定的标签恢复其记录的位置。单标签、整组和整侧 API 采用同一位置规则，整侧首次固定保留组间顺序。

窗格上下栏分别使用 `PaneBarSharing::Shared` / `PerPane`，默认共享。创建标签时设置 `TabOptions.topBarSharing` / `bottomBarSharing`，运行中可调用 `setTabTopBarSharing` / `setTabBottomBarSharing`。各 Pane 栏继续由 `setPaneTopBar` / `setPaneBottomBar` 接入；显式 Tab 栏不会因逐窗格模式而复制。对应统一命令为 `tab.topBar.setSharing` / `tab.bottomBar.setSharing`，接受 `sharing: "shared"` 或 `"perPane"`，无参数时切换当前策略。

共享策略进入布局快照；栏控件本身由宿主重建和接入，窗口模块不序列化任意 `QWidget`。

窗口位置、正常尺寸和最大化状态随布局保存，最小化不覆盖正常位置。恢复时按全部屏幕的逻辑可用区域调整越界位置，保留有效的负坐标和跨屏位置。宿主收到 `lastWindowAboutToClose` 时可同步保存仍有效的快照，避免立即关闭最后一个窗口而错过异步保存；该通知中不得修改布局。

## 规范与验证

- [窗口系统设计](../../docs/design/window-system-design.md)
- [工作区与导航](../../docs/design/workspace-design.md)
- [双窗口与拖拽](../../docs/design/floating-window-design.md)
- [测试要求](../../docs/development/testing-design.md)

运行 `pwsh tools/verify.ps1` 验证 Muxora、独立模块、安装后的外部接入及部署启动。测试失效须修复实现；不自行降低标准。
