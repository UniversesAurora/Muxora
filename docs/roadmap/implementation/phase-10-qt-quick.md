# 阶段 10：保持外观与交互的 Qt Quick 迁移

上游：[ADR 0013](../../adr/0013-qt-quick-rendering.md)、[Quick 呈现设计](../../design/quick-rendering-design.md)。本阶段按用户要求优先推进，原有未完成步骤保留。

## 剩余工作与分工（2026-11-04 起）

2026-11-04 起 Muxora 只用 Quick 启动（见 10.3 的切换决定）。还要做的是把 Widgets 才有的功能在 Quick 里补齐，然后删除 Widgets 代码。顺序和分工如下，每一项的要求写在对应子步里。分工按 [AGENTS.md](../../../AGENTS.md#轮流开发与交接)：新界面组件和新外观由 Claude 做，执行者（目前是 Codex）做不涉及新外观的步骤；执行者做到需要新增界面功能或新外观的地方就停下，写进交接记录。已有菜单和工具栏按原外观和交互改用 Quick，复用现有按钮、主题与菜单，属于现有功能迁移，由执行者继续完成。

| 顺序 | 工作 | 位置 | 负责 |
| --- | --- | --- | --- |
| 1 | 删除 Widgets 占位内容 | 10.6 删除单元 1 | 执行者 |
| 2 | 窗格与标签的上下栏、输入法与焦点接到 Quick | 10.3 子步 6 | 执行者（Muxora 文件标签的两条栏的外观由 Claude 做） |
| 3 | 命令菜单改为与呈现无关的数据描述 | 10.4a | 执行者 |
| 4 | 工具栏排布规则抽成不依赖控件的模块 | 10.4e 前半 | 执行者 |
| 5 | `quiet-desktop.ps1` 运行期间唤醒并保持屏幕亮着 | 10.5 工具 | 执行者 |
| 6 | 现有弹出菜单、右键菜单、菜单栏迁到 Quick | 10.4b–d | 执行者 |
| 7 | 工具栏、状态栏的 Quick 渲染 | 10.4e 后半、10.4f | 执行者迁移既有工具栏；状态栏见 10.4f |
| 8 | 命令面板、窗口选择器、拖动中按 Tab 选目标 | 10.4g | Claude |
| 9 | 设置页与快捷键页：Claude 先做 Quick 表单控件和第一页样板，执行者按样板迁移其余分区 | 10.4h | Claude，然后执行者 |
| 10 | 对话框（改名、侧栏面板尺寸、关于、脚本输入框） | 10.4i | Claude |
| 11 | 无障碍；菜单栏单按 Alt 的键盘导航 | 10.4j、10.4k | 执行者 |
| 12 | 帧成本测量与调优 | 10.5 | 执行者测量，Claude 定优化 |
| 13 | 管理器去掉 Widgets 呈现，删除其余 Widgets 代码和测试 | 10.6 删除单元 3–5 | Claude 拆步，执行者执行 |
| 14 | 用户试用、经授权的全量测试、部署依赖和文档收尾 | 10.6 收尾 | Claude |

1–5 互不依赖，可以按顺序交给执行者；6 沿用现有菜单设计继续迁移，后续仍按表内分工推进。每完成一块 Quick 版，就在同一轮删掉被替代的 Widgets 代码和只测它的用例（10.6 列出了对应关系），不留到最后。

**测试**：Widgets 版不再维护，它的套件不再运行；改到 Widgets 代码时只要求能编译，删除时连同用例一起删。例外是 10.4a 和 10.4e 前半这类把规则从 Widgets 抽出来的重构，对应的 Widgets 套件是行为不变的依据，要跑。Quick 和共用逻辑按 [测试要求](../../development/testing-design.md) 只跑相关套件，桌面套件经 `quiet-desktop.ps1 -Elevate`；依赖帧推进的断言（动画结束、帧数）要先唤醒屏幕，原因见 [Qt Quick 经验](../../development/lessons/qt-quick.md#测量帧率前先确认屏幕处于工作状态)。对比原版用 `out/package/muxora-widgets-reference/`（`widgets-reference` 标签的 Release 包）。

## 10.1 基线、帧动画和保留绘制基础

**状态**：绘制与帧基础已实现并定向验证，尚未接入默认应用。主体阴影的冷资源、保留纹理与尺寸顶点更新已定向验证；接合阴影的 [自有冷资源](../../development/verification-log/2026-10.md#接合阴影的自有冷资源2026-10-28) 已分离，主体与单接合补片的 [GPU 合成](../../development/verification-log/2026-10.md#quick-接合阴影的保留合成2026-10-28) 已定向验证，文字和完整应用的呈现测量未完成，继续 10.2；不能据此视为整体高刷新验收。结果见 [基线记录](../../development/verification-log/2026-10.md#按窗口帧取样的动画值与迁移前基线2026-10-27)、[阴影资源](../../development/verification-log/2026-10.md#主体阴影资源与绘制后端分离2026-10-28)、[主体阴影 GPU 记录](../../development/verification-log/2026-10.md#quick-主体阴影保留纹理与连续缩放2026-10-28) 和 [GPU 记录](../../development/verification-log/2026-10.md#quick-保留绘制与屏幕刷新验证2026-10-27)。

- 保存当前 Widgets 分隔条和绘制基线；把动画时长与曲线从 QWidget 适配中分离，增加按单调帧时间推进的值动画。
- 增加可选 Quick 构建目标和有界路径几何；建立 `QQuickItem` 到保留场景图节点的同步，复用已有接合轮廓。
- 检查四方向、非凸轮廓、DPI、重定向动画、资源生命周期和空闲停止；分别记录逻辑成本和 GPU 帧提交。
- 完成条件：基础接口与定向检查可运行，无逐帧整窗栅格化，未把基础渲染测量写成完整应用性能。

## 10.2 分离标签交互与呈现

**状态**：进行中。标签栏的状态、布局、输入和动画已集中到 Gui 层的 `TabStripController`，Widgets 的 `TabBar` 和 Quick 的 `TabBarItem` 只做适配，见 [控制器记录](../../development/verification-log/2026-10.md#标签状态与-widgets-门面分离2026-10-30)。Quick 已有标签表面、标题、图标、编号、文件夹头、条带装饰、边缘渐隐和拖动前景的保留绘制组件，基础层与前景层经统一帧发布接到控制器，见 [统一发布](../../development/verification-log/2026-10.md#标签栏基础与前景的统一发布2026-10-29) 和 [审查记录](../../development/verification-log/2026-10.md#gpt-审查-deepseek-提交标签合成与运行段资源2026-10-29)。

尾部控件（新建、类型、标签列表、计数、图标模式、侧栏按钮）和应用窗口接线已随 10.3 完成；离屏合成时的字体外观待用户试用。各子步的改动和验证按日期记在 [验证日志](../../development/verification-log/2026-10.md)。

- 从 `TabBar` 分离输入、滚动、展开、拖拽与动画状态，保留已有布局计算；Widgets 和 Quick 读取同一份呈现数据，不复制规则。
- 把 `TabDragVisual` 中的 QPainter 回调改成有界渲染描述，明确资源所属窗口。
- 标签卡片、阴影、字形、图标、渐隐与命中保持现有设计。验证各方向、置顶尺寸、多行与独立置顶行、拖入拖出、右侧镜像及收起浮层。

## 10.3 Quick 窗口、内容与原生交互

**状态**：进行中，2026-10-30 起由 Claude 负责。已有部件：共用分割布局与 `SplitTreeItem`、`GroupSurfaceItem` 背板、接到控制器的 `TabBarItem`、`ContentHost` 内容宿主、以 QWindow 为核心的原生框架与系统菜单，见 [分割记录](../../development/verification-log/2026-10.md#quick-组与窗格的共用分割布局2026-10-29) 和 [收尾记录](../../development/verification-log/2026-10.md#quick-输入qwindow-与内容宿主收尾2026-10-30)。`QuickWorkspace` 已能按模型建窗口、中央停靠区和窗格内容，见 [工作区记录](../../development/verification-log/2026-10.md#quick-工作区按模型建窗口2026-10-31)；Muxora 2026-11-04 起只用 Quick 启动，占位页由编译好的 QML 组件显示，快捷键、固定区域、自动隐藏侧栏、窗口标题栏，以及标签在栏内、组间、窗口间的拖动、拖出成浮窗和浮窗拖标题停靠可用；标签等菜单入口已接入，命令工具栏已接入；工具栏布局编辑与状态栏仍在迁移。

用户 2026-10-30 明确：整个界面必须完全迁移到 Quick，Widgets 路径在 10.6 删除。迁移期间两个后端共存，按下面的结构过渡。

用户 2026-11-04 决定提前切换：Muxora 立即只用 Quick 启动，Widgets 版不再维护、不再跑它的测试。切换前用当时的代码（`widgets-reference` 标签）打一份 Release 包，单独放在 `out/package/muxora-widgets-reference/`，只用来启动原版对比，日常 Release 不覆盖它。菜单、工具栏、状态栏、设置等在 Quick 里重做（10.4），每做完一块就删掉对应的 Widgets 代码和测试，不等到最后一次删；在此之前这些入口在 Quick 里暂时没有。下面的过渡结构据此只保留到 Widgets 代码删完。

**过渡结构**（2026-10-31 由 Claude 调整：原定从管理器抽出窗口与停靠表面两个接口，改为下面的监听方式，避免先大改 1300 多行的管理器视图代码、到 10.6 再删掉一半）

- `WindowManager`、`LayoutModel`、命令和布局事务只有一份。管理器构造时选择呈现方式：`Widgets`（默认，现有窗口）或 `External`（不建任何 Widgets 视图；模型、命令、焦点状态和 `layoutChanged` 通知照常）。所有修改都经 `refresh()` 发出 `layoutChanged`，这是 Quick 视图的唯一更新来源。
- 新增 `QuickWorkspace`：监听管理器，为每个模型窗口建 Quick 窗口，按模型搭建停靠表面（`GroupItem`、`RailItem`、分割树），用户操作调用管理器现有的公开接口（关闭、激活、移动、设比例等），不复制命令或布局规则。
- 拖动的判断（命中、停靠引导、插入位置）先从 `DragController` 抽成不依赖控件的共用部分，Widgets 和 Quick 各自只做输入与原生移动的适配；原生移动用 `QWindow`。
- 宿主内容：`External` 模式下 `addTab`、`splitPane` 不带 QWidget，内容由 `QuickWorkspace` 的内容工厂按窗格 ID 和描述创建，挂在 `ContentHost`；栏位同理。Muxora 原来用 `--quick` 选择 Quick，2026-11-04 起只用 Quick，见上面的切换决定。
- 10.6 删除 Widgets 时，管理器里的视图代码整块删除，`External` 成为唯一模式，不留抽象层；`WindowManager` 随之移出 Widgets 库（在此之前 Quick 库经它链接 Widgets）。
- 不在 Quick 窗口里嵌 QWidget，也不用原生子窗口遮挡 Quick 浮层。

**子步**（按顺序，每步可单独构建和验证）

0. 已完成：Widgets 路径上交出时已失败的用例已修好，36 个相关套件通过，见 [记录](../../development/verification-log/2026-10.md#修复交出时已失败的-widgets-用例2026-10-31)。标签组规则已移到共用的 `TabGroupController`。
1. 已完成（`0b7ab5f`）：标签组容器 `GroupItem`，与 Widgets 版截图对照一致；卡片圆角处暂用停靠区底色，Widgets 会在那里重绘底板（含几个阴影像素），留到 10.5。原计划：`TabBarItem`、`GroupSurfaceItem`、内容区和竖栏尾部，布局用 `layoutTabGroup`，表面与焦点动画用共用帧状态；组按 `GroupId` 复用，内容由 `ContentHost` 挂载。
2. 已完成（右键菜单随 10.4）：停靠表面 `DockSurfaceItem`。中央分割树和四边固定区域已完成，组和标签页按 ID 复用，标签增删时只重建该组的标签项，改名和颜色原地更新。固定区域按组保存的宽度排在中央四周，窗口变大时只有中央变大，拖动分隔条保存新宽度。自动隐藏的侧栏轨道按 Widgets 的网格排在窗口边缘，点按钮在窗口上方打开展开面板（标题、固定、关闭和页面），Esc 或点别处关闭。面板的尺寸调整手柄和按住标题拖动标签已完成，见 [面板记录](../../development/verification-log/2026-11.md#quick-展开面板的尺寸手柄和标题拖动2026-11-04)；右键菜单随 10.4。
3. 进行中：整窗 `WorkspaceQuickWindow` 已用原生框架（去掉系统标题栏、保留边缘缩放和 Snap 的最大化按钮命中），顶部是 `WindowChromeItem`：居中的标题、按设置排列的最小化／最大化／关闭按钮，按住标题拖动窗口、双击最大化。菜单、工具栏和状态栏随 10.4；窗口族规则已核对：Quick 浮窗和工作台都是独立的普通顶层窗口（不设所有者窗口，可单独最小化），标题照 Widgets 写出活动标签和“浮动窗口”，见 [标题记录](../../development/verification-log/2026-11.md#quick-窗口标题跟随活动标签2026-11-04)。混合 DPI 下的移动要两块缩放不同的显示器，本机只有一块，未验证。管理器的显示、激活和窗口间切换经 `WindowPresenter` 作用到 Quick 窗口，见 [窗口请求记录](../../development/verification-log/2026-11.md#quick-窗口接收管理器的显示和激活2026-11-04)；关闭全部窗口在 External 下本来可用。视图模式（窗口、组、窗格全屏和禅模式）与栏的显隐在 Quick 里按 Widgets 的规则生效，见 [视图状态记录](../../development/verification-log/2026-11.md#quick-窗口按视图状态显示2026-11-04)；窗口选择器要用菜单，随 10.4。原计划：`QQuickWindow` 加原生框架、标题栏、工具栏占位与状态栏；最大化、全屏、Snap、DPI 和窗口族规则与现有窗口一致。
4. 进行中：管理器的 `External` 模式与不带控件的 `addTab`／`splitPane` 已完成（`5d38cef`）；`QuickWorkspace` 按模型建窗口、按窗格建内容，关闭窗口经管理器决定。Muxora `--quick` 能打开并显示初始布局（冒烟启动和截图）；切换、分割、关闭标签和关闭窗口由 `quick-workspace` 用例覆盖，应用里没有试用；`--quick` 恢复和保存同一个布局文件，见 [布局记录](../../development/verification-log/2026-11.md#quick-恢复和保存布局2026-11-04)。快捷键（含续键）经管理器同一套分发在 Quick 窗口里生效，按键先到窗口再到焦点项。
5. 已完成（按 Tab 选目标窗口随 10.4）：拖动。拖放后目标窗口由管理器的 `showWindow` 提到前面并激活，见子步 3。栏内换位、拖出成浮窗、跨窗口停靠、拖入侧栏，全部经 `QWindow` 完成。共用命中已完成：`dropTargetAt()` 只经 `DropSurface` 接口取数据，Widgets 改为调用它，见 [记录](../../development/verification-log/2026-11.md#拖放命中移到两套停靠区共用的查询2026-11-01)。Quick 的栏内换位、组间和窗口间移动、停靠引导已完成，见 [拖动记录](../../development/verification-log/2026-11.md#quick-标签在栏内组间和窗口间拖动2026-11-01)；标签拖出成浮窗已完成，见 [拖出记录](../../development/verification-log/2026-11.md#quick-标签拖出成浮窗2026-11-01)；真实鼠标下拆出的窗口交给系统移动，浮动窗口拖标题可停靠，见 [系统移动记录](../../development/verification-log/2026-11.md#quick-拖动交给系统移动浮窗拖标题可停靠2026-11-03)。到标签栏上方时收回鼠标、窗口隐藏、由目标标签栏在指针处画出带过去的标签，见 [载体记录](../../development/verification-log/2026-11.md#quick-标签栏上方由目标栏画载体2026-11-03)。标签夹拖出成浮窗已完成，见 [标签夹记录](../../development/verification-log/2026-11.md#quick-标签夹拖出成浮窗2026-11-03)。侧栏内的插入位置和从侧栏开始拖动已完成，见 [侧栏记录](../../development/verification-log/2026-11.md#quick-侧栏的拖入和拖出2026-11-03)。落下时标签从指针处滑到位已完成，见 [落位记录](../../development/verification-log/2026-11.md#quick-标签落下时滑到位2026-11-04)。拖到侧栏上方时窗口隐藏、目标窗口在指针处画出侧栏按钮，见 [侧栏载体记录](../../development/verification-log/2026-11.md#quick-侧栏上方画出侧栏按钮2026-11-04)。拆出的标签夹可以放到停靠引导和侧栏上，见 [标签夹停靠记录](../../development/verification-log/2026-11.md#quick-拆出的标签夹放到引导和侧栏上2026-11-04)。在目标窗口上停一会儿会把它提到前面，见 [唤起记录](../../development/verification-log/2026-11.md#quick-拖动中唤起目标窗口2026-11-04)。拖动中滚轮滚动指针下的标签栏或侧栏，见 [滚轮记录](../../development/verification-log/2026-11.md#quick-拖动中滚轮滚动指针下的标签栏2026-11-04)。拖到标签栏上时目标组显示被拖内容的画面，见 [内容预览记录](../../development/verification-log/2026-11.md#quick-拖到标签栏上时显示内容画面2026-11-04)。Widgets 拖动中按 Tab 弹出窗口列表选目标，要用菜单，随 10.4 做。做法（2026-10-31 由 Claude 定）：
   - `DragController::hit()` 只经停靠区的查询取数据（组和标签栏矩形、插入位置、侧栏矩形、停靠条、内容区）。把这些查询定义成一个不含控件的接口，`DockSurface` 和 `DockSurfaceItem` 各自实现；命中、停靠引导和预览矩形移到以该接口为参数的共用函数，Widgets 版改为调用它，行为不变（先跑 Widgets 拖动套件确认）。
   - Quick 一侧另写输入适配：标签栏的 `TabPointerDecision` 拖动动作开始拖动，指针移动时调用共用命中函数，放下时调用管理器现有的 `moveTab`、`moveTabToEdge`、`moveTabToEdgeBar`、`moveTabToWindow`；停靠引导和插入预览画在 `DockSurfaceItem` 的前景层。
   - 拖出成浮窗：管理器在 `External` 下建模型窗口，`QuickWorkspace` 建它的 Quick 窗口，之后用 `QWindow::startSystemMove()` 交给系统移动，回到标签栏上方时收回鼠标，与 Widgets 的原生移动规则相同。
   - 顺序：先抽共用命中并验证 Widgets，再做栏内换位与跨组移动，再做拖出浮窗和侧栏。
6. 进行中（执行者）：宿主内容和栏位的 Quick 接口。内容焦点、输入法作用域和宿主显隐回调已接通，见 [记录](../../development/verification-log/2026-11.md#quick-内容焦点与显隐接到管理器2026-11-04)。宿主内容已由内容工厂按描述创建（子步 4）。上下栏接口、共享规则、最小尺寸、禅模式和跨窗格移动已实现，见 [上下栏记录](../../development/verification-log/2026-11.md#quick-标签与窗格的上下栏接口2026-11-04)；层级转换的栏转移与合并也已接通，见 [收尾记录](../../development/verification-log/2026-11.md#quick-层级转换保留宿主栏2026-11-04)。执行者的接口部分已完成，Muxora 文件栏组件和用户试用仍待完成。原定要求如下：
   - **上下栏**：`QuickWorkspace` 增加 `setTabBar(TabId, QQuickItem*, bool top)` 和 `setPaneBar(PaneId, QQuickItem*, bool top)`，传空指针移除；工作区接管所有权，标签或窗格关闭时删除，在组间、窗口间移动时保持同一个项（和窗格内容一样按 ID 保存）。排布照 Widgets 的 `panes/tab_content.cpp`：`TabOptions::topBarSharing`／`bottomBarSharing` 为 `Shared` 时一条栏横跨整个标签，`PerPane` 时每个窗格各有一条；栏高取项的隐含高度；栏计入最小尺寸；禅模式隐藏全部栏（`windowViewState().zen`）；窗格全屏时只排那个窗格的栏。用 `quick-workspace` 用例覆盖两种共享方式、移动后身份不变、关闭时删除、禅模式隐藏，用普通 `QQuickItem` 当栏，不做新外观。
   - **输入法与焦点**：Quick 内容获得焦点时，管理器的当前窗格跟着变（相当于 `WindowManager::eventFilter` 里 Widgets 的 `FocusIn` 分支），输入法按 [8.16](phase-8-ui-quality.md#816-按层级保存输入法状态) 的作用域保存和恢复；Widgets 版在 `input_method_scopes_widgets.cpp`。
   - **Muxora**：文件标签的地址栏和信息栏（Widgets 版见 `placeholder_content.cpp` 的 `populateInitialWorkspace`）需要两个 QML 栏组件，属于新外观，由 Claude 做；执行者只做接口。已完成，见 [记录](../../development/verification-log/2026-11.md#muxora-文件页的地址栏和信息栏2026-11-06)。
7. 完成条件：两个组、窗格、分隔条和标签跨窗口移动都能完整使用。原定“缺项不切换默认”由用户 2026-11-04 的决定取代：先切换，缺的在 Quick 里补。子步 6 完成、用户试用过 Quick 版的拖动与布局后，10.3 才算完成。
7. 完成条件：两个组、窗格、分隔条和标签跨窗口移动都能完整使用。原定“缺项不切换默认”由用户 2026-11-04 的决定取代：先切换，缺的在 Quick 里补。

## 10.4 工具栏、侧栏、菜单与设置

**状态**：进行中（2026-11-06）。10.4a–i 已完成；10.4k 菜单栏键盘导航已实现，10.4j 无障碍进行中。各项的记录在下面的条目里。外观照 Widgets 参考版（`out/package/muxora-widgets-reference/`）和 [界面设计规范](../../design/ui-design-system.md)，不借此重新设计；[8.27 菜单与浮动面板重做](phase-8-ui-quality.md#827-菜单与浮动面板重做) 的新要求仍等用户细化，不在本步做。

**和 Widgets 版对照的标准**（用户 2026-11-06）：窗口框架、标签、标签栏、菜单、工具栏、状态栏、面板和对话框要和 Widgets 参考版一比一一致，位置和尺寸以同一进程里的 Widgets 窗口为准（`QuickWorkspaceTest::rowsMatchTheWidgetsWindow` 的做法），截图对比要在同一时刻拍。例外：Muxora 的占位内容页（欢迎、会话、文件、传输）以后会重写，不要求还原；文字的纵向位置只要合理，不要求和原版逐像素一致（现在比原版高 1 个物理像素，是两套渲染器给基线取整不同）。

- **10.4a 命令菜单数据模型（执行者）**：`commands/command_menus.cpp` 现在一边决定菜单内容一边建 `QMenu`。拆成与呈现无关的菜单描述：条目的命令 ID、标题、快捷键文字、能否勾选与是否勾选、可用、可见、分隔、子菜单，以及动态部分（窗口列表、标签颜色、新建标签类型、贡献菜单）。应用菜单、浮窗菜单和各处右键菜单都先生成描述，`QMenu` 只按描述生成，行为不变。`menu-structure` 改为检查描述，保留现有断言。完成标准：描述不依赖 Qt Widgets，`QMenu` 一侧只剩生成代码；`menu-structure`、`popup-menu`、`sidebar-menu` 通过（这一步仍要跑这几个 Widgets 套件，因为它们是行为不变的依据）。
  - 已完成：静态菜单、命令状态、应用及浮窗扩展、窗口列表、右键菜单、标签颜色、新建类型和标签栏选项由不含控件的值描述生成；相关结构断言检查描述，实际弹出与执行检查保留。结果及边界见上述记录。
- **10.4b Quick 弹出菜单（执行者）**：独立顶层 Quick 窗口，按 10.4a 的描述绘制；支持多级子菜单、键盘导航、勾选、快捷键文字、分隔、禁用、超出屏幕时翻转、点外面关闭，原生弹出窗口的行为照 `popup_menu.cpp`。
  - 共用呈现已实现：使用 Qt Quick Controls 的独立窗口菜单，复用现有主题和保留文字／图标，键盘、捕获、长菜单与释放路径通过定向检查，见 [记录](../../development/verification-log/2026-11.md#现有弹出菜单接入-quick-controls2026-11-05)。工作区入口和菜单栏继续由后续两步接入，用户试用待完成。
- **10.4c 右键菜单（执行者）**：标签、标签夹、标签栏空白处、标签组、窗格内容、侧栏按钮、展开面板、新建标签下拉、标签列表。`GroupItem::stripMenuRequested`、`groupMenuRequested`、`RailButtonItem::menu` 等回调已留好。完成后欢迎页“右键标签或浮窗标题栏……”这句提示才成立。
  - 入口与状态接线已完成：标签、标签夹、空白栏、组回调、侧栏和新建类型已接到 Quick 菜单；实际标签栏／侧栏目标及查询期间销毁已定向验证，见 [记录](../../development/verification-log/2026-11.md#quick-标签与侧栏接入现有菜单2026-11-05)。窗格内容、展开面板及管理器请求路由已接通并定向验证，见 [后续接线](../../development/verification-log/2026-11.md#quick-窗格展开面板与管理器菜单请求2026-11-05)；全部标签列表已接入并定向验证，见 [列表记录](../../development/verification-log/2026-11.md#quick-全部标签列表2026-11-05)；文本编辑常用菜单也已合并并定向验证，见 [编辑记录](../../development/verification-log/2026-11.md#quick-文本编辑与窗格菜单合并2026-11-05)；浮窗标题入口也已接通，见 [标题记录](../../development/verification-log/2026-11.md#quick-浮窗标题菜单入口2026-11-05)；上下文菜单状态刷新已接入，见 [刷新记录](../../development/verification-log/2026-11.md#quick-上下文菜单保留对象并刷新状态2026-11-05)；新建类型菜单状态保持已接入，见 [收尾记录](../../development/verification-log/2026-11.md#quick-新建类型菜单状态收尾2026-11-05)；主窗口标题区菜单随工具栏接入，用户试用待完成。旧呈现调用尚未删除，未满足整块删除条件。
- **10.4d 菜单栏（执行者）**：工作台在标题行显示文件／视图／布局／窗口／工具／帮助，浮窗显示自己的浮窗菜单；含窗口列表。
  - 进行中：工作台／浮窗的常规菜单栏已接入标题行，窗口列表、动态贡献、键盘路径、菜单栏右键和释放路径已定向验证，见 [记录](../../development/verification-log/2026-11.md#quick-菜单栏接入标题行2026-11-05)；窄窗口收纳、隐藏菜单助记键与跨收纳边界的方向键已接入并定向验证，见 [收纳记录](../../development/verification-log/2026-11.md#quick-窄窗口菜单收纳2026-11-05)。工具栏组合布局已接入同一注册表，见 [记录](../../development/verification-log/2026-11.md#quick-窗口内容并入工具栏注册表2026-11-05)。标题文字测量已移出窗口尺寸变化路径，局部成本与验证边界见 [记录](../../development/verification-log/2026-11.md#quick-标题测量移出尺寸变化2026-11-05)。用户试用待完成。
- **10.4e 工具栏（执行者迁移既有外观）**：前半把 `chrome/command_strip.cpp` 里的排布规则（行与区域、弹性与按内容、压缩、最小宽度、重置、锁定、保存格式）抽成不依赖控件的模块，Widgets 改用它，`toolbar-*`、`multirow-toolbar` 通过；后半复用既有样式做 Quick 渲染、拖动换位和选项菜单，宿主和 Lua 的自定义工具栏随之显示。保存格式不变；做完后去掉管理器里为 Quick 原样保留工具栏布局的 `keptStrips`。
  - 前半已完成：共用逻辑覆盖宽度分配、压缩、弹性归属、重置优先级、行与区域、移动空位回收、约束及当前格式存取；各呈现提供当前逻辑尺寸下的内容测量。控件接线和相关套件已通过，提取前后布局调用成本相近，见 [记录](../../development/verification-log/2026-11.md#工具栏区域与空位回收完成共用规则2026-11-04)。窗口中的 JSON 合并规则也已迁入共用状态层，见 [合并记录](../../development/verification-log/2026-11.md#工具栏布局合并脱离窗口控件2026-11-05)。命令工具栏已接入 Quick 窗口，覆盖上下区域、保存布局读取、动态命令状态、鼠标／键盘执行和溢出菜单，见 [接入记录](../../development/verification-log/2026-11.md#quick-命令工具栏接入窗口2026-11-05)。菜单／标题／窗口按钮已并入同一注册表，保存布局读取、内容显隐与原生命中已定向验证，见 [统一记录](../../development/verification-log/2026-11.md#quick-窗口内容并入工具栏注册表2026-11-05)。管理器／Lua 的布局编辑、写回保存和按窗口类型的归属已接通，`keptStrips` 已去掉，不带参数的工具栏命令在 Quick 菜单里给出选择，见 [编辑记录](../../development/verification-log/2026-11.md#quick-工具栏编辑与布局写回2026-11-05)。工具栏区域的右键菜单（锁定、各条显隐，后接窗口菜单）和每条工具栏的选项菜单（溢出按钮、重置大小、弹性空间、增删按钮）已接入，见 [菜单记录](../../development/verification-log/2026-11.md#quick-工具栏区域菜单与选项菜单2026-11-05)。解锁后的手柄拖动（换位、边界调宽、移到另一区域或新行、Esc 取消）也已接入，见 [拖动记录](../../development/verification-log/2026-11.md#quick-工具栏手柄拖动2026-11-05)。Quick 工具栏的迁移到此完成。所有 Quick 按钮和标签栏的悬停提示也已接入，见 [提示记录](../../development/verification-log/2026-11.md#quick-悬停提示2026-11-05)。Widgets 的 `CommandStripHost` 仍被 Widgets 呈现使用，随 10.6 删除单元 3–5 删除。
  - 审查记下的写回要求（[审查记录](../../development/verification-log/2026-11.md#审查-codex-的-103-子步-6-至-104e-提交2026-11-05)）已随写回处理：只在保存内容或工具栏注册变化时解码；Quick 保存的宽度不低于可恢复的最小值；工具栏查询读 Quick 一侧的布局。还差一个 Quick 多行区域的用例（行从外侧排起、区域高度等于各行之和）。
- **10.4f 状态栏（Claude）**：工作区信息、临时消息、宿主和 Lua 的状态项、右键隐藏状态项，照 `chrome/status_bar.cpp`。
  - 已完成：Quick 工作台窗口底部的状态栏，覆盖消息与临时消息（按键序列等待）、工作区信息、宿主／Lua 状态项（带命令的可点击、可键盘激活）、右键逐项显隐和悬停提示；浮窗照旧没有状态栏，见 [记录](../../development/verification-log/2026-11.md#quick-状态栏2026-11-05)。Widgets 的 `WorkspaceStatusBar` 随 10.6 删除。
- **10.4g 浮动面板（Claude）**：命令面板、窗口选择器（`floating_panel.cpp`、`command_dialogs.cpp`），以及拖动中按 Tab 弹出窗口列表选目标（Widgets `DragController::selectTarget`）。
  - 命令面板和窗口选择器已在 Quick 里实现：一个共用面板（无边框窗口居中于所属窗口，搜索框、带表头的列表、提示和两个按钮），行内容和筛选规则与 Widgets 共用 `panel_rows`，见 [记录](../../development/verification-log/2026-11.md#quick-命令面板与窗口选择器2026-11-05)。和 Widgets 的差别：窗口选择器的行前还没有窗口图标。拖动中按 Tab 选目标窗口也已接入，见 [记录](../../development/verification-log/2026-11.md#quick-拖动中按-tab-选目标窗口2026-11-05)；10.4g 完成，Widgets 的面板和 `DragController::selectTarget` 随 10.6 删除。
- **10.4h 设置页与快捷键页（Claude 后执行者）**：Claude 先做 Quick 表单控件（开关、下拉、数值、滑块、列表、快捷键录入，类型选择按 [设置设计 §3](../../design/settings-and-commands-design.md#3-全局设置与编辑)）和第一个分区的样板；执行者照样板迁移其余分区和快捷键页，遇到样板里没有的控件停下。
  - Claude 部分已完成：设置项改为不依赖界面的 `SettingDescription`，Quick 设置页按描述生成，应用、界面、窗口行为和拖动分类已迁移；表单控件含复选框、下拉框、数字框、输入框、滑条加数字框、按键录入框、列表、按钮和多行输入框，外观由 `SettingsStyle` 统一提供；按键录入规则与 Widgets 共用，录入时不触发快捷键，见 [记录](../../development/verification-log/2026-11.md#quick-设置页与表单控件2026-11-06)。标签配色用新的 JSON 类型（多行文本、提示和应用按钮）放在“标签外观”分类，见 [配色记录](../../development/verification-log/2026-11.md#quick-设置页的标签配色编辑2026-11-06)。
  - 显示、标签外观和置顶标签分类已迁移为描述，控件沿用既有类型，标签外观排在配色前。栏显隐按设置页当前所属窗口读写，跨窗口移动保留页面与控件；标题裁切、多行及置顶尺寸的依赖保持原行为，见 [记录](../../development/verification-log/2026-11.md#quick-设置页补齐显示与标签分类2026-11-06)。
  - 快捷键页已迁移为 Quick，复用既有控件和共用校验，支持搜索、录键、参数、增删绑定和外部修改时保留未应用编辑。10.4h 完成，见 [记录](../../development/verification-log/2026-11.md#quick-快捷键页2026-11-06)。Widgets 设置页和快捷键页随 10.6 删除。
  - 快捷键页的搜索、参数 JSON 与 `runCommands` 校验、冲突与前缀提示、外部绑定修改检查已从 Widgets 页移到 `settings/shortcut_edit`，旧页调用共用规则，原套件通过。Quick 页也已接入，见 [记录](../../development/verification-log/2026-11.md#快捷键编辑规则脱离控件2026-11-06)。
- **10.4i 对话框（Claude）**：重命名标签／组／标签夹（现用 `QInputDialog`）、侧栏面板尺寸（`peek_size_command.cpp`）、关于、Lua 的输入框（`src/app/scripting_setup.cpp`）、Lua 父窗口查询（`src/app/script_services.cpp`）。宿主的关闭确认由宿主提供，Muxora 的那个也一起换。
  - 已完成：管理器的 `openDialog` 让对话框不依赖呈现、不阻塞调用方，Quick 下是带原生标题栏的独立窗口；重命名标签／组／标签夹、侧栏面板尺寸、关于、Lua 输入框和 Lua 通知已改用它，见 [记录](../../development/verification-log/2026-11.md#quick-对话框2026-11-06)。Muxora 没有设置关闭确认，无需替换。不带参数时仍弹 Widgets 菜单的选择命令（标签和标签夹颜色、选目标标签组／窗口／标签／标签夹）也改为在 Quick 菜单里列出选择，见 [选择菜单记录](../../development/verification-log/2026-11.md#quick-下的选择菜单2026-11-06)。Widgets 呈现下的旧对话框和菜单随 10.6 删除。
- **10.4j 无障碍（执行者）**：Quick 的标签栏、标签组、侧栏按钮、按钮和标题行提供无障碍角色、名称和状态，照 Widgets 的 `TabBarAccessible`；键盘焦点顺序与 Widgets 一致。
  - 进行中：按钮已通过 Qt 公开的 `Accessible` 接入名称、状态与原激活路径，读屏树可发现性和接口释放已定向验证；标签栏、标签组、侧栏、标题与焦点顺序继续，见 [记录](../../development/verification-log/2026-11.md#quick-按钮的读屏信息与动作2026-11-06)。
- **10.4k 菜单栏单按 Alt 的键盘导航（执行者，用户 2026-11-06 要求）**：Widgets 的 QMenuBar 在 Windows 上单按一下 Alt（按下到松开之间没有别的键、没有鼠标操作）进入键盘导航，Quick 菜单栏还没有。照 Qt 源码 `qmenubar.cpp`（`QMenuBar::eventFilter` 的 `altPressed`、`QMenuBarPrivate::setKeyboardMode`、`keyPressEvent`）实现，外观复用现有的悬停底色，不做新样式：
  - 进入：Alt 松开时进入键盘导航，高亮第一个可见菜单，记下原来的焦点项，菜单栏取得键盘焦点；访问键下划线显示（`PopupMenu::setMnemonicsShown`，现在只跟 Alt 的按下松开和 Alt 打开的菜单，见 `WorkspaceQuickWindow::updateMnemonics`）。按 Alt 期间按了别的键、点了鼠标、窗口失去激活，都不进入。
  - 导航中：左右键在可见菜单间移动并循环，窄窗口时包括收纳按钮；下键、上键、回车、空格打开高亮的菜单并选中第一项；按某个菜单的访问键字母直接打开它；菜单打开后左右键切换到相邻菜单（现有行为）。
  - 退出：Esc、再按一次 Alt、鼠标点击、窗口失去激活都退出，焦点回到原来的项，下划线收起。
  - 测试加在 `quick-popup-menu`（参考 `menuAccessKeysShowWithTheKeyboard`）：单按 Alt 后第一个菜单高亮、下划线显示；右键移到第二个、下键打开它；Esc 退出后焦点回到原内容；Alt 与其他键组合时不进入。
  - 做完在 [变更记录](../../../CHANGELOG.md) 写一条；验证日志的新记录里说明补上了 [访问键下划线记录](../../development/verification-log/2026-11.md#quick-菜单的访问键下划线照-windows-规则显示2026-11-06) 末尾提到的差别，旧记录不改。
  - 已实现单按 Alt、可见根与收纳入口循环、展开与访问键、Esc／Alt／点击／失去激活退出及原焦点返回。录键控件不进入菜单导航，Alt 候选期间临时观察打断输入，菜单打开后仍复用原导航。定向结果及整套菜单检查的超时记录见 [记录](../../development/verification-log/2026-11.md#quick-菜单栏单按-alt-导航2026-11-06)。
  - 与计划的差别：菜单栏行为用例归入 `quick-menu-bar`，弹出树与上下文菜单保留在 `quick-popup-menu`。原文件超过测试规范建议规模，合并运行也触及套件总时限；按职责拆开后沿用相同断言和各自原有的时限，未减少行为覆盖。

## 10.5 帧成本与资源调优

**状态**：整体调优未开始；各前置步骤发现的局部成本当步处理。分隔条尺寸更新与实际比例提交的定向对照已完成，见 [测量记录](../../development/verification-log/2026-10.md#分隔条比例提交路径的成本2026-10-28)。多标签的无效接合轮廓计算已减少，单标签组分隔条卡顿仍待后续迁移，见 [取样与分隔条对照](../../development/verification-log/2026-10.md#标签接合轮廓只为使用者生成2026-10-28)。Quick 基准已加入 30 标签整栏场景，三轮 p95 为 7.43–8.01 ms，仍高于 144 Hz 单周期预算，见 [整栏基准](../../development/verification-log/2026-10.md#quick-三十标签整栏基准2026-10-29)；该场景不含宿主内容和输入，不能代表完整应用。

- **工具（执行者）**：`tools/tests/quiet-desktop.ps1` 在测试会话期间唤醒屏幕并保持亮着，结束（包括失败）时撤销；做法和原因见 [Qt Quick 经验](../../development/lessons/qt-quick.md#测量帧率前先确认屏幕处于工作状态)。改动遵守 [测试要求 §3](../../development/testing-design.md#3-工具与统一入口)，同时更新 `quiet-desktop-test.ps1`。
  - 已完成：按会话显示通知决定是否唤醒，亮屏请求随测试会话释放；无窗口与原生自检通过，见 [记录](../../development/verification-log/2026-11.md#桌面检查自动保持亮屏2026-11-04)。
- 对照基线测量分隔条、标签拖动/切换、多行展开、侧栏展开、拖动中的系统移动及多窗口（执行者用 `tools/bench.ps1` 测，数字写进验证日志；优化方案由 Claude 定）。
- 2026-11-05 审查记下的两处（[记录](../../development/verification-log/2026-11.md#审查-codex-的-103-子步-6-至-104e-提交2026-11-05)）：每个窗口创建时实例化整棵应用菜单（约 65 个菜单弹窗、1750 个 Quick 项），改为子菜单第一次打开时再建；Debug 下单窗口一次标签切换的工作区同步约 5 毫秒，大部分在标签组同步，按基线测 Release 并定位。
- 优化节点保留、局部更新、批次、文字/阴影缓存和上传；在目标刷新周期内控制 CPU、GPU 与同步成本，不停用视觉效果换取成绩。拖到标签栏上时整窗截图（`GroupItem::pagePicture`）的代价也在这里测。
- 检查高刷新、多屏混合刷新/DPI、隐藏恢复及反复关闭后的资源回落，性能结论按实际运行记录。混合 DPI 要两块缩放不同的显示器，没有条件时如实记录。
- 偶发问题：`quick-native-drag` 在整批运行中偶尔超时于“system loop again”（离开标签栏后没能回到系统移动），见 [验证日志](../../development/verification-log/2026-11.md#quick-展开面板的尺寸手柄和标题拖动2026-11-04)。执行者再遇到时先查 `TabDrag::queueSystemMove` 到 `enterSystemMove` 之间的条件。

## 10.6 分块删除 Widgets 与收尾

**状态**：进行中。Widgets 不再是默认，按下面的单元分块删除，不等到最后一次删光；删除前确认 Quick 一侧已有等价行为和用例，缺的用例补到 `quick-*` 套件（按 [测试的分量](../../development/testing-design.md#测试的分量)，不逐条照搬）。

删除单元 1 已完成：Muxora 只保留 Quick 占位页，本地化套件检查同一份编译后的 QML 和文字函数，见 [验证记录](../../development/verification-log/2026-11.md#删除-muxora-的-widgets-占位内容2026-11-04)。下一步为 10.3 子步 6。

| 单元 | 删除内容 | 时机 |
| --- | --- | --- |
| 1 | Muxora 的 Widgets 占位内容：`createPlaceholder` 和它的控件代码、`addPlaceholderTab`／`populateInitialWorkspace` 里的 Widgets 分支；`src/app/tests/localization_test.cpp` 改为检查文字函数（`notesText()` 等）和 QML 占位页，保留现有译文断言 | 现在（执行者） |
| 2 | 10.4 各项替换掉的 Widgets 代码：`commands/popup_menu.cpp` 和 `command_menus.cpp` 的 `QMenu` 部分、`chrome/status_bar.cpp`、`chrome/command_strip.cpp`、`floating_panel.cpp`、`commands/command_dialogs.cpp`、`settings/*`、`peek_size_command.cpp` 和各命令里 Widgets 分支的对话框与 `QMenu` 选择器、`commands/dialogs.cpp` 的 `WidgetDialogs`，以及只测它们的 Widgets 套件 | 并入单元 3：管理器的 Widgets 呈现还在用这些代码，删掉 Widgets 呈现时一起删（2026-11-06 改） |
| 3 | 管理器去掉 Widgets 呈现：删除 `Presentation` 里的 Widgets 和 `refresh()` 的建视图部分；公开接口去掉 `QWidget`（`addTab(QWidget*)`、`addTabToNewGroup(QWidget*)`、`splitPane(…, QWidget*, …)`、`setTab*Bar(QWidget*)`／`setPane*Bar(QWidget*)`、`windowWidget`、`paneWidget`、`QWidget` 内容工厂）；`WindowManager` 移出 `WindowSystemWidgets`，只依赖 Qt Gui。Lua 模块（`window-system-lua/src/window_api.cpp`）和 Muxora 跟着改 | 10.4 完成后，Claude 先拆成可单独提交的步骤 |
| 4 | Widgets 停靠区与窗口：`docking/dock_surface`、`tab_strip`、`tab_bar`、`edge_strip`、`edge_tab_button`、`drag_controller`、`drag_presentation`、`panes/tab_content`、`chrome/workspace_window`、`workspace_view`、`style` 里只给控件用的部分（`themed_controls`、`integer_slider`、`wheel_guard`、`smooth_scroll`、`tooltip_layout`、`applyTheme`）、`input_method_scopes_widgets.cpp`，以及剩下的 Widgets 套件 | 单元 3 之后 |
| 5 | 构建与部署：模块和应用不再链接 Qt Widgets（应用改用 `QGuiApplication`），`deployment-files.json` 和 [第三方声明](../../../THIRD_PARTY_NOTICES.md) 去掉 Widgets 的文件和插件 | 单元 4 之后 |

收尾（Claude）：核对 [浮窗设计](../../design/floating-window-design.md)、[窗口交互细则](../../design/window-interaction-design.md) 和标签栏设计里的行为在 Quick 版都有；用户试用；全量测试先取得单独授权；更新设计文档里关于 Widgets 的描述和预览说明。未完成全界面迁移、试用与帧测量时，不宣称整体 GPU 化或达到 GPUI 同级性能。
