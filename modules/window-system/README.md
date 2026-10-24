# WindowSystem

可独立构建的 Qt Widgets 工作区模块。Muxora 通过公开 API 使用它；其他项目也可通过 CMake package 接入，模块不依赖 SSH、终端、文件业务或 Muxora 配置。

当前提供两种窗口壳、逻辑窗口族、中央标签组、标签内窗格、四边自动隐藏／固定、单标签、整组与整窗移动、组／标签／窗格层级转换、标签颜色和六种排序、顶部／底部工具区、独立栏显隐、三种全屏范围与禅模式、命令面板、快捷键入口和布局快照。本文说明构建与公开 API；交互行为的具体规则见 [窗口交互细则](../../docs/design/window-interaction-design.md)，实际验证与待完善项见 [预览说明](../../docs/development/window-system-preview.md)。

## 构建边界

| Target | 职责 |
| --- | --- |
| `WindowSystem::Model` | 布局模型、稳定 ID、窗口族、事务和严格 JSON 校验；只依赖 Qt Core。 |
| `WindowSystem::Widgets` | 原生窗口、停靠视图、窗格、命令、主题与平台适配；依赖 Model 和 Qt Widgets。 |

可通过 `add_subdirectory()` 嵌入，也可安装后使用 `find_package(WindowSystem CONFIG REQUIRED)`。独立模块构建不探测 Muxora 或远程业务依赖；安装后接入由 `tests/fixtures/external-host/` 验证，不额外交付示例产品。

从源码构建需要 Qt 的 LinguistTools；翻译目录在构建时编译并嵌入静态库，已安装的模块消费者不需要翻译工具或 SDK 目录中的 `.qm` 文件。

## 公开 API

入口为 [WindowManager](include/window_system/window_manager.hpp)，独立模型见 [LayoutModel](include/window_system/layout_model.hpp)。实际宿主组装见 [Muxora 启动入口](../../src/app/main.cpp) 与 [内容接入](../../src/workspace_host/placeholder_content.cpp)。

`LayoutModel::revision()` 统计同一个模型的已提交修改；宿主整体替换模型（恢复快照、回退到副本）后数值可能重复。需要判断“模型是否还是原来那份”时使用 `stateId()`：副本共享同一个值，每次成功修改或 `fromJson()` 都换成进程内的新值，默认构造的模型为 0。它不写入快照。

标题裁切的两套长度通过 `TabAppearance` 设置；有效范围和零值含义见 [标签外观设置](../../docs/design/settings-and-commands-design.md#31-标签外观与颜色)。设置页复用内部 `IntegerSlider` 组合 Qt 滑条与数字框，组合不解释单位；`UnitSpinBox` 独立处理显示后缀，宿主配置的数值类型仍严格校验。

`LayoutModel::ownerOfNode(node)` 返回分割节点所在的布局树：窗口的中央树、某一侧的固定树，或某个标签的窗格树。已提交的模型上为 O(1)；叶节点、未知 ID 和固定侧的恢复骨架返回空。它只描述模型，不涉及宿主为这棵树创建的控件。

成功添加内容后模块接管 QWidget parent；添加失败保持调用者所有权。观察内容用稳定 ID 或 QPointer。移动、拆出及窗口形态转换保留原内容实例；关闭许可由宿主回调控制，完整根关闭先检查整个窗口族。

`setPaneTopBar()` / `setPaneBottomBar()` 为 Pane 设置专属附加栏，焦点变化时显示在 Tab 的共用上下位置；`setTabTopBar()` / `setTabBottomBar()` 作为默认栏。`pinTab()` 只处理当前标签，`pinEdge()` 明确处理该侧全部标签。`setTabSticky()` 把标签钉在本组标签栏最前面（与固定到侧栏无关），`isTabSticky()` 查询，尺寸由 `TabAppearance::stickySizing` 和 `shrinkStickyTitleWidth` 决定；模型中 `GroupRecord::stickyTabs` 是开头连续钉住的标签数，规则见 [交互细则 · 钉住标签](../../docs/design/window-interaction-design.md#钉住标签)。标签图标可通过 `TabOptions::showIcon` 或 `setTabIconVisible()` 控制；中央栏使用 `TabAppearance::showIcons`，固定和非固定侧栏使用 `showSidebarIcons`，开启时仍尊重单标签设置。

达到拖动阈值后，鼠标仍在原标签条或收纳条内时只显示标签预览，内容留在原处。首次离条才移入真实轻量窗口并清理空组，Esc 恢复原布局和控件实例。标签条按释放位置插入，内容区不隐式合并。详见 [交互细则](../../docs/design/window-interaction-design.md)。

侧栏同样按释放位置插入。`moveTabToEdgeBar(tab, targetWindow, edge, index)` 和 `moveAllTabsToEdgeBar(sourceWindow, targetWindow, edge, index)` 操作该侧可见的自动隐藏标签序列，内部标签组边界不影响视觉顺序。`index` 为排除待移动内容后的最终索引，`-1` 追加到末尾；整窗移动保留载荷顺序。模型可通过 `autoHiddenTabs()` 查询这一序列，普通组与侧栏都可使用 `workspace.reorderTab.previous` / `workspace.reorderTab.next` 命令和默认 `Ctrl+Shift+PgUp` / `Ctrl+Shift+PgDown` 调整顺序。

菜单、命令面板和快捷键共用 [CommandRegistry](include/window_system/command_registry.hpp)。宿主可以注册自己的操作；主题使用 [Theme](include/window_system/theme.hpp) 令牌，默认不添加应用 logo。布局快照由宿主选择保存位置，模块不写 Muxora 用户文件。

每个命令的 `bindings` 为 `CommandBinding` 数组，每条含 `sequence` 与对象 `args`。`setBindings` 替换、`addBinding` 增加、`removeBinding` 按索引移除；原 `shortcut` 镜像第一条，`setShortcut` 仍表示替换全部绑定。二至四段序列可在组合前缀后接裸字母或数字。`WindowManager::commandsChanged` 用于界面同步和宿主持久化，模块不选择用户配置路径。

`runCommands` 接受 `args.commands` 数组，元素为命令 ID 或 `{command, args}` 对象，依次执行并在失败时停止。窗口管理器为后续步骤解析原窗口的当前活动内容；独立使用注册表时默认保留原上下文，可通过 `setCompositeContextResolver` 注入自己的解析策略。组合有界且禁止递归调用，不运行脚本文本。

快捷键页“按键查询”录入最多四段按键，按完整序列或前缀筛选绑定，查询不执行命令。`runCommands` 使用普通命令的绑定与参数编辑，没有独立的组合快捷键类型。状态栏把续键等待提示与普通消息分开；命中、失败、取消和其他等待结束路径恢复最新的普通消息，不覆盖宿主在等待期间更新的状态。

组件批量修改绑定时可使用 `setBindingsBatch()`：先校验整批绑定，再提交并通知一次，观察者不会看到部分更新。动态添加和移除命令同样通知 `commandsChanged`，菜单回收已移除命令及不再使用的动态分类。

[TranslationManager](include/window_system/localization.hpp) 由宿主在创建界面前构造、安装语言并保留生命周期，提供 `System`、`SimplifiedChinese` 和 `English`。宿主可通过 `addCatalog` 增加自己的翻译目录，通过 `requestedLanguageChanged` 持久化重启偏好。语言切换不会重建业务内容或修改用户名称。目录维护入口为 CMake 的 `window_system_lupdate`、`window_system_lrelease`；应用目录使用 `muxora_lupdate`、`muxora_lrelease`。源文本使用英文，中文由目录提供；未安装目录时显示英文。新增文本更新两种语言的 `.ts` 后，运行 [翻译覆盖检查](../../docs/development/testing-design.md#翻译覆盖) 再构建和测试。模型及 Lua 适配诊断沿用 `WindowSystem` 上下文，不为翻译增加 Widgets 链接依赖。内置菜单的显示文字与持久化路径分开，宿主提供的标题、类别和用户路径不按文字内容二次翻译。

默认主题的配色和形状见 [交互细则 · 默认视觉](../../docs/design/window-interaction-design.md#默认视觉)；宿主通过 [Theme](include/window_system/theme.hpp) 令牌取色，例如普通按钮悬停用 `Theme.buttonHover`。

`Command::menuPath` 指定主菜单与子菜单，例如 `{"file"}` 或 `{"tools", "extensions"}`；路径的每一段是英文 ID，内置菜单是 `file`、`view`、`layout`、`window`、`tools`、`help`，显示名由窗口系统翻译，其他名称按原文显示；`menuOrder` 控制同组顺序。未指定路径的宿主命令按类别放入扩展分组。菜单增量接收新命令，显示当前快捷键，执行时仍走注册表；内部带参数的操作及工具条定制项不重复堆进主菜单。完整窗口默认采用“文件、视图、布局、窗口、工具、帮助”，设置统一从“工具 → 设置”打开。标签、窗格和窗口右键菜单复用同一组语义分类。

`Command::contextMenus` 为右键菜单贡献 `ContextMenuPlacement`，可选择 Tab、Pane、Group 或 Window，并设置子菜单 `path`、`order` 和可选 `contentType`。内容类型按目标标签的 `TabOptions::contentType` 过滤，空类型匹配全部；菜单操作使用打开菜单时捕获的上下文，目标失效后不会改为操作当前焦点。未来 SSH／SFTP 组件可以通过这些元数据增加菜单，不需要修改窗口类。命令的显示文本由提供者负责翻译。

轻量浮窗也提供菜单，默认放在标题同一行最左侧；执行使用本浮窗的 Window／Tab／Pane 上下文。它保留内容、布局和窗口操作，省去退出整个应用、保存／重置完整工作区等入口，不创建完整工作区按钮条或全局状态栏。浮窗条带 ID 为 `floating-menu`，与完整窗口的 `menu` 分开保存；升降级保留完整菜单的位置和定制，恢复浮窗时局部菜单与标题同行。

`requestCloseAllWindows()` 为整个工作区申请一次关闭许可，再原子关闭所有窗口族与轻量窗口；任一标签被拒绝或许可回调改变布局时，保留整个工作区。最后关闭前发出 `lastWindowAboutToClose`，此时全部窗口与内容仍可保存；最终只发出一次 `allWindowsClosed`。模块不调用应用退出，宿主负责保存、处理写入失败并决定结束进程。Muxora 的“文件 → 退出”使用该接口。

`movePane(sourceTab, pane, targetTab, targetPane, placement)` 将原窗格合入另一个标签；`swapPanes()` 交换同标签的树叶位置。方向和循环版本包括 `movePaneToAdjacentTab()`、`movePaneToAdjacentGroup()`、`movePaneToWindowCycle()`、`swapPaneDirection()`；标签提供 `reorderTab()`、`moveTabToAdjacentGroup()`、`moveTabToWindowCycle()`。已有窗口也可通过 `movePaneToWindow()` / `moveTabToWindow()` 显式指定。移动保留内容与专属栏，合入现有标签采用目标的共享策略；空目标继承源策略。

对应命令为 `pane.moveToTab`、`pane.moveToGroup.left/right/up/down`、`pane.moveToWindow`、`pane.swap.left/right/up/down` 及 `workspace.moveTabToGroup.left/right/up/down`、`workspace.moveTabToWindow`。标签和窗口选择命令支持 ID 参数或键盘选择菜单，`.previous` / `.next` 提供循环移动。窗格前后标签移动默认 `Alt+PgUp` / `Alt+PgDown`；标签重排继续使用 `Ctrl+Shift+PgUp` / `Ctrl+Shift+PgDown`。全部命令可从快捷键编辑器绑定。

`WindowManager::resetToolbarWidth(window, stripId)` 重置指定工具条的宽度，保留排列顺序和其他条带的请求宽度。工具条选项中的“重置尺寸”调用统一命令 `window.toolbar.resetSize`；指定 `strip` 可直接执行，不带参数则显示工具条列表，支持键盘选择。目标在弹性左侧或无弹性行中保持左端，在弹性右侧保持右端，在相应方向分配或收回空间；空间不足时遵守各自最小宽度。重置隐藏条不改变可见布局，重置弹性条仍保留弹性策略。

宿主用 [CommandToolbar / StatusItem](include/window_system/custom_chrome.hpp) 值描述扩展工具区：`registerToolbar()` 接受稳定 ID、标题、命令 ID 序列和默认区域／行；`updateToolbar()` 更新相同 ID 的定义并保留各窗口及窗口形态的排列。工具条状态可通过 `toolbarState()` 查询，位置、宽度、弹性、按钮及显隐继续使用普通工具条 API。`registerStatusItem()` / `setStatusItemText()` / `removeStatusItem()` 管理全局状态项，各窗口独立保存显隐。移除贡献时清理该 ID 的布局元数据，提供者仍负责注销自己的命令。

`StatusItem::command` 可选地指定点击或键盘激活时执行的已注册命令。命令使用该状态项所在窗口的上下文；留空仍是普通文本项。命令项的文本为空时暂时隐藏，定义与用户显隐偏好保留，恢复文本后遵循原偏好。移除状态项或销毁窗口后，尚未执行的点击不再派发。

这些公开接口不引入 Lua 依赖。可选的 [WindowSystemLua](../window-system-lua/README.md) 在外部把值和稳定 ID 映射为本模块 API，其他宿主也可以直接用 C++ 注册组件。

每行最多一条弹性工具条。标题条必须保持弹性，同一行的其他条带不能设为弹性；把弹性条拖入标题行时，它会改为普通条。宿主约束优先于保存布局，恢复时也强制标题弹性。窗口宽度变化时先调整弹性空间，右侧条带直接相对右边缘排列；缩窄时的临时压缩不改写请求宽度，放大后恢复。标题在自身条带内居中，过长时右侧省略，内容区至少 96 个逻辑像素。

重置优先级以可选 `naturalWidthPriority` 字段进入布局快照，每行最多一个；未启用时省略该可选字段。手动调整宽度、改变尺寸策略或移动条带会清除受影响行的优先级；Esc 取消拖动恢复原值。

`moveToolbar(window, stripId, ToolbarArea::Top/Bottom, row, index = -1)` 将已有工具条放入指定区域与行，保留其内容、按钮选择和尺寸策略。顶部与底部均支持任意多行，底部工具区位于下侧收纳条之下、全局状态栏之上。各区从外侧的第 0 行向工作区增加，顶部向下、底部向上；`index = -1` 追加到行末。允许已有行或末尾新一行，错误目标和越界索引返回 `false`，不创建空控件或改变布局。

解锁拖动到最后一行时，工作区一侧出现一行投放预览；其他时候空行没有高度，移走最后一条后该行立即消失。上下区域共用锁定、弹性、压缩和溢出规则。`window.toolbar.move` 支持 `strip`、`area: "top" / "bottom"`、`row`、`index`；不带参数时提供工具条和区域／行的键盘选择菜单，位于“视图 → 工具条 → 移动工具条…”。区域随布局中的 `rows`／`bottomRows` 保存，旧快照沿用顶部区域。

`WindowBehavior::captionButtonOrder` 默认 `CaptionButtonOrder::Windows`，设置页使用 `window.captionButtonOrder`，Lua 中为 `caption_button_order`（`windows`／`reversed`／`macos`）。三种顺序分别是“最小化、最大化、关闭”（靠右）、“关闭、最大化、最小化”（靠左）和“关闭、最小化、最大化”（靠左）。

`WindowBehavior::hideToolbarOptionsWhenLocked` 默认 `false`，设置页使用 `window.hideToolbarOptionsWhenLocked`。开启后，锁定且无溢出的普通工具条隐藏选项尾部并释放宽度；溢出或解锁时恢复。禁用工具按钮不绘制高亮边框，只显示灰色图标。

完整布局快照显式保存 `toolbarProfiles` 和 `visibilityProfiles` 对象。各窗口的工具条布局必须属于自身形态，并包含该形态的内置条带；不拆分混合布局、不从窗口记录补共享默认，也不补旧窗口控制条。保存时不预留空尾行。运行中新增脚本工具条仍使用其声明位置，不受此限制。

当前 API 为 0.1 初版，尚在交互迭代阶段，不承诺稳定二进制 ABI。完整 JSONC 用户键位、条件表达式和后续扩展仍按设计逐步接入。

`setTabBarsVisible(window, visible)` / `tabBarsVisible(window)` 按窗口控制中央标签组的标签栏，隐藏时内容与布局模型保持。侧边收纳条、固定面板标签栏及临时展开标题继续显示。窗口创建选项 `WindowOptions.tabBarsVisible` 默认为 `true`；命令 `window.tabBars.toggle` 可从菜单、命令面板或 `Ctrl+Alt+T` 调用，设置随布局保存。

`setSideBarsVisible(window, visible)` / `sideBarsVisible(window)` 独立控制四边收纳条，隐藏时关闭当前临时面板，保留固定分区。`WindowOptions.sideBarsVisible` 默认为 `true`；命令 `window.sideBars.toggle` 默认绑定 `Ctrl+Alt+B`，状态随窗口布局保存。隐藏后仍可通过明确的标签激活操作打开临时面板。省略该可选字段时默认显示侧栏；非法字段拒绝恢复，不修改现有布局。

`setToolbarsVisible(window, visible)` 控制顶部与底部工具区，`setStatusBarVisible(window, visible)` 控制完整窗口的全局状态栏；对应 `WindowOptions.toolbarsVisible` / `statusBarVisible` 默认均为 `true`。命令 `window.toolbars.toggle` / `window.statusBar.toggle` 默认使用 `Ctrl+Alt+H` / `Ctrl+Alt+S`。这四项显隐偏好按窗口保存，与是否全屏无关，其他窗口保持自己的设置。

`setViewMode(window, ViewMode::Normal/WindowFullscreen/GroupFullscreen/PaneFullscreen)` 选择全屏范围。窗口范围保留整个工作区；组与窗格范围投影当前目标的原控件，不复制内容、不改布局树。组范围仍可切换本组标签，窗格范围按 Pane ID 跟随同窗口移动；激活范围外内容会退回窗口全屏，目标关闭或跨窗口移走后安全退出。退出恢复原分割尺寸和进入前的正常位置／最大化状态。中央标签栏开关在这些模式中继续只影响中央组，固定／收纳标题保持原规则。

`setZenMode(window, enabled)` 可从普通窗口开启，按 `WindowBehavior::zenFullscreenMode` 一次进入指定全屏范围和禅模式，默认 `WindowFullscreen`，也可选择 `GroupFullscreen` 或 `PaneFullscreen`。已经全屏时保留当前范围。禅模式临时关闭四栏的有效显示，另隐藏固定标题及所有 Tab／Pane 上下附加栏，保留原控件和四项偏好。显示任一栏会退出禅模式，恢复其他偏好并应用本次显示；关闭禅模式保留全屏，退出全屏则一并关闭禅模式。`preferredBarVisibility()` 返回保存的偏好，`barVisibility()` 返回有效显示值，`windowViewState()` 另包含模式、禅状态与稳定目标 ID。

| 命令 | 默认键位 |
| --- | --- |
| `window.fullscreen.toggle` | `F11` |
| `window.fullscreen.group` | `Ctrl+Shift+F11` |
| `window.fullscreen.pane` | `Shift+F11` |
| `window.zen.toggle` | 依次按 `Ctrl+K`、`Z` |
| `window.fullscreen.exit` | `Esc` |

全屏状态、禅模式和临时目标只在当前运行中有效，不写入窗口快照。恢复使用正常矩形、最大化状态和原栏偏好，不恢复全屏或禅模式。窗口快照只接受当前格式的字段，不记录临时模式。`zenFullscreenMode` 是进入禅模式的行为偏好，继续保存。

侧栏标签首次固定时，新分区位于该侧最靠外的位置；此前取消固定的标签恢复其记录的位置。单标签、整组和整侧 API 采用同一位置规则，整侧首次固定保留组间顺序。

各侧固定区使用独立的二维 `pinnedLayouts` 树，中央树和其他侧的比例不受其内部调整影响。取消固定还保留有界的分割恢复骨架，重新固定只重建缺失槽位，不回写整侧旧快照覆盖后续编辑；没有隐藏标签引用的记录会清理。`pinnedLayouts` 必须包含四侧树（空侧写为 null），缺失、重复、错侧和无效引用均拒绝。标签排序计数也必须显式保存，不在读取时补建。

临时展开面板可从边缘或角调整宽高。`setPeekSize(tab, edge, QSize(width, height))` 按标签分别保存左右侧和上下侧的请求；同方向两边共享，两个方向互不影响。默认左右宽 250 并填满可用高度，上下高 170 并填满可用宽度。`setPeekSizePolicy(tab, edge, PeekSizePolicy{width, height})` 的两个维度均可省略，省略时使用对应默认行为；`resetPeekSize(tab, edge)` 重置该方向。宽度有效范围 140..16777215，高度 100..16777215；实际可用区域限制显示尺寸，不改写请求，也不影响固定分区尺寸。

方向策略随标签跨组、换边和跨窗口移动并保存到布局。`setPeekSize`、`peekSize` 和 `resetPeekSize` 都必须指定 `edge`；重置只作用于指定方向，不修改另一方向。命令 `tab.peek.setSize` 接受可选 `tab` 和同时提供的 `width` / `height`，其中 0 表示该维度默认值；无参数时显示可键盘编辑的对话框。`tab.peek.resetSize` 只重置标签当前所在方向。两项命令均可绑定快捷键，自动隐藏标签的菜单入口位于“边栏 → 临时展开大小”。

临时面板沿条带方向缩短时尽量对齐触发标签的起始边，再限制到可用范围。整侧固定按钮独立于条带滚动内容，始终可见；相邻显示的收纳条在交界保留 2 个逻辑像素的间隙。

新建标签默认插在本组当前标签之后。`WindowBehavior.newTabPosition` 可选择 `Beginning`、`BeforeActive`、`AfterActive`、`End`；图形设置同步该值并随布局保存。`WindowManager::addTab` 的末尾参数 `insertionIndex` 为非负数时使用显式位置，-1 跟随行为设置；独立 `LayoutModel::addTab` 的 -1 仍表示追加，避免模型依赖宿主偏好。

每组最后一个标签后是组合的新建按钮与类型下拉，中间保留 1 个逻辑像素细缝；右键任一按钮也打开类型菜单。宿主先注册创建内容的命令，再调用 `setTabCreationCommands(defaultCommand, choices)`。`requestNewTab(window, group, command)` 和按钮使用同一处理函数，并把被点击组的稳定 ID 传入命令；不依赖另一个组当前是否聚焦。空目录不创建内容，未知、重复命令或无效目标返回失败。

标签条尺寸变化时的滚动规则见 [交互细则 · 条带尺寸变化](../../docs/design/window-interaction-design.md#条带尺寸变化)，模块内部实现，不需要宿主参与。

### 标签外观、排序与整组移动

[TabAppearance](include/window_system/tab_appearance.hpp) 通过 `tabAppearance()` / `setTabAppearance()` 读取和完整校验后应用，随布局快照保存。`centralPlacement` 与 `pinnedPlacement` 分别选择中央和固定侧栏标签位于 `Top` 或 `Bottom`。中央栏使用 `showIcons`、`showNumbers`；固定和非固定侧栏使用独立的 `showSidebarIcons`、`showSidebarNumbers`，默认均关闭。编号从 1 开始：中央与固定栏按本组顺序，非固定收纳条按整侧顺序，跨内部组继续递增。`showNewTabButton` 控制各标签栏末尾的新建入口。右键打开目标菜单，不切换标签；菜单含本栏适用的显隐、多行和上下位置设置。

`multiline` 开启后按可用宽度换行，不提供横向滚动条。`wheelExpandRows` 只在多行时起作用：向下滚动展开全部行，向上仅显示活动行；单行时保留值但不生效。拖动按实际行和显示矩形计算插入位置与换行空位，隐藏行不接收投放；从原条带的可见行之间移动不拆窗，首次离开整个条带才形成真实浮窗。

`clipTitles`／`maximumTitleWidth` 控制普通及固定标签栏，`clipSidebarTitles`／`maximumSidebarTitleWidth` 独立控制四边非固定收纳条。两者均直接裁切而不生成省略号，图标、编号、关闭目标另占空间；完整标题、提示及可访问名称保留，拖拽预览按呈现样式选择对应策略。单位、默认值、允许范围见 [标签外观设置](../../docs/design/settings-and-commands-design.md#31-标签外观与颜色)，头文件提供范围常量。设置页、`presentation.toggleTitleClipping`／`presentation.toggleSidebarTitleClipping` 命令和布局快照共用同一份外观。

`setTabColor(tab, colorId)` 选择稳定颜色 ID，空 ID 恢复主题默认，未知 ID 返回 `false`。`tabColorPreset(id)` 查询当前预设。`TabAppearance::colors` 可整体替换默认列表，每项 `TabColorPreset` 分别含普通栏、非固定收纳条的活动／非活动状态；每个 `TabVisualColors` 明确指定 `background`、`text`、`line`、`icon`。当前设置页提供严格 JSON 数组编辑，完整字段、格式和范围见 [设置设计](../../docs/design/settings-and-commands-design.md)。这不代表完整 JSONC 用户设置服务已完成。

`sortTabs(group, TabSortOrder)` 按名称升／降序、打开时间最早／最近优先或访问时间最早／最近优先排序。名称比较忽略大小写，同值保持已有顺序；活动标签、原内容、Pane ID 和 Group ID 保持。标签的 `openedOrder`、`visitedOrder` 随模型保存，切换活动标签时更新访问次序。

`swapGroups(source, target)` 交换两个中央或固定组的槽位，保留分割节点、比例和槽位尺寸；`mergeGroups(source, target, insertionIndex)` 把源标签按原顺序插入目标，保留目标组 ID，清理空源组。`moveGroup(source, target, placement)` 把源组插到目标旁；中央和固定目标均允许四方向，固定目标生成同侧固定组。对应方向、指定目标、已有窗口、新窗口与边栏命令分别使用 `group.swap.*`、`group.merge.*`、`group.moveToGroup`、`group.moveWindow*`、`group.detachFloating` / `group.detachWorkbench`、`group.moveEdge.*` / `group.hideEdge.*`。窗口和边栏移动也有公开 API，均保留组、标签、窗格与原内容。`closeGroup()` 一次申请该组全部标签的关闭许可，拒绝时保留内容。

`addTabToNewGroup(window, content, options, reference, placement)` 原子创建相邻中央或同侧固定组，不在原组发布或激活临时标签；原组的活动标签、顺序和滚动锚点保持。提交前准备失败时返回空 ID，仍存活的内容由调用者持有；属性与焦点恢复原值，同步宿主回调对布局的修改继续保留。提交后模块接管内容，即使刷新通知关闭了新标签或销毁了管理器，仍返回本次提交的稳定 ID。相邻组与窗格拆分命令在工厂销毁管理器后终止，不再检查可能已随管理器销毁的返回指针；工厂须自行安排仍存活拆分结果的清理。新建完整窗口首次显示前按所在屏幕工作区调整整个外框的位置和大小，恢复布局仍使用独立的桌面归一化规则。标题区域不显示悬停气泡，完整标题继续用于窗口标题和可访问名称。

| 操作 | 统一命令 |
| --- | --- |
| 设置单标签颜色 | `tab.color.set`，参数 `color` |
| 名称升／降序 | `group.sort.alphabeticalAscending` / `group.sort.alphabeticalDescending` |
| 打开次序最早／最近优先 | `group.sort.openedAscending` / `group.sort.openedDescending` |
| 访问次序最早／最近优先 | `group.sort.visitedAscending` / `group.sort.visitedDescending` |
| 整组方向移动 | `group.move.left/right/up/down` |
| 整组移到指定组旁 | `group.moveToGroup`，参数 `targetGroup`、`placement` |
| 整组交换槽位 | `group.swap.left/right/up/down` 或 `group.swap`，参数 `targetGroup` |
| 整组合并标签 | `group.merge.left/right/up/down` 或 `group.merge`，参数 `targetGroup`、可选 `insertionIndex` |
| 整组移到已有窗口 | `group.moveWindow`，参数 `targetWindow`；或 `group.moveWindow.previous` / `group.moveWindow.next` |
| 整组新建窗口或移到边栏 | `group.detachFloating` / `group.detachWorkbench`，`group.moveEdge.*` / `group.hideEdge.*` |
| 关闭本组 | `group.close` |

这些操作可以从菜单、命令面板或绑定的快捷键调用。用户已取消 Chrome 式命名、着色、折叠标签集合；模块保留现有分割 `GroupId` 层级，不提供集合 API、集合工具条或关闭集合的归档存储。

层级转换提供以下公开入口；方向参数只允许 `Left`、`Right`、`Top`、`Bottom`，默认 `Right`：

| API | 结果与命令 |
| --- | --- |
| `convertGroupToTab(group, placement)` | 组内全部窗格树组合成一个标签，返回保留的活动 Tab ID；`group.convertToTab`。 |
| `moveGroupIntoTab(sourceGroup, targetTab, targetPane, placement)` | 整组窗格树插入组外目标窗格旁，保留目标标签；`group.moveIntoTab`。 |
| `moveTabIntoTab(sourceTab, targetTab, targetPane, placement)` | 来源标签整个窗格树插入另一标签的目标窗格旁；`tab.moveIntoTab`。 |
| `convertTabToGroup(tab, placement)` | 各 Pane 转成单窗格标签，活动 Pane 保留原 Tab ID，返回目标 Group ID；`tab.convertToGroup`。 |
| `extractPaneToTab(tab, pane)` | 一个窗格提取为同组标签，返回 Tab ID；只有一个窗格时返回原 Tab ID；`pane.extractToTab`。 |

空 `targetPane` 使用目标活动窗格。组来源必须可见；转换保留 Pane ID、内容与专属栏，跨窗口也不重建业务实例。组转标签和并入标签时，来源 Tab 栏迁移到其活动 Pane，与已有栏组合，目标使用逐窗格附加栏。中央和固定来源保留所在区域规则，自动隐藏条带内部组不作为组转换来源。带目标的命令接受 `targetTab`、可选 `targetPane` 和整数 `placement`；缺参时提供窗口→组→标签→窗格→方向选择器。布局菜单与右键菜单按当前组、标签、窗格区分来源，单纯交换和合并仍使用各自命令。

窗格内容的显式最小尺寸或 `minimumSizeHint()` 逐层约束分割、标签组和窗口，动态变化会更新，不能把内容压到相邻窗格上。内容最小尺寸大于屏幕时无法同时满足全窗可见；模块保留内容最小尺寸，超出轴从可用屏幕左侧或上侧起始，不再压低最小尺寸换取全窗显示。

`FloatingPanel` 是可独立使用的临时面板 API。`FloatingPanelOptions.dismissOnOutsidePress` 与 `modal` 分别控制外点关闭和模态，默认自动关闭、非模态；`show()` 立即返回。命令面板和窗口选择器采用这一默认值，宿主也可通过 `showCommandPalette` / `showWindowSelector` 的选项参数修改。非模态外点关闭不会吞掉或合成原点击，面板内打开的菜单仍视为其内部。

窗口菜单末尾按完整窗口与其轻量浮窗列出窗口，可通过 `activateWindow(window)` 或 `window.activate`（`targetWindow`）切换。状态栏右键独立切换 `message`、`workspace` 控件；`setStatusItemVisible` / `statusItemVisible` 和 `window.statusItem.setVisible`（`item`、`visible`）共用偏好。单个控件显隐与整个状态栏显隐独立，保存在各窗口的 `hiddenStatusItems`，不互相覆盖。

窗格上下栏分别使用 `PaneBarSharing::Shared` / `PerPane`，默认共享。创建标签时设置 `TabOptions.topBarSharing` / `bottomBarSharing`，运行中可调用 `setTabTopBarSharing` / `setTabBottomBarSharing`。各 Pane 栏继续由 `setPaneTopBar` / `setPaneBottomBar` 接入；显式 Tab 栏不会因逐窗格模式而复制。对应统一命令为 `tab.topBar.setSharing` / `tab.bottomBar.setSharing`，接受 `sharing: "shared"` 或 `"perPane"`，无参数时切换当前策略。

共享策略进入布局快照；栏控件本身由宿主重建和接入，窗口模块不序列化任意 `QWidget`。

窗口位置、正常尺寸和最大化状态随布局保存，最小化不覆盖正常位置。恢复时按全部屏幕的逻辑可用区域调整越界位置，保留有效的负坐标和跨屏位置。宿主收到 `lastWindowAboutToClose` 时可同步保存仍有效的快照，避免立即关闭最后一个窗口而错过异步保存；该通知中不得修改布局。

完整窗口和轻量浮窗分别使用 `WindowBehavior.hideSingleTabWorkbench`（默认 false）和 `hideSingleTabFloating`（默认 true）。按整个窗口的标签数恰好为 1 判断，只自动收起中央标签栏；固定面板和侧边收纳条不受影响。用户显式隐藏优先，增加标签、关闭标签、窗口升降级或修改策略立即重新计算。拖入条带时的临时标签预览仍显示。

各窗口保存自己的四栏显隐；`visibilityProfiles` 保存两种窗口最后明确编辑的默认值，新建、拖出和提取窗口使用目标类型的默认值。现有窗口不强制同步；保存或关闭旧窗口不覆盖较新的默认值。转换类型时，每个窗口通过 `barLayouts` 记录并恢复自身两种类型的偏好。布局恢复严格验证字段，禅模式的临时隐藏不会写进偏好。嵌入宿主可设置 `WindowOptions.inheritVisibilityProfile = false`，让创建参数中的四项显隐直接生效。

`executeInContext(commandId, context)` 使用已经捕获的窗口、组、标签和窗格 ID；执行前及宿主的可用性回调后再次核验目标。已关闭或移到其他窗口的目标不会被替换成当前焦点。命令面板采用此入口；`execute()` 仍按指定窗口解析当前上下文。

命令目录新增／移除通过 `setCatalogChangedHandler` 通知，绑定修改通过 `setBindingsChangedHandler` 通知。WindowManager 将目录的界面刷新合并到下一事件循环；注册过程中不提前调用命令的 `enabled`，批量注册也不会递归刷新菜单。需要观察界面贡献的宿主应等待该提交，命令本身注册后即可查询和执行。

## 规范与验证

- [窗口系统设计](../../docs/design/window-system-design.md)
- [工作区与导航](../../docs/design/workspace-design.md)
- [双窗口与拖拽](../../docs/design/floating-window-design.md)
- [窗口交互细则](../../docs/design/window-interaction-design.md)
- [测试要求](../../docs/development/testing-design.md)

运行 `pwsh tools/tests/quiet-desktop.ps1 -Elevate pwsh tools/verify.ps1` 验证 Muxora、独立模块、安装后的外部接入及部署启动。用例由程序断言模型、几何、颜色、输入结果与内容身份，结果可自行运行复现；不依赖人工或模型阅读截图，不使用 computer use 执行测试。安装后的外部夹具还检查颜色、排序和整组跨窗移动。具体执行状态以预览说明及实际日志为准；失败须修复实现，不自行降低标准。
