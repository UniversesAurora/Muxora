# WindowSystem

可独立构建的 Qt Widgets 工作区模块。Muxora 通过公开 API 使用它；其他项目也可通过 CMake package 接入，模块不依赖 SSH、终端、文件业务或 Muxora 配置。

当前提供两种窗口壳、逻辑窗口族、中央标签组、标签内窗格、四边自动隐藏／固定、单标签与整窗移动、命令面板、快捷键入口和布局快照。首轮功能与待完善项见 [预览说明](../../docs/development/window-system-preview.md)。

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

`WindowManager::resetToolbarWidth(window, stripId)` 重置指定工具条的宽度，保留排列顺序和其他条带的请求宽度。工具条选项中的“重置尺寸”调用统一命令 `window.toolbar.resetSize`；指定 `strip` 可直接执行，不带参数则显示工具条列表，支持键盘选择。目标在弹性左侧或无弹性行中保持左端，在弹性右侧保持右端，在相应方向分配或收回空间；空间不足时遵守各自最小宽度。重置隐藏条不改变可见布局，重置弹性条仍保留弹性策略。

每行最多一条弹性工具条。窗口宽度变化时先调整弹性空间，右侧条带直接相对右边缘排列；缩窄时的临时压缩不改写请求宽度，放大后恢复。标题在自身条带内居中，过长时右侧省略，内容区至少 96 个逻辑像素。

重置优先级以可选 `naturalWidthPriority` 字段进入布局快照，每行最多一个；未启用时省略该字段，旧布局仍可读取。手动调整宽度、改变尺寸策略或移动条带会清除受影响行的优先级；Esc 取消拖动恢复原值。

当前 API 为 0.1 初版，尚在交互迭代阶段，不承诺稳定二进制 ABI。完整 JSONC 用户键位、条件表达式和后续扩展仍按设计逐步接入。

`setTabBarsVisible(window, visible)` / `tabBarsVisible(window)` 按窗口控制全部标签栏，隐藏时内容与布局模型保持。窗口创建选项 `WindowOptions.tabBarsVisible` 默认为 `true`；命令 `window.tabBars.toggle` 可从菜单、命令面板或 `Ctrl+Alt+T` 调用，设置随布局保存。

窗格上下栏分别使用 `PaneBarSharing::Shared` / `PerPane`，默认共享。创建标签时设置 `TabOptions.topBarSharing` / `bottomBarSharing`，运行中可调用 `setTabTopBarSharing` / `setTabBottomBarSharing`。各 Pane 栏继续由 `setPaneTopBar` / `setPaneBottomBar` 接入；显式 Tab 栏不会因逐窗格模式而复制。对应统一命令为 `tab.topBar.setSharing` / `tab.bottomBar.setSharing`，接受 `sharing: "shared"` 或 `"perPane"`，无参数时切换当前策略。

共享策略进入布局快照；栏控件本身由宿主重建和接入，窗口模块不序列化任意 `QWidget`。

窗口位置、正常尺寸和最大化状态随布局保存，最小化不覆盖正常位置。恢复时按全部屏幕的逻辑可用区域调整越界位置，保留有效的负坐标和跨屏位置。宿主收到 `lastWindowAboutToClose` 时可同步保存仍有效的快照，避免立即关闭最后一个窗口而错过异步保存；该通知中不得修改布局。

## 规范与验证

- [窗口系统设计](../../docs/design/window-system-design.md)
- [工作区与导航](../../docs/design/workspace-design.md)
- [双窗口与拖拽](../../docs/design/floating-window-design.md)
- [测试要求](../../docs/development/testing-design.md)

运行 `pwsh tools/verify.ps1` 验证 Muxora、独立模块、安装后的外部接入及部署启动。测试失效须修复实现；不自行降低标准。
