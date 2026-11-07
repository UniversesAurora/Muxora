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

**状态**：已完成（2026-11-07）。10.4a–k 都已完成，Claude 审查过执行者的部分，见 [审查记录](../../development/verification-log/2026-11.md#审查-codex-的-104h-至-105-提交2026-11-07)；真实读屏软件的试用并入 10.6 收尾的用户试用。各项的记录在下面的条目里。外观照 Widgets 参考版（`out/package/muxora-widgets-reference/`）和 [界面设计规范](../../design/ui-design-system.md)，不借此重新设计；[8.27 菜单与浮动面板重做](phase-8-ui-quality.md#827-菜单与浮动面板重做) 的新要求仍等用户细化，不在本步做。

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
  - 命令面板和窗口选择器已在 Quick 里实现：一个共用面板（无边框窗口居中于所属窗口，搜索框、带表头的列表、提示和两个按钮），行内容和筛选规则与 Widgets 共用 `panel_rows`，见 [记录](../../development/verification-log/2026-11.md#quick-命令面板与窗口选择器2026-11-05)。窗口选择器行前的窗口图标 2026-11-07 已补上，见 [记录](../../development/verification-log/2026-11.md#quick-窗口选择器的窗口图标2026-11-07)。拖动中按 Tab 选目标窗口也已接入，见 [记录](../../development/verification-log/2026-11.md#quick-拖动中按-tab-选目标窗口2026-11-05)；10.4g 完成，Widgets 的面板和 `DragController::selectTarget` 随 10.6 删除。
- **10.4h 设置页与快捷键页（Claude 后执行者）**：Claude 先做 Quick 表单控件（开关、下拉、数值、滑块、列表、快捷键录入，类型选择按 [设置设计 §3](../../design/settings-and-commands-design.md#3-全局设置与编辑)）和第一个分区的样板；执行者照样板迁移其余分区和快捷键页，遇到样板里没有的控件停下。
  - Claude 部分已完成：设置项改为不依赖界面的 `SettingDescription`，Quick 设置页按描述生成，应用、界面、窗口行为和拖动分类已迁移；表单控件含复选框、下拉框、数字框、输入框、滑条加数字框、按键录入框、列表、按钮和多行输入框，外观由 `SettingsStyle` 统一提供；按键录入规则与 Widgets 共用，录入时不触发快捷键，见 [记录](../../development/verification-log/2026-11.md#quick-设置页与表单控件2026-11-06)。标签配色用新的 JSON 类型（多行文本、提示和应用按钮）放在“标签外观”分类，见 [配色记录](../../development/verification-log/2026-11.md#quick-设置页的标签配色编辑2026-11-06)。
  - 显示、标签外观和置顶标签分类已迁移为描述，控件沿用既有类型，标签外观排在配色前。栏显隐按设置页当前所属窗口读写，跨窗口移动保留页面与控件；标题裁切、多行及置顶尺寸的依赖保持原行为，见 [记录](../../development/verification-log/2026-11.md#quick-设置页补齐显示与标签分类2026-11-06)。
  - 快捷键页已迁移为 Quick，复用既有控件和共用校验，支持搜索、录键、参数、增删绑定和外部修改时保留未应用编辑。10.4h 完成，见 [记录](../../development/verification-log/2026-11.md#quick-快捷键页2026-11-06)。Widgets 设置页和快捷键页随 10.6 删除。
  - 快捷键页的搜索、参数 JSON 与 `runCommands` 校验、冲突与前缀提示、外部绑定修改检查已从 Widgets 页移到 `settings/shortcut_edit`，旧页调用共用规则，原套件通过。Quick 页也已接入，见 [记录](../../development/verification-log/2026-11.md#快捷键编辑规则脱离控件2026-11-06)。
- **10.4i 对话框（Claude）**：重命名标签／组／标签夹（现用 `QInputDialog`）、侧栏面板尺寸（`peek_size_command.cpp`）、关于、Lua 的输入框（`src/app/scripting_setup.cpp`）、Lua 父窗口查询（`src/app/script_services.cpp`）。宿主的关闭确认由宿主提供，Muxora 的那个也一起换。
  - 已完成：管理器的 `openDialog` 让对话框不依赖呈现、不阻塞调用方，Quick 下是带原生标题栏的独立窗口；重命名标签／组／标签夹、侧栏面板尺寸、关于、Lua 输入框和 Lua 通知已改用它，见 [记录](../../development/verification-log/2026-11.md#quick-对话框2026-11-06)。Muxora 没有设置关闭确认，无需替换。不带参数时仍弹 Widgets 菜单的选择命令（标签和标签夹颜色、选目标标签组／窗口／标签／标签夹）也改为在 Quick 菜单里列出选择，见 [选择菜单记录](../../development/verification-log/2026-11.md#quick-下的选择菜单2026-11-06)。Widgets 呈现下的旧对话框和菜单随 10.6 删除。
- **10.4j 无障碍（执行者）**：Quick 的标签栏、标签组、侧栏按钮、按钮和标题行提供无障碍角色、名称和状态，照 Widgets 的 `TabBarAccessible`；键盘焦点顺序与 Widgets 一致。
  - 已接入并定向验证：按钮使用 Qt 公开的 `Accessible`，见 [按钮记录](../../development/verification-log/2026-11.md#quick-按钮的读屏信息与动作2026-11-06)；侧栏、标签组、标题和键盘焦点见 [接线记录](../../development/verification-log/2026-11.md#quick-侧栏标签组与标题的读屏接线2026-11-06)。标签栏按稳定身份提供标签、标签夹和单选接口，动作走原管理器路径，资源随移除和销毁释放，见 [标签记录](../../development/verification-log/2026-11.md#quick-标签与标签夹的读屏身份2026-11-06)。真实读屏软件试用待完成。
- **10.4k 菜单栏单按 Alt 的键盘导航（执行者，用户 2026-11-06 要求）**：Widgets 的 QMenuBar 在 Windows 上单按一下 Alt（按下到松开之间没有别的键、没有鼠标操作）进入键盘导航，Quick 菜单栏还没有。照 Qt 源码 `qmenubar.cpp`（`QMenuBar::eventFilter` 的 `altPressed`、`QMenuBarPrivate::setKeyboardMode`、`keyPressEvent`）实现，外观复用现有的悬停底色，不做新样式：
  - 进入：Alt 松开时进入键盘导航，高亮第一个可见菜单，记下原来的焦点项，菜单栏取得键盘焦点；访问键下划线显示（`PopupMenu::setMnemonicsShown`，现在只跟 Alt 的按下松开和 Alt 打开的菜单，见 `WorkspaceQuickWindow::updateMnemonics`）。按 Alt 期间按了别的键、点了鼠标、窗口失去激活，都不进入。
  - 导航中：左右键在可见菜单间移动并循环，窄窗口时包括收纳按钮；下键、上键、回车、空格打开高亮的菜单并选中第一项；按某个菜单的访问键字母直接打开它；菜单打开后左右键切换到相邻菜单（现有行为）。
  - 退出：Esc、再按一次 Alt、鼠标点击、窗口失去激活都退出，焦点回到原来的项，下划线收起。
  - 测试加在 `quick-popup-menu`（参考 `menuAccessKeysShowWithTheKeyboard`）：单按 Alt 后第一个菜单高亮、下划线显示；右键移到第二个、下键打开它；Esc 退出后焦点回到原内容；Alt 与其他键组合时不进入。
  - 做完在 [变更记录](../../../CHANGELOG.md) 写一条；验证日志的新记录里说明补上了 [访问键下划线记录](../../development/verification-log/2026-11.md#quick-菜单的访问键下划线照-windows-规则显示2026-11-06) 末尾提到的差别，旧记录不改。
  - 已实现单按 Alt、可见根与收纳入口循环、展开与访问键、Esc／Alt／点击／失去激活退出及原焦点返回。录键控件不进入菜单导航，Alt 候选期间临时观察打断输入，菜单打开后仍复用原导航。定向结果及整套菜单检查的超时记录见 [记录](../../development/verification-log/2026-11.md#quick-菜单栏单按-alt-导航2026-11-06)。
  - 与计划的差别：菜单栏行为用例归入 `quick-menu-bar`，弹出树与上下文菜单保留在 `quick-popup-menu`。原文件超过测试规范建议规模，合并运行也触及套件总时限；按职责拆开后沿用相同断言和各自原有的时限，未减少行为覆盖。

## 10.5 帧成本与资源调优

**状态**：进行中（2026-11-07）。完整 Quick 工作区的首轮测量与阴影归因已完成，见 [记录](../../development/verification-log/2026-11.md#quick-完整窗口帧成本与阴影归因2026-11-06)；多窗口与原生拖动的拆出、截图峰值也已取样，见 [补充记录](../../development/verification-log/2026-11.md#quick-多窗口与真实拖动耗时2026-11-06)。第一项优化已完成：接合阴影的生成改快、只存标签两端，标签切换时左侧标签栏超过两个刷新周期的帧从每轮 14–15 个降到 0，见 [记录](../../development/verification-log/2026-11.md#quick-切换标签时接合阴影的生成成本2026-11-07)。剩下的按下面“后续优化”的顺序做。各前置步骤发现的局部成本当步处理。分隔条尺寸更新与实际比例提交的定向对照已完成，见 [测量记录](../../development/verification-log/2026-10.md#分隔条比例提交路径的成本2026-10-28)。多标签的无效接合轮廓计算已减少，单标签组分隔条卡顿仍待后续迁移，见 [取样与分隔条对照](../../development/verification-log/2026-10.md#标签接合轮廓只为使用者生成2026-10-28)。Quick 基准已加入 30 标签整栏场景，三轮 p95 为 7.43–8.01 ms，仍高于 144 Hz 单周期预算，见 [整栏基准](../../development/verification-log/2026-10.md#quick-三十标签整栏基准2026-10-29)；该场景不含宿主内容和输入，不能代表完整应用。

- **工具（执行者）**：`tools/tests/quiet-desktop.ps1` 在测试会话期间唤醒屏幕并保持亮着，结束（包括失败）时撤销；做法和原因见 [Qt Quick 经验](../../development/lessons/qt-quick.md#测量帧率前先确认屏幕处于工作状态)。改动遵守 [测试要求 §3](../../development/testing-design.md#3-工具与统一入口)，同时更新 `quiet-desktop-test.ps1`。
  - 已完成：按会话显示通知决定是否唤醒，亮屏请求随测试会话释放；无窗口与原生自检通过，见 [记录](../../development/verification-log/2026-11.md#桌面检查自动保持亮屏2026-11-04)。
- 对照基线测量分隔条、标签拖动/切换、多行展开、侧栏展开、拖动中的系统移动及多窗口（执行者用 `tools/bench.ps1` 测，数字写进验证日志；优化方案由 Claude 定）。
- 2026-11-05 审查记下的两处（[记录](../../development/verification-log/2026-11.md#审查-codex-的-103-子步-6-至-104e-提交2026-11-05)）：每个窗口创建时实例化整棵应用菜单（约 65 个菜单弹窗、1750 个 Quick 项），改为子菜单第一次打开时再建；Debug 下单窗口一次标签切换的工作区同步约 5 毫秒，大部分在标签组同步，按基线测 Release 并定位。
- 优化节点保留、局部更新、批次、文字/阴影缓存和上传；在目标刷新周期内控制 CPU、GPU 与同步成本，不停用视觉效果换取成绩。拖到标签栏上时整窗截图（`GroupItem::pagePicture`）的代价也在这里测。
- 检查高刷新、多屏混合刷新/DPI、隐藏恢复及反复关闭后的资源回落，性能结论按实际运行记录。混合 DPI 要两块缩放不同的显示器，没有条件时如实记录。
- **后续优化**（Claude 2026-11-07 定，按顺序；每项用 `window-system-quick-workspace-benchmark` 或在它上面加场景，改动前后各测三轮，外观不变。分工：1–3 和 5 由执行者测量并定位到具体函数，结论写进验证日志，改法由 Claude 定；4 不改外观，执行者直接做）：
  1. 顶部标签栏远距切换每次仍有一帧超过两个刷新周期，阴影已不是主因。**Claude 2026-11-07 已归因，暂缓**：这一帧由几笔开销叠成，主要是两处：主线程生成新选中标签的接合阴影（动画中形状每帧不同，缓存从不命中）；渲染线程为选中和边缘渐隐的标签新建遮罩图层（`MaskedLayer.qml` 只在有轮廓或渐隐时开图层，每次切换都释放旧的、新建 4 倍多重采样的渲染目标）。只去掉其中一处不能消除超时帧。改法是两处渲染改造：遮罩不再给每个标签单独开图层，阴影生成移出主线程或在动画中复用相近形状。只影响 144 Hz 下顶部标签栏切到远处标签时的一帧，排在 2、3、5 之后，由 Claude 做。数字见 [归因记录](../../development/verification-log/2026-11.md#quick-启动和顶部远距切换的耗时归因2026-11-07)。先用基准的 `far top` 场景定位这一帧在做什么（激活、滚动开始、内容切换），再定改法。接手三轮已复测，后两轮仍各有 8 帧超过两个刷新周期，第一轮额外峰值和函数归因待查，见 [基线记录](../../development/verification-log/2026-11.md#顶部远距切换接手基线2026-11-07)。
  2. 多行展开：执行者测得 `anchorPatch` 累计 325 毫秒，第一项优化后重测；若仍是阴影，看多锚点合并（`ElevationRenderer::anchorPatches` 多于一个锚点时每次都重新合成，没有缓存）。
  3. 拖出标签时的整窗截图（`GroupItem::pagePicture`，单次约 48 毫秒）：只截被拖标签所在区域，或复用已有的内容图层，先测再改。
  4. 每个窗口创建时实例化整棵应用菜单（约 65 个菜单弹窗、1750 个 Quick 项），改为子菜单第一次打开时再建。已实现按展开层级创建，为保留 Qt 初次定位和键盘选择，父菜单打开时提前准备直接子菜单；深层未打开时只保留描述。对象量已下降，三轮冷创建耗时未见稳定改善，见 [记录](../../development/verification-log/2026-11.md#quick-菜单按展开层级创建2026-11-07)。Claude 2026-11-07 查明那约 1 秒不在菜单：它是菜单栏第一次量字时 Qt 加载后备字体，界面字体是多字体列表才会这样（[经验](../../development/lessons/windows-platform.md#多字体的界面字体会在第一次量字时加载全部字体族)）。菜单这边没有要做的了；用户 2026-11-07 决定不改默认界面字体，要求用和 VS Code 一样的机制。已按 [ADR 0014](../../adr/0014-prefill-font-fallbacks.md) 预填后备字体表，启动快约 0.8 秒，见 [记录](../../development/verification-log/2026-11.md#windows-界面字体的后备字体表预填2026-11-07)。
  5. 两窗同时更新时主窗口的提交节奏（p95 约 19 毫秒）：在 1–4 之后重测，再看是否需要单独处理。
- 偶发问题：`quick-native-drag` 在整批运行中偶尔超时于“system loop again”（离开标签栏后没能回到系统移动），见 [验证日志](../../development/verification-log/2026-11.md#quick-展开面板的尺寸手柄和标题拖动2026-11-04)。执行者再遇到时先查 `TabDrag::queueSystemMove` 到 `enterSystemMove` 之间的条件。

## 10.6 分块删除 Widgets 与收尾

**状态**：进行中。Widgets 不再是默认，按下面的单元分块删除，不等到最后一次删光；删除前确认 Quick 一侧已有等价行为和用例，缺的用例补到 `quick-*` 套件（按 [测试的分量](../../development/testing-design.md#测试的分量)，不逐条照搬）。

删除单元 1 已完成：Muxora 只保留 Quick 占位页，本地化套件检查同一份编译后的 QML 和文字函数，见 [验证记录](../../development/verification-log/2026-11.md#删除-muxora-的-widgets-占位内容2026-11-04)。单元 3 已由 Claude 拆成下面“单元 3 的步骤”（2026-11-07），单元 2 在其中的 3.5 执行。

| 单元 | 删除内容 | 时机 |
| --- | --- | --- |
| 1 | Muxora 的 Widgets 占位内容：`createPlaceholder` 和它的控件代码、`addPlaceholderTab`／`populateInitialWorkspace` 里的 Widgets 分支；`src/app/tests/localization_test.cpp` 改为检查文字函数（`notesText()` 等）和 QML 占位页，保留现有译文断言 | 现在（执行者） |
| 2 | 10.4 各项替换掉的 Widgets 代码：`commands/popup_menu.cpp` 和 `command_menus.cpp` 的 `QMenu` 部分、`chrome/status_bar.cpp`、`chrome/command_strip.cpp`、`floating_panel.cpp`、`commands/command_dialogs.cpp`、`settings/*`、`peek_size_command.cpp` 和各命令里 Widgets 分支的对话框与 `QMenu` 选择器、`commands/dialogs.cpp` 的 `WidgetDialogs`，以及只测它们的 Widgets 套件 | 并入单元 3：管理器的 Widgets 呈现还在用这些代码，删掉 Widgets 呈现时一起删（2026-11-06 改） |
| 3 | 管理器去掉 Widgets 呈现：删除 `Presentation` 里的 Widgets 和 `refresh()` 的建视图部分；公开接口去掉 `QWidget`（`addTab(QWidget*)`、`addTabToNewGroup(QWidget*)`、`splitPane(…, QWidget*, …)`、`setTab*Bar(QWidget*)`／`setPane*Bar(QWidget*)`、`windowWidget`、`paneWidget`、`QWidget` 内容工厂）；`WindowManager` 移出 `WindowSystemWidgets`，只依赖 Qt Gui。Lua 模块（`window-system-lua/src/window_api.cpp`）和 Muxora 跟着改 | 按下面“单元 3 的步骤”（2026-11-07 拆） |
| 4 | Widgets 停靠区与窗口：`docking/dock_surface`、`tab_strip`、`tab_bar`、`edge_strip`、`edge_tab_button`、`drag_controller`、`drag_presentation`、`panes/tab_content`、`chrome/workspace_window`、`workspace_view`、`style` 里只给控件用的部分（`themed_controls`、`integer_slider`、`wheel_guard`、`smooth_scroll`、`tooltip_layout`、`applyTheme`）、`input_method_scopes_widgets.cpp`，以及剩下的 Widgets 套件 | 单元 3 之后 |
| 5 | 构建与部署：模块和应用不再链接 Qt Widgets（应用改用 `QGuiApplication`），`deployment-files.json` 和 [第三方声明](../../../THIRD_PARTY_NOTICES.md) 去掉 Widgets 的文件和插件 | 单元 4 之后 |

### 单元 3 的步骤（Claude 2026-11-07 拆）

现状：管理器的 Widgets 呈现只剩测试在用。2026-11-07 时 57 个测试源码（原记 58，数错了）（`rg -l "Presentation::Widgets|WindowManager manager;" modules/window-system/tests modules/window-system-lua/tests src/app/tests`；默认构造就是 Widgets 呈现）还建 Widgets 窗口，其中很多测的是管理器行为，不是 Widgets 外观。所以先迁测试，再删代码；每步完成时构建和相关套件保持通过。

| 步骤 | 内容 | 负责 | 完成标准 |
| --- | --- | --- | --- |
| 3.1 | Lua 的 `tabs.set_bar`／`panes.set_bar` 改用与呈现无关的文字栏：管理器加文字栏描述（文字、提示），Quick 工作区画成一条文字栏，外观照 Widgets 的 `QLabel`（左右各 6 像素边距、界面字体和文字颜色）。Quick 下这两个接口现在不显示（见 [记录](../../development/verification-log/2026-11.md#lua-的窗口位置尺寸和状态在-quick-下失效2026-11-07)） | Claude（新界面元素） | Quick 下 Lua 设置、替换、清除两种栏，有 `quick-*` 用例。**已完成**（2026-11-07），见 [记录](../../development/verification-log/2026-11.md#lua-文字栏在-quick-下显示2026-11-07) |
| 3.2 | 测试清单：上面 57 个源码逐个用例分三类，写成表格放进本节（每行：源码、用例、类别、去向）。A：测管理器或模型行为，不依赖 Widgets 控件，换成 `Presentation::External`（要窗口时加 `QuickWorkspace`）就能测；B：测 Widgets 控件本身，Quick 已有等价用例（写出是哪个 `quick-*` 用例），随单元 4 删除；C：行为 Quick 还没有用例，要补到 `quick-*` 套件。只写表，不改代码 | 执行者 | 表格完整，每个用例都有类别；拿不准的标 C。Claude 审过表再做 3.3 |
| 3.3 | 按表把 A 类迁到 External：一个源码一个提交，断言不改，只换建窗口的方式和取控件的方式（例如 `windowWidget` 换成 `QuickWorkspace::window`）。改不动的（断言依赖 Widgets 控件）改标 C，写明原因 | 执行者 | 迁过的套件在 External 下通过；不删断言、不放宽时限 |
| 3.4 | C 类补到对应 `quick-*` 套件，按 [测试的分量](../../development/testing-design.md#测试的分量) 合并成行为用例，不逐条照搬。Quick 行为和 Widgets 不一致的，先写进交接记录问 Claude，不改预期迁就 | 执行者 | 每个 C 类都有 Quick 用例或写明的放弃理由 |
| 3.5 | 删除单元 2 的内容：命令里的 Widgets 分支（`tab_group_commands`、`tab_folder_commands`、`window_commands`、`peek_size_command`、`command_dialogs` 里 `presentation == External` 之外的分支）、`WidgetDialogs`、`QMenu` 选择器、Widgets 设置页和快捷键页、`floating_panel`、`chrome/status_bar`、`chrome/command_strip`，以及表里只测它们的 B 类用例 | 执行者 | 全模块构建通过，剩下的 Widgets 呈现只在管理器内部 |
| 3.6 | 管理器只留外部呈现：删除 `Presentation` 枚举和构造参数、`refresh()` 的建视图部分、`ensureTabView`、`storage`、`TabContent`／`WorkspaceWindow`／`DragController` 的使用、`QApplication::focusChanged` 监听、`windowWidget`／`paneWidget`、`QWidget` 内容工厂和带 `QWidget*` 的重载（`addTab`、`addTabToNewGroup`、`splitPane`、`setTab*Bar`／`setPane*Bar`、拆出时的 `beforeShow`）、`createTabCreationMenu`。`layout_persistence`、`view_state`、`custom_chrome`、`hierarchy_api` 里的 Widgets 分支一起删。Lua 和 Muxora 跟着改 | 执行者 | 公开头文件不出现 `QWidget`；所有剩余套件通过 |
| 3.7 | 库拆分：与呈现无关的源码（管理器、命令、设置描述、工具栏描述、布局保存、主题、本地化等）移到只依赖 Qt Gui 的新库，`WindowSystemQuick` 和 Lua 模块链接它，不再链接 `WindowSystemWidgets`；`WindowSystemWidgets` 只剩单元 4 要删的控件 | 执行者 | Quick 套件和 Muxora 不链接 Qt Widgets 之外的 Widgets 库；构建通过 |

每步完成都更新本节状态、验证日志和预览说明；3.2 的表审完之前不删任何用例。遇到新界面或新外观停下交给 Claude。

#### 3.2 测试清单

**状态**：已整理，Claude 2026-11-07 审定，结论见下面“审定结论”，3.3 可以开始。基于 `86636a0` 的源码，指定检索实际匹配 57 个文件，计划拆步提交 `22cab07` 的结果也是 57。下表覆盖其中 56 个 QtTest 源码的 777 个主用例，以及一个子进程辅助入口。`_data` 的全部数据行随主用例归类，`init`、`cleanup`、`initTestCase` 和 `cleanupTestCase` 随所属夹具保留。

源码缩写 `WS/` 指 `modules/window-system/tests/`，`Lua/` 指 `modules/window-system-lua/tests/`，`App/` 指 `src/app/tests/`。B 类写出已阅读实际断言的 Quick 用例。C 类包含 Quick 覆盖缺口、旧控件内部结构依赖和需 Claude 审定的外观混合断言，去向是补充或审定位置。A 类在 3.3 实际迁移时仍按原断言验证，需要改变行为断言的转为 C。

本表共 778 行，A 233、B 3、C 542，包含子进程入口。A 类已按 3.3 完成迁移或确认无需呈现迁移，证据见[迁移进度](#33-迁移进度)；C 类按 3.4 分功能整理。

##### 审定结论（Claude 2026-11-07）

分类整体批准，作为 3.3 和 3.4 的依据。3 条 B 类照列，随所测的 Widgets 代码在 3.5 或单元 4 删除。

**3.3 可以开始，范围是全部 A 类**，条件：

1. 一个源码一个提交，只换建窗口和取控件的方式，断言不改，时限不放宽。
2. 先做整份都是 A 的 `WS/visibility_profile_test.cpp`、`App/layout_file_test.cpp`、`App/scripting_test.cpp`（连同 `scripting_exit_child.cpp`），再做 A 多的 `Lua/script_host_test.cpp`、`WS/workspace_test.cpp`、`WS/view_state_test.cpp`、`WS/shortcut_dispatch_test.cpp`、`WS/tab_group_test.cpp`，其余按表的顺序。
3. 同一源码里 A、C 都有时，这一步只迁 A 用例，C 用例保持原样，留到 3.4。
4. 迁不动的改标 C，把原因写进表里。不在产品代码里为迁移加特殊分支；迁移暴露出产品缺陷（例如又遇到只读 Widgets 视图的代码）时停下，写进交接记录交给 Claude。
5. 迁移后套件仍在 60 秒上限内，超出就按职责拆分。

**因方向导航只读 Widgets 几何而标 C 的行**：`d14f781` 已改成向呈现取几何（见 [记录](../../development/verification-log/2026-11.md#方向导航和窗口状态命令在-quick-下失效2026-11-07)）。`tab_group_test` 的 `directionalGroupExchangeWrapsWithinTheVisibleGroupLayer`、`directionalMergeIncludesPinnedGroupsAndAppendsInStableOrder`、`wholeGroupExchangeIncludesPinnedNeighbors`，以及 `movement_test` 的 `directionalGroupTransfersWrapByVisibleGeometry`、`groupFocusWrapsWithoutEnteringHiddenRail`、`paneSwapAndFocusWrapWithinCurrentTab`、`tLayoutNavigationRetainsOrthogonalAnchor`，在 3.3 按 A 试迁，几何由 Quick 窗口的实际布局给出。手工设置 `QWidget` 几何的 `directionalTiesFollowStableModelOrder` 和 `splitterGapSelectsNearestProjection` 仍是 C。

**3.4 处理 C 类的规则**，执行者逐行套用，表里写着“请 Claude 审定”的部分也按这里处理，不再逐条交回：

1. 静态外观断言直接退役，不找替代：尺寸、间距、写死的数值、像素、颜色、边框、悬停图像、计时器名称和间隔。依据是 [测试要求 §1](../../development/testing-design.md#1-必须遵守的规则) 和 [退役与重写](../../development/testing-design.md#退役与重写) 第 4 条；混合用例只保留功能断言。
2. 下面这些不算外观，必须保留：文字显示完整（标题、读屏名称）、能点到和命中（屏幕最后一个物理像素、缩放边角）、内容身份和焦点、模型和保存结果、按键不漏进编辑器、生命周期和重入销毁。
3. 只测 Widgets 机制本身的（`QWidget` 接管属性、宿主样式表、原生子窗口、`QSplitter`、Widgets 容器的恢复），按退役条件 1 随 Widgets 删除。背后有用户能看到的错误时（例如状态栏空白、Snap 失效、内容被释放两次），在 Quick 的对应入口补一条行为用例，不照搬机制。
4. 同一功能的多条 C 合成一两个数据驱动的行为用例（[测试的分量](../../development/testing-design.md#测试的分量)），一个功能一个提交，验证日志列出“原用例 → 新用例或退役依据”。建议顺序：内容与生命周期（`workspace`、`tab_group`、`view_reuse`、`memory_lifecycle`），快捷键，标签栏（`tab_strip_layout`、`tab_viewport`、`sticky_tabs`、`tab_bar_presentation`、`tab_carrier_source`），拖动（`drag_*`、`strip_drag`、`edge_*`），工具栏与标题（`chrome_interaction`、`multirow_toolbar`、`toolbar_*`、`caption_strip`、`native_frame`、`compact_ui`），设置页（`pane_settings`、`shortcut_settings`、`peek_size`），其余。
5. Quick 根本没有这个行为（不只是缺用例），或者行为和 Widgets 不同的：不实现，不改预期去迁就，写进交接记录的“Quick 缺的行为”交给 Claude，因为可能涉及界面。

##### 窗口系统

| 源码 | 用例 | 类别 | 去向与依据 |
| --- | --- | --- | --- |
| `WS/caption_strip_test.cpp` | `captionButtonOrderFollowsTheBehavior` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少三种标题按钮顺序的呈现、完整序列、保存恢复、非法枚举和遗漏字段默认值的整组覆盖。包含静态填满关系，请 Claude 逐断言审定。 |
| `WS/caption_strip_test.cpp` | `captionButtonsRetainWindowAndCloseGuardBehavior` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。Quick 已测默认标题栏最大化与关闭，但未等价覆盖移至第二行后的最小化、还原角色和中文读屏名称、关闭守卫拒绝后再次接受。 |
| `WS/caption_strip_test.cpp` | `captionControlsNeverTakeExtraWidth` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少相邻栏移走后标题按钮固定宽度不吸收空位、旧保存宽度被归一化且模型有效的覆盖。是否属于纯外观的部分由 Claude 审定。 |
| `WS/caption_strip_test.cpp` | `captionHitPriorityPreservesResizeEdgesAndCorners` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少真实 Quick 窗口八方向缩放命中、最大化后均为客户区、恢复后重新生效的覆盖。 |
| `WS/caption_strip_test.cpp` | `captionOrderChangesKeepAllButtonsWhileMaximized` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少最大化态多次切换按钮顺序、还原角色变化后仍有且仅有三个可见可用按钮的覆盖。相邻和固定尺寸断言先请 Claude 审定。 |
| `WS/caption_strip_test.cpp` | `centeredTitleKeepsDoubleClickAndContentIdentity` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。Quick 默认标题按钮测试只点击最大化按钮，未测两种窗口标题区域双击最大化/还原、拖动区域识别和内容身份保持。 |
| `WS/caption_strip_test.cpp` | `clientEdgesBelongToContent` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少 Quick 原生客户区左右下边归内容、外侧隐形边框归缩放及顶边角命中优先级的覆盖，不能按静态对齐退役。 |
| `WS/caption_strip_test.cpp` | `compressedMenuStripMovedUpKeepsTheWindowAlien` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。旧用例防菜单移回顶栏、反复悬停重开溢出子菜单后意外创建原生子窗口，造成状态栏空白/失去悬停和 Snap。Quick 没有同流程覆盖，像素断言交 Claude 审定。 |
| `WS/caption_strip_test.cpp` | `compressedTitleKeepsItsFullCaption` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。Quick 无窗口标题受压后仍保留完整标题和无障碍名称、恢复宽度后仍完整的对等用例。省略显示检查属于文字可读性回归，不能仅按外观删去。 |
| `WS/caption_strip_test.cpp` | `controlsCanGrowAndMoveButNeverCompressOrBecomeElastic` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少标题按钮栏的固定宽度策略、禁弹性/禁溢出、移行及最窄窗口仍可点的组合覆盖。原用例还含 126/42×32 等静态尺寸，先列 C 请 Claude 审定这些断言。 |
| `WS/caption_strip_test.cpp` | `disabledHiddenCompactAndFullscreenCaptionsRejectNativeSnap` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。Quick 独立端点仅测禁用，未覆盖实际标题按钮隐藏、全屏、紧凑拖动预览抑制 Snap/悬停并在恢复后重新启用。 |
| `WS/caption_strip_test.cpp` | `expandedCaptionControlsReachEveryScreenCorner` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少工作台/浮窗、最大化/全屏、上下左右角、三种按钮顺序下最后物理角像素能关闭并受守卫保护的覆盖。 |
| `WS/caption_strip_test.cpp` | `fixedResizeAxesRemainRespected` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少 Quick 窗口固定宽度/固定两轴后原生边角退化为 HTBORDER/HTTOP/HTCLIENT 的覆盖。 |
| `WS/caption_strip_test.cpp` | `floatingMenuRestoresBesideTheTitleWithoutMovingTheWorkbenchMenu` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少工作台/浮窗各自上下多行菜单布局、隐藏菜单项、反复切换不重复菜单且保持内容身份的组合覆盖。 |
| `WS/caption_strip_test.cpp` | `incompleteLayoutsCannotIntroduceOrRemoveCaptionControls` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。直接验证 Widgets 容器恢复的原子性及标题按钮强制约束，Quick 现有套件未测缺按钮/不完整布局不能增删标题按钮。 |
| `WS/caption_strip_test.cpp` | `lightweightCustomizationPreservesTheCompleteChromeCache` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少两种窗口类型来回切换并保存/恢复各自完整工具栏缓存、菜单/状态栏归属和只重建一次内容的覆盖。 |
| `WS/caption_strip_test.cpp` | `maximizeEntireRectangleRetainsNativeSnapHitAndQtHover` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。Quick 原生端点用例未覆盖工作台/浮窗、正常/最大化、三种按钮顺序及按钮首中末行的实际 HTMAXBUTTON 与悬停。图像悬停断言先请 Claude 审定。 |
| `WS/caption_strip_test.cpp` | `maximizedCaptionButtonsShowThemedToolTips` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。Quick 悬停提示用例未测最大化后实际标题按钮的客户区/HTMAXBUTTON 分工和相邻提示文本切换。主题外观部分请 Claude 审定。 |
| `WS/caption_strip_test.cpp` | `maximizedCaptionControlsOwnTheActualScreenEdge` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少两种窗口、初始已填工作区与普通初始尺寸、各标题按钮在工作区最后物理像素上的输入、捕获、关闭守卫和内容身份覆盖。 |
| `WS/caption_strip_test.cpp` | `movedMaximizeButtonRetainsNativeHitTesting` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少标题按钮移至第二行后以实际按钮坐标触发 HTMAXBUTTON、按压/释放和最大化恢复的集成覆盖，Quick 原生端点用例使用独立模拟端点。 |
| `WS/caption_strip_test.cpp` | `reusedToolTipsKeepTextAndHostStyles` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。旧用例混合宿主样式表不被覆盖、主题块不重复和换邻按钮提示文字更新。Quick 悬停提示用例未覆盖宿主样式表契约，需 Claude 先审定随 Widgets 删除的适用范围。 |
| `WS/caption_strip_test.cpp` | `titleMinimumIncludesTheUnlockedHandleAndCannotBeOverriddenByLayout` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）及 WS/quick_native_frame_test.cpp（quick-native-frame），静态外观断言请 Claude 按测试规范审定。缺少标题最小可用内容宽度与解锁手柄分别计入、非法保存宽度不能压缩标题内容的覆盖。96 等固定数值先请 Claude 审定。 |
| `WS/chrome_interaction_test.cpp` | `contextMenuListsEveryStripAndOmitsUnrelatedWindowKinds` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。Quick toolbarMenusChangeTheLayout 只验证解锁/隐藏一个栏，未检查上下文菜单完整列出所有栏且排除提升/归位/降级等无关窗口命令。 |
| `WS/chrome_interaction_test.cpp` | `customizationUpdatesMenuAndActionGeometryImmediately` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少自定义菜单隐藏根菜单/按钮时即时更新宽度与实际可见性、命令自身隐藏不留下溢出的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `disabledToolbarButtonsHaveNoBorderOrHoverFeedback` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。用例主要检查禁用按钮边框/悬停图像和图标颜色，另有点击不执行的功能断言。先列 C，请 Claude 按静态外观规范审定并保留禁用命令不执行的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `elasticContentDoesNotBecomeAWindowDragTarget` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少普通可交互内容即使弹性也不会成为窗口拖动区、显式标题拖动区在尺寸策略变化后仍有效的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `extremeRequestedWidthsRemainBoundedAndRestorable` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少多栏请求 INT_MAX 宽度后仍在窗口内且保留可用最小范围，移行再恢复仍保存原请求的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `fixedWidthStripRightOfElasticOnlyReorders` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少弹性栏右侧固定宽度栏拖动只重排而不调整相邻宽度/保存请求的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `hiddenActionsDoNotReserveButtonsOrOverflowEntries` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少命令自身 visible 动态变化时首尾按钮显示恢复及不留溢出项的覆盖，Quick 保存 hiddenItems 用例不等价。 |
| `WS/chrome_interaction_test.cpp` | `hiddenStripContentDoesNotReserveWidthOrOverflow` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少自定义内容整体隐藏/恢复时自然宽度、溢出状态与自定义菜单勾选一致的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `insertionRightOfElasticSpansToTheRightNeighbour` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少弹性栏右侧插入时起点跟随指针、到右邻结束、左右邻的宽度/位置保持及 Esc 恢复的覆盖。具体间距请 Claude 审定。 |
| `WS/chrome_interaction_test.cpp` | `lockedEmptyOptionsDisappearAndReturnForOverflow` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少锁定且无溢出时隐藏选项尾钮、不改保存布局，解锁或溢出时恢复，重置后再次隐藏以及晚注册栏继承策略的覆盖。尺寸差断言请 Claude 审定。 |
| `WS/chrome_interaction_test.cpp` | `movingElasticStripClaimsTheTargetRowAndEscRestoresTheOwner` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。Quick handleDragsMoveStrips 只测移行/重排/取消结果，未测源弹性栏临时夺取目标行所有权、Esc 恢复目标所有者及提交后的所有权。 |
| `WS/chrome_interaction_test.cpp` | `movingFollowingStripsRepairsLiveAndEscRestores` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少拖动中即时修复被移走栏的空位、保留后继位置、保存仍为提交态且 Esc 恢复宽度及可见性的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `narrowStripsKeepTheTailAndExposeOverflowActions` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。Quick 已测溢出命令能执行，但未核对极窄栏尾钮仍在可点范围、所有溢出项和扩宽后清空溢出及完整选项菜单结构。 |
| `WS/chrome_interaction_test.cpp` | `narrowWidthResetsYieldOnTheOppositeSide` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少窄窗口重置在弹性前后选择正确一侧让出空间、扩宽后恢复自然宽度且其他栏请求不变的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `oneElasticStripOwnsEachRowAndRestoresOptionalDefaults` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少同一行只有一个弹性所有者、切换所有者、移行后竞争、遗漏 sizing 默认和冲突保存值归一化的完整覆盖。 |
| `WS/chrome_interaction_test.cpp` | `optionsPolicyDoesNotReserveSpaceOrChangeSavedLayout` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少禁用选项尾钮不占内容空间、不改保存布局且标题仍可拖窗口的覆盖。固定12像素与纯布局外观部分请 Claude 审定。 |
| `WS/chrome_interaction_test.cpp` | `outsidePopupDismissalRestoresNativeHover` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少标题/内容/其他按钮/Esc/命令/子菜单多种关闭路径后不误执行命令、释放捕获/鼠标按钮并恢复原生悬停的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `overflowToggleRestoresHoverAndPressState` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少真实原生输入第二次点溢出按钮只关闭一次菜单、释放按压态、随后按钮悬停正常的覆盖，Quick 菜单关闭用例未检查此入口。 |
| `WS/chrome_interaction_test.cpp` | `partialRestoreIgnoresUnavailableStripsButRejectsMalformedKnownRows` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少容器级部分恢复忽略未安装栏，但重复已知栏和非字符串行项原子拒绝的覆盖，不能仅换完整 manager.restoreLayout。 |
| `WS/chrome_interaction_test.cpp` | `removalRightOfElasticKeepsTheStripsBeforeInPlace` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少从弹性栏右侧拖走中间栏时右邻补空、左侧不移动并在提交后保持的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `removingFollowingStripsRepairsTheVacatedBoundary` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少前栏处于压缩/拉伸、后继为空/弹性/普通三种情形下移走中间栏后边界修复，以及恢复保存布局回到原宽度的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `resetBeforeElasticKeepsAnAlreadyCompressedPrefix` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少弹性栏前重置目标时已受压前缀维持当前宽度、右侧保持且原保存请求不丢失的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `resetWidthMenuUsesTheHostCommandCallback` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少选项菜单重置精确调用宿主回调一次且传原栏ID、恢复自然宽度及缺失ID失败的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `resettingHiddenStripsDoesNotChangeVisibleGeometry` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少重置隐藏栏只清其保存宽度、不改变可见栏范围或其他自然宽度优先请求的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `resettingWidthsKeepsTheCorrectAnchorAndOtherRequests` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少无弹性/弹性前后、压缩/拉伸下重置自然宽度保留正确锚点、其他栏请求和唯一 naturalWidthPriority，并可恢复的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `restoringLockedMinimumWidthDoesNotUseTheCurrentUnlockedMinimum` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少目标当前解锁时恢复锁定最小宽度布局，按保存的锁定状态校验并原样恢复的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `rightBlockTracksTheWindowEdgeAndRecoversRequestedWidths` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少尾栏显示/隐藏时右侧栏组随窗口缩放保持锚定、窄窗口后恢复请求宽度且保存布局不变的覆盖。纯间距断言先请 Claude 审定。 |
| `WS/chrome_interaction_test.cpp` | `rightOfElasticStripsResizeAgainstAnAnchoredRightEdge` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少弹性栏右侧拖动边界仅改变目标宽度、右端锚定、前后普通栏按规则移动以及 Esc 恢复的组合覆盖。 |
| `WS/chrome_interaction_test.cpp` | `slidingTheFollowingHandleCompressesThePreviousStrip` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少拖动后一个栏的手柄压缩前栏、前栏进入溢出并保持顺序和点击边界不相互覆盖的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `slidingTheFollowingHandleRestoresPreviouslyCompressedButtons` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少连续拖动边界先压缩再扩展恢复按钮，随后 Esc 取消第三次压缩仍无溢出的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `stripConstraintsProtectContentThroughSizingAndRestore` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少不可压缩/不可弹性栏的 API 拒绝、窄窗口仍可访问、恶意保存值归一化及禁用选项策略保持的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `stripCustomizationsSurviveWindowKindConversions` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少工作台与浮窗反复转换保存各自锁定、行位置、按钮排除和隐藏栏，浮窗编辑不改休眠工作台快照的覆盖。 |
| `WS/chrome_interaction_test.cpp` | `titleFirstFillsTheRowAndIsAWindowDragArea` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少标题改位置后保持弹性、仅标题内容可拖窗口、解锁手柄仍为拖栏入口的覆盖。左对齐/边距等静态断言先请 Claude 审定。 |
| `WS/chrome_interaction_test.cpp` | `toolbarDragImmediatelyMovesThePresentationAndCancelsCleanly` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。Quick 取消拖动仅检查预览存在/消失及保存状态，未等价检查源栏隐藏、预览随指针立即移动、Esc 恢复源栏。 |
| `WS/chrome_interaction_test.cpp` | `visibilityCustomizationAndWidthsSurviveRestore` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。Quick 已测各项独立编辑保存恢复，未测同一布局同时解锁、改宽、隐藏内容、移行后的自定义菜单勾选及立即反向切换。 |
| `WS/chrome_interaction_test.cpp` | `widthResetPrioritySurvivesRestoreAndDragCancellation` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少自然宽度优先标记在拖动取消/恢复后保留，显式宽度或移行时清理、多标记归一化、错误类型原子拒绝的覆盖。 |
| `WS/compact_ui_test.cpp` | `compactPeekAndEdgeBarsKeepRealContent` | C | 拟补 WS/quick_workspace_test.cpp（quick-workspace）。Quick sidebarTabsOpenInAPanelOverTheWindow 已测侧栏面板，但未等价核对四边面板的固定入口及固定后组操作按钮、内容对象保持的完整组合。 |
| `WS/compact_ui_test.cpp` | `elasticSizingIsAdjacentToResetAndUsesTheHostCommand` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少弹性选项在菜单中的顺序、勾选更新、只调用宿主尺寸回调一次，且不在自定义子菜单重复出现的覆盖。 |
| `WS/compact_ui_test.cpp` | `groupActionsInvokeTheirOwnCallbacks` | C | 拟补 WS/quick_workspace_test.cpp（quick-workspace）。缺少固定组取消固定当前/全部两个按钮分别只执行自己的回调、无障碍名称完整且不改活动标签的覆盖，Quick 无障碍用例没有这两个按钮。 |
| `WS/compact_ui_test.cpp` | `groupSeparatorsResizeWithoutReplacingContent` | C | 拟补 WS/quick_split_tree_test.cpp（quick-split-tree）及 WS/quick_workspace_test.cpp（quick-workspace）。Quick dragCommitsModelRatioWithoutReplacingContent 只测独立窗格 SplitTreeItem。缺少工作区组分隔条和嵌套窗格分隔条都通过实际管理器更新原节点比例、窗口缩放后仍保持内容的覆盖。 |
| `WS/compact_ui_test.cpp` | `protectedSizingDoesNotAppearInEitherMenu` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少强制弹性所有者及其同排普通栏都不提供可编辑弹性菜单项的覆盖。 |
| `WS/compact_ui_test.cpp` | `scrollbarOverlaysTheTabsWithoutMovingTheContent` | C | 拟补 WS/quick_workspace_test.cpp（quick-workspace）。缺少滚动条出现/滚动/调整窗口与标签栏显隐后内容仍在正确区域、滚动不改活动标签的覆盖。位置断言防内容遮挡，请 Claude 保留功能含义。 |
| `WS/custom_chrome_test.cpp` | `dynamicDefinitionsRejectInvalidAndReservedIds` | A | 原 custom-chrome 套件改用 External，内容身份和系统窗口标题经 QuickWorkspace 取得。管理器公开注册/删除/宽度/显隐API的非法ID、重复项及越界行请求必须原子拒绝，只需 External 创建窗口。 |
| `WS/custom_chrome_test.cpp` | `lockedOptionsPreferenceUpdatesEveryWindowAndRestoresStrictly` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。含实际所有窗口工具栏宿主读取策略、晚创建窗口继承和错误类型恢复原子性，Quick 当前只测设置页单项编辑，缺所有窗口与恢复接线覆盖。 |
| `WS/custom_chrome_test.cpp` | `registryCanBeDestroyedByTheRegistrationObserver` | A | 原 custom-chrome 套件改用 External，内容身份和系统窗口标题经 QuickWorkspace 取得。纯注册表增删通知同步销毁所有者的生命周期契约，不依赖控件，保留返回值和所有者释放断言。 |
| `WS/custom_chrome_test.cpp` | `registryNotificationsAndRemovalKeepRunningCallbacksAlive` | A | 原 custom-chrome 套件改用 External，内容身份和系统窗口标题经 QuickWorkspace 取得。纯 CommandRegistry 行为，断言通知次数、自移除回调、内置复合命令保护和不存在命令失败，可原样保留。 |
| `WS/custom_chrome_test.cpp` | `removedProviderIdsDoNotAccumulateInSavedProfiles` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少反复注册/改宽/删除不同提供者ID后保存快照不积累旧ID、注册表与实际呈现都为空的资源/持久化覆盖。 |
| `WS/custom_chrome_test.cpp` | `renamingPreservesContentAndTheFactoryDescriptor` | A | 原 custom-chrome 套件改用 External，内容身份和系统窗口标题经 QuickWorkspace 取得。断言重命名保留窗格内容身份、TabOptions 状态、系统窗口标题与保存标题，换 Quick 内容/窗口查询可保留语义。 |
| `WS/custom_chrome_test.cpp` | `statusItemsUpdateInPlaceAndParticipateInVisibilityAndRestore` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少双窗口自定义状态项原位更新、菜单勾选、迟注册提供者沿用已存隐藏状态、恢复及移除释放实际项的覆盖。 |
| `WS/custom_chrome_test.cpp` | `toolbarsSurviveKindChangesAndReleaseEveryPresentation` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少50轮注册、窗口升降级、删除自定义工具栏期间旧按钮销毁、新按钮唯一且对象总量回到基线的覆盖。 |
| `WS/custom_chrome_test.cpp` | `toolbarsUseCommandsAndRetainIndependentWindowLayouts` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。Quick 已测按钮上下文和单项编辑，但缺一次用例中双窗口宽度/显隐/排除项/区域/弹性/锁定相互独立，以及标题和按钮栏禁止弹性的组合覆盖。 |
| `WS/drag_interaction_test.cpp` | `actualTabBarDragMergesAcrossWindows` | C | 补入 quick-workspace-drag。Quick的tabsDragIntoAnotherWindow落在标签栏，原例通过目标内容中心导向合并，尚未确认此落点路径。 |
| `WS/drag_interaction_test.cpp` | `cancelFromAnEmptiedFloatingWindowRestoresItsToolbarPreferences` | C | 补入 quick-workspace-drag。Quick取消拆出尚未验证源浮窗被移除后，两种窗口类型的工具栏布局、侧栏和状态栏偏好与内容身份一并恢复。 |
| `WS/drag_interaction_test.cpp` | `cancelFromMaximizedWindowPreservesItsNormalPlacement` | C | 补入 quick-workspace-drag。Quick拖动用例尚未检查最大化工作台标签及浮窗标题取消后，模型、内容身份、最大化状态和普通窗口位置同时恢复。 |
| `WS/drag_interaction_test.cpp` | `contentBodyDoesNotAcceptAnImplicitMerge` | C | 补入 quick-workspace-drag。未找到Quick用例检查在内容主体非导向位置放下时保持浮窗，且目标组及窗口数量不变。 |
| `WS/drag_interaction_test.cpp` | `draggingThePeekTitleImmediatelyRemovesThePeek` | C | 补入 quick-workspace-drag。Quick的sidebarTabsOpenInAPanelOverTheWindow已有标题拆出及Esc归位，未完整断言实时内容身份、浮窗数量及AutoHide位置恢复。 |
| `WS/drag_interaction_test.cpp` | `edgeGuidesSeparateHiddenAndPinnedDrops` | C | 补入 quick-workspace-drag。Quick的railsTakeAndGiveDraggedTabs测侧栏插入，未测四边各自的隐藏/固定导向分流及避开标签栏。 |
| `WS/drag_interaction_test.cpp` | `escapeCancelsBeforeAnyContentTransfer` | C | 补入 quick-workspace-drag。Quick的aTabDraggedOutBecomesAFloatingWindow只检查返回原窗口，未完整检查原组ID、临时窗口数和鼠标捕获释放。 |
| `WS/drag_interaction_test.cpp` | `floatingTitleDragMovesAllTabsTogether` | C | 补入 quick-workspace-drag。Quick的aFloatingWindowDraggedByItsTitleJoinsAStrip验证标签栏合并，原例经内容中心导向接收整窗，需补该入口。 |
| `WS/drag_interaction_test.cpp` | `foreignClickThroughOverlayDoesNotBlockTabDrop` | C | 补入 quick-workspace-drag。Quick跨窗用例使用进程内普通遮挡窗，未验证另进程透明点击穿透覆盖层下的落点及内容身份。 |
| `WS/drag_interaction_test.cpp` | `foreignOpaqueOverlayBlocksTabDropWithoutLosingContent` | C | 补入 quick-workspace-drag。Quick尚无另进程不透明顶层覆盖窗阻止放下、同时保留原内容与目标组的用例。 |
| `WS/drag_interaction_test.cpp` | `narrowGroupRecognizesDirectionalGuides` | C | 补入 quick-workspace-drag。Quick只验证普通组下方导向，缺窄组左右导向可达与实际分组落点，原两行数据需保留。 |
| `WS/drag_interaction_test.cpp` | `pinnedGroupAcceptsDirectionalDrops` | C | 补入 quick-workspace-drag。Quick尚未完整覆盖固定组四方向分割、中央组不变、内容身份与可见性，以及捕获释放，不能仅用普通中央组导向替代。 |
| `WS/drag_interaction_test.cpp` | `sideBarDragAcrossWindowsKeepsTheOriginalContent` | C | 补入 quick-workspace-drag。Quick尚无左侧栏跨窗到顶部侧栏中间的插入，需检查原组回收、次序和内容身份。 |
| `WS/drag_interaction_test.cpp` | `sideBarDragEscapeRestoresAnEmptiedSourceGroup` | C | 补入 quick-workspace-drag。Quick侧栏取消只检查扁平顺序，缺原组ID、完整模型快照、源内容身份及侧栏按钮恢复。 |
| `WS/drag_interaction_test.cpp` | `sideBarDragReordersAcrossInternalGroups` | C | 补入 quick-workspace-drag。Quick的railsTakeAndGiveDraggedTabs仅左侧栏同窗排序，缺四边跨内部组排序、拆出阶段模型及内容身份检查。 |
| `WS/drag_interaction_test.cpp` | `tabStripDropInsertsAtTheReleasedPosition` | C | 补入 quick-workspace-drag。Quick的tabsDragWithinAndAcrossGroups覆盖向尾端插入，原例验证已拆出标签在首项之前释放后成为首项，缺此边界。 |
| `WS/drag_interaction_test.cpp` | `tearOffIsVisibleBeforeReleaseAndCollapsesTheEmptyGroup` | C | 补入 quick-workspace-drag。Quick有拆空组及可见浮窗场景，尚无同例验证实时内容所属窗口、浮窗继续跟手、Esc恢复原组及载体销毁。 |
| `WS/drag_interaction_test.cpp` | `tearOffUsesTheDefaultLightweightWindow` | C | 补入 quick-workspace-drag。Quick的aTabDraggedOutBecomesAFloatingWindow覆盖浮窗类型，未断言家族归属和原有内容对象身份，需保留这两项。 |
| `WS/drag_interaction_test.cpp` | `wholeWindowDropInsertsAllTabsAtTheReleasedPosition` | C | 补入 quick-workspace-drag。Quick整窗标题拖动只验证目标尾部追加，缺整窗标签块在两项中间按原顺序插入。 |
| `WS/drag_presentation_test.cpp` | `compactPreviewMaskFollowsTheDisplayedRows` | C | 补入 quick-workspace-drag。Quick没有覆盖多行收起/置顶组合下的载体可见标签掩膜及返回窗口呈现后的状态。掩膜/固定行高断言需Claude审定其功能价值。 |
| `WS/drag_presentation_test.cpp` | `directSaveOmitsIntermediateCarrier` | C | 补入 quick-workspace-drag。Quick拖动缺同步layoutChanged中保存的布局不含临时载体，以及取消/提交/API打断三种最终快照原子性。 |
| `WS/drag_presentation_test.cpp` | `displacedCrossKeepsItsTargetAcrossNeighborAndGaps` | C | 补入 quick-workspace-drag。Quick尚未验证位移导向跨越邻组/导向空隙时保持旧目标，离开导向后再改目标的完整四边场景。 |
| `WS/drag_presentation_test.cpp` | `displacedGroupCrossAlignsWithItsEdgePair` | A | 保留 drag-presentation 的纯 dockGuides 算法用例。纯dockGuides计算覆盖负桌面坐标与偏移组的导向避让和确定位置，无呈现依赖，原断言保留。 |
| `WS/drag_presentation_test.cpp` | `edgeGuidesAcceptDropsBesideStackedGroups` | C | 补入 quick-workspace-drag。Quick缺上下叠组旁左右侧导向的固定/隐藏实际放下，以及原内容身份和捕获释放检查。 |
| `WS/drag_presentation_test.cpp` | `edgeGuidesStayInTheirOwnSideBand` | A | 保留 drag-presentation 的纯 dockGuides 算法用例。只检查dockGuides在左右侧带、侧栏有无时的合法命中范围和避让，无窗口或控件。 |
| `WS/drag_presentation_test.cpp` | `edgeTargetMirrorsTheLabelAndClearsOnCancel` | C | 补入 quick-workspace-drag。Quick仅左侧栏检查载体隐藏，缺四边转场的键盘焦点、内容身份、保存快照及取消清理。像素相等、字体和固定尺寸部分需Claude审定。 |
| `WS/drag_presentation_test.cpp` | `gapDisplacesOnlyTheVisualTabs` | C | 补入 quick-workspace-drag。Quick真实拖动检查插入顺序，尚未独立检查预览仅移动显示矩形、不改变标签数量/当前项/ID，且预览前后插入索引稳定。 |
| `WS/drag_presentation_test.cpp` | `guidesAvoidVisibleStripsAndShortGroupHeaders` | A | 保留 drag-presentation 的纯 dockGuides 算法用例。只调用dockGuides并检查导向范围、标签栏避让及互不遮挡，无呈现依赖，保留原断言。 |
| `WS/drag_presentation_test.cpp` | `guidesTrackWindowGeometryWithoutMouseMovement` | C | 补入 quick-workspace-drag。Quick尚未检查指针不动而目标移动/缩放/最大化/还原后，导向跟随新坐标并按新位置落下。混合像素绘制断言需Claude按规范审定。 |
| `WS/drag_presentation_test.cpp` | `hiddenCrossDoesNotStealNeighboringContent` | C | 补入 quick-workspace-drag。Quick尚未验证进入标签栏隐藏导向后，旧导向臂坐标不再捕获邻组内容落点。 |
| `WS/drag_presentation_test.cpp` | `narrowGroupCrossStaysBesideItsWindowEdgePair` | A | 保留 drag-presentation 的纯 dockGuides 算法用例。纯矩形输入覆盖四边窄组、邻栏阻挡及结果稳定性，无Widgets对象，保留命中布局断言。 |
| `WS/drag_presentation_test.cpp` | `stationaryDragScrollsAndStops` | C | 补入 quick-workspace-drag。Quick的aDragScrollsTheStripUnderThePointer只测滚轮输入，缺静止悬停自动滚动、离开后停止及侧栏分支。 |
| `WS/drag_presentation_test.cpp` | `stickyRowPreviewKeepsTheLabelAgainstItsContent` | C | 补入 quick-workspace-drag。Quick未覆盖上下标签栏×置顶尺寸×多行模式的预览往返、内容身份、提交前快照及Esc恢复。掩膜几何和贴合部分需Claude审定。 |
| `WS/drag_presentation_test.cpp` | `tabTargetKeepsCarrierAndLiveContent` | C | 补入 quick-workspace-drag。Quick整窗预览有透明度/图像/落点检查，缺拆出准备阶段同步保存快照、载体原生ID、内容身份、提交时隐藏顺序及原生动画状态。 |
| `WS/drag_presentation_test.cpp` | `wholeWindowPresentationRestoresKeyboardFocus` | C | 补入 quick-workspace-drag。Quick整窗标题用例不含真实编辑内容焦点，缺侧栏预览退出后取消/释放均恢复可继续输入的焦点及原生ID。 |
| `WS/edge_strip_test.cpp` | `allPinActionStaysVisibleOutsideTheScrollableTrack` | C | 补入 quick-rail。Quick的buttonsFollowTheirOrderAndTheTailStaysAtTheEnd只检查轨道尾部预留，缺多个滚动偏移/预览下全部固定按钮仍可点击且不误激活标签。 |
| `WS/edge_strip_test.cpp` | `boundedViewportScrollsWithoutActivatingButtons` | C | 补入 quick-rail。Quick滚动用例缺滚轮事件从按钮路由且不激活、覆盖条与内容偏移同步及另一轴不滚动。像素颜色、固定厚度等外观部分需Claude审定。 |
| `WS/edge_strip_test.cpp` | `dragHoverScrollsThenStopsAndNotifiesGeometry` | C | 补入 quick-rail。Quick缺拖动悬停自动滚动的几何通知、离开及隐藏后停止。计时器名称与固定interval属实现细节，须Claude审定。 |
| `WS/edge_strip_test.cpp` | `exactFitAndOnePixelOverflowFollowResize` | C | 补入 quick-rail。Quick缺恰好容纳和一像素溢出反复缩放时的滚动范围、覆盖滚动条显隐及值同步。固定尺寸部分需Claude按规范审定。 |
| `WS/edge_strip_test.cpp` | `fewLabelsNeverShowAScrollBar` | C | 补入 quick-rail。Quick缺内容不溢出时悬停及拖动悬停都不启动滚动、偏移为零且不激活按钮的检查。 |
| `WS/edge_strip_test.cpp` | `initialOverlayRangeMatchesTheViewport` | C | 补入 quick-rail。Quick的wheelScrollsWithinTheTrackAndRebuildsKeepTheActiveTab未检查初始及未显示状态下的滚动范围、页长和覆盖滚动条同步。 |
| `WS/edge_strip_test.cpp` | `insertionGapMovesRealButtonsAndCapturesItsIndex` | C | 补入 quick-rail。Quick的dropsFollowThePointerAndTheDraggedTabLeavesAGap未验证预览空位内部多点命中同一索引、全部原按钮身份、取消复位和无效索引。空位像素断言需Claude审定。 |
| `WS/edge_strip_test.cpp` | `previewDoesNotCreateModelTabsOrReplaceContent` | C | 补入 quick-rail。Quick的dropsFollowThePointerAndTheDraggedTabLeavesAGap为独立RailItem，缺工作区预览不改模型/同边顺序/实际内容身份的集成断言。 |
| `WS/edge_strip_test.cpp` | `removingGapAndLabelsReclaimsTheScrollRange` | C | 补入 quick-rail。Quick未检查插入空位移除/标签删除后滚动范围回收至零、偏移归零及尾部按钮保持可达。 |
| `WS/edge_tab_interaction_test.cpp` | `allPinActionsHaveDistinctIconsAndFollowTheEdgeTabs` | C | 补入 quick-workspace。Quick未完整检查全部固定/全部取消固定与单项按钮的作用范围及提示，图标像素差和排列尺寸需Claude按规范审定。 |
| `WS/edge_tab_interaction_test.cpp` | `appearanceChangesRelabelRailsInPlace` | C | 补入 quick-workspace。Quick尚未验证修改侧栏标题/编号设置保持既有按钮与打开的预览、内容可见且保存新配置，不能仅按静态宽度退役。 |
| `WS/edge_tab_interaction_test.cpp` | `autoHidePresentationUsesIndependentFlagsAndPerTabIconOverrides` | C | 补入 quick-workspace。Quick未验证中央/侧栏编号与图标开关相互独立、每标签图标覆盖、跨内部组编号和重排后内容身份。 |
| `WS/edge_tab_interaction_test.cpp` | `clippedRailLabelsUseTheSameInsertionExtent` | C | 补入 quick-rail/quick-workspace-drag。缺裁剪侧栏文字后实际标签、拖动载荷和插入空位长度一致以及空位索引稳定，完整标题应保留。 |
| `WS/edge_tab_interaction_test.cpp` | `dragHoverScrollsAtViewportEdges` | C | 补入 quick-group。Quick仅有拖动中滚轮，没有标签栏边缘悬停滚动及清除悬停后停止。 |
| `WS/edge_tab_interaction_test.cpp` | `edgeBarCornerOwnership` | C | 补入 quick-workspace。Quick仅检查底边铺满，缺16种四边组合互不遮挡、尾控件可达和内容边界。像素颜色、固定间距、等距内缩部分需Claude审定。 |
| `WS/edge_tab_interaction_test.cpp` | `independentTitleLimitsFollowActualDragPresentations` | C | 补入 quick-workspace-drag。缺普通栏与侧栏来源、交换两种宽度限制后实际拖动预览/空位遵循目标策略，以及模型/内容/取消恢复。 |
| `WS/edge_tab_interaction_test.cpp` | `insertionIndexAccountsForTheScrolledViewport` | C | 补入 quick-group。Quick现有用例没有滚动后的第9项前后半区插入索引与全局坐标转换检查。 |
| `WS/edge_tab_interaction_test.cpp` | `oversizedUnclippedTitleStaysWithinTheWidgetSizeLimit` | C | 补入 quick-rail。断言绑定QWIDGETSIZE_MAX及EdgeTabButton，Quick无此Widgets上限契约，需Claude审定保留的超长标题安全边界及其预期。 |
| `WS/edge_tab_interaction_test.cpp` | `peekShowsOnlyTheChosenTabAndPinsOnlyThatTab` | C | 补入 quick-workspace。Quick的sidebarTabsOpenInAPanelOverTheWindow用分离侧栏组，缺同组仅当前标签的内容可见、点击固定当前标签只移走一项及预览关闭检查。 |
| `WS/edge_tab_interaction_test.cpp` | `peekShowsTheRealContentAndSharedBars` | C | 补入 quick-workspace-content。现有栏共享用例未覆盖四边预览内的实际编辑输入、共享上下栏、关闭重开后内容及文字保持、随后固定。 |
| `WS/edge_tab_interaction_test.cpp` | `pinnedExtentSurvivesRefreshAndUnpinRepin` | C | 补入 quick-workspace。Quick的pinnedRegionsSitAroundTheCenterAndKeepTheirExtent未覆盖尺寸命令后取消固定/重新固定、刷新和窗口缩放仍保留已设尺寸。 |
| `WS/edge_tab_interaction_test.cpp` | `railTitleLengthChangesKeepTheLeadingTabInPlace` | C | 补入 quick-workspace。Quick的wheelScrollsWithinTheTrackAndRebuildsKeepTheActiveTab只恢复既有长度的活动锚点，缺改变标题宽度时非活动前沿锚点不漂移。 |
| `WS/edge_tab_interaction_test.cpp` | `regularPinnedAndAutoHideTitlesUseIndependentLimits` | C | 补入 quick-workspace。Quick缺中央、固定和自动隐藏三种呈现各自应用标题裁剪限制，且保留完整标题/提示/无障碍名称的组合。 |
| `WS/edge_tab_interaction_test.cpp` | `sideBarInsertionUsesTheAppropriateAxis` | C | 补入 quick-rail。现有dropsFollowThePointerAndTheDraggedTabLeavesAGap只有左边部分点位，缺四边各项前后半区命中及插入标记。 |
| `WS/edge_tab_interaction_test.cpp` | `sideBarReorderCommandsKeepLiveContent` | A | 迁至 Presentation::External + QuickWorkspace。管理器重排命令、循环顺序及快捷键接线，使用Quick窗口发键与Quick内容身份查询，保留全部预期顺序和快捷键断言。 |
| `WS/edge_tab_interaction_test.cpp` | `tabIconVisibilityUsesTheHostApi` | C | 补入 quick-workspace。Quick尚无管理器setTabIconVisible切换后实际标签图标同步，同时保留每标签showIcon配置的用例。 |
| `WS/edge_tab_interaction_test.cpp` | `tabListUsesHoverCloseAndIncludesEveryTab` | C | 补入 quick-popup-menu。现有tabListActionsFollowTheTabAcrossReordering未检查所有溢出标签进入列表、悬停显隐关闭按钮及关闭弹窗。 |
| `WS/edge_tab_interaction_test.cpp` | `tabWheelScrollsWithoutChangingSelection` | C | 补入 quick-tab-bar-input。现有inputsAndFramesDriveTheSharedController未发送滚轮，缺实际标签栏滚轮滚动且当前选择不变的接线。 |
| `WS/edge_tab_interaction_test.cpp` | `titleClippingUsesTheTextAxisAndMatchesTheDragPreview` | C | 补入 quick-rail/quick-workspace-drag。缺四边裁剪轴、完整标题/提示/无障碍名称及拖动预览一致的行为。像素截取、固定宽度及字体尺寸部分需Claude审定。 |
| `WS/edge_tab_interaction_test.cpp` | `unpinRepinRestoresTheOriginalGroupAndContentPointers` | A | 迁至 Presentation::External + QuickWorkspace。只通过管理器固定/取消固定API检查原组、标签顺序和内容身份，换内容工厂及查询为Quick即可保留语义。 |
| `WS/extension_api_test.cpp` | `bindingBatchesExposeOnlyTheCommittedState` | A | 保留 CommandRegistry 逻辑检查。批量绑定的原子提交与一次通知不依赖窗口呈现 |
| `WS/extension_api_test.cpp` | `catalogRegistrationDefersEnabledCallbacksAndCoalescesRefresh` | C | 补 quick-menu-bar 的命令注册与合并刷新。需同时保留同步注册不查询、合并通知和移除瞬时贡献的断言 |
| `WS/extension_api_test.cpp` | `closedContextTargetsCannotAffectAnotherTab` | C | 补 quick-popup-menu 的关闭目标拒绝执行。原菜单留存期间原标签关闭，动作不能作用到另一个标签 |
| `WS/extension_api_test.cpp` | `commandStateCallbacksMayDestroyTheWindowHost` | C | 补 quick-popup-menu / quick-toolbar 的管理器销毁路径。现有 aCommandQueryCanReleaseTheWorkspace 销毁 workspace 而非管理器，不能替代此例 |
| `WS/extension_api_test.cpp` | `componentCommandsContributeToMatchingContextMenus` | C | 补 quick-popup-menu 的上下文贡献筛选。旧例同时核对内容类型、菜单目标和贡献执行次数，现有 Quick 场景未确认完整组合 |
| `WS/extension_api_test.cpp` | `contextActionsExecuteOnceAfterTheirMenuIsDeleted` | C | 补 quick-popup-menu 的执行后删除与单次触发。需要同一动作删除菜单之后仍恰好执行一次的完整覆盖 |
| `WS/extension_api_test.cpp` | `toolbarWindowMenuActionsExecuteOnlyOnce` | C | 补 quick-toolbar 的窗口上下文贡献单次执行。需保留窗口菜单贡献引发状态栏切换且恰好执行一次的重入边界，未确认现有 Quick 覆盖 |
| `WS/floating_panel_test.cpp` | `applicationDeactivationFollowsOutsidePolicy` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少应用失活按 dismiss 策略分别关闭/保留 Quick 面板的覆盖。 |
| `WS/floating_panel_test.cpp` | `closeCallbacksMayDestroyThePanelOwner` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少外部点击关闭面板时关闭回调同步销毁所属窗口及面板的覆盖。 |
| `WS/floating_panel_test.cpp` | `defaultPaletteIsNonModalAndOutsidePressClosesWithoutConsumingInput` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。Quick 键盘面板用例未断言默认非模态，也未验证点击另一个窗口能关闭面板并把同一次输入交给外部目标，普通 Quick PopupMenu 的外部点击抑制规则不同。 |
| `WS/floating_panel_test.cpp` | `ownedPopupsAndInsidePressDoNotDismiss` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少面板内部点击及所属子弹窗的窗口外区域输入均不误关闭面板的覆盖。 |
| `WS/floating_panel_test.cpp` | `paletteEnabledCallbackMayDestroyItsManager` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少命令面板构建/刷新 enabled 查询同步销毁管理器后安全结束的覆盖。Quick 现有销毁查询用例针对上下文菜单及工作区，不能代替。 |
| `WS/floating_panel_test.cpp` | `paletteExecutionPreservesCapturedContext` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。Quick 只统计面板命令执行次数，未测面板打开后改变活动标签，实际执行仍使用开始时窗口、标签和选定窗格。 |
| `WS/floating_panel_test.cpp` | `paneFocusAllowsDeferredManagerDeletion` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少 focusPane 的焦点进入/共享栏隐藏回调延后销毁管理器时，先切活动窗格及栏可见性再完整释放的覆盖。 |
| `WS/floating_panel_test.cpp` | `panelOptionsAreIndependent` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少 dismissOnOutsidePress×modal 四组合下实际 Quick 命令面板的模态与独立关闭行为覆盖。 |
| `WS/floating_panel_test.cpp` | `pendingExecutionDoesNotOutliveManager` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少面板已接受、命令尚在队列时销毁管理器后绝不执行命令的 Quick 覆盖。 |
| `WS/floating_panel_test.cpp` | `rejectedPaletteNeverExecutes` | B | WS/quick_popup_menu_test.cpp::palettesRunTheirChoiceFromTheKeyboard。已读实际 Quick 断言。面板先用回车执行一次，再重新打开按 Esc，等待面板销毁后 runs 仍为1，等价覆盖取消不执行命令。 |
| `WS/floating_panel_test.cpp` | `selectorIsNonModalAndActivatesTheSelectedWindow` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。Quick palettesRunTheirChoiceFromTheKeyboard 覆盖键盘切到目标窗口，但未验证非模态及搜索目标后保留目标当前标签。 |
| `WS/floating_panel_test.cpp` | `visiblePolicyChangesAllowDeferredOwnerDeletion` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少面板策略切换触发显示/隐藏事件时宿主延后销毁、窗口和面板仅释放一次、排队回调被取消的覆盖。Quick 策略API若不同先交 Claude 审定。 |
| `WS/floating_panel_test.cpp` | `visiblePolicyChangesUpdateModality` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少已显示命令面板热更新 dismiss/modal 策略后仍可见且实际窗口模态即时更新的覆盖，先请 Claude 核定 Quick 是否保留该 API 契约。 |
| `WS/floating_panel_test.cpp` | `windowActivationAllowsDeferredManagerDeletionOnShow` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），窗口激活/窗格焦点生命周期并入 quick-workspace-content。缺少 activateWindow 显示事件延后销毁管理器、先完成上下文更新再释放窗口/内容并取消队列回调的覆盖。 |
| `WS/focus_mode_test.cpp` | `activatingRailOutsideScopeRestoresWindowScope` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。真实点击侧栏须从组/窗格范围退到窗口全屏并开所点内容。Quick 侧栏与范围测试各自独立，缺联动。 |
| `WS/focus_mode_test.cpp` | `autoHideActivationRevalidatesAfterHostNotification` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。宿主通知内六类同步变更含移/固定/关/销毁/新开/取消，旧请求必须服从最新状态。Quick 侧栏例无通知内重入。 |
| `WS/focus_mode_test.cpp` | `closingAnExistingPeekPreservesTheNewerRequest` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。回收旧页的父变更回调中打开新面板，新请求应取代外层旧请求。Quick 无此重入例，ParentChange/TabContent 入口也需改写。 |
| `WS/focus_mode_test.cpp` | `closingTheTargetRestoresRemainingContent` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。关闭范围目标后退出范围并恢复剩余内容可见。Quick 当前范围例未检查关闭目标路径。 |
| `WS/focus_mode_test.cpp` | `contentScopesAreIndependentAcrossWindows` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。两窗口组/窗格范围及禅模式切换时各自栏独立恢复。Quick 范围例未跨窗口覆盖该组合。 |
| `WS/focus_mode_test.cpp` | `groupScopeProjectsTheSameWidgets` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。中央/四边固定/自动隐藏九行同时验证投影填满、边栏、标题显隐、页面/正文可编辑和退出恢复。Quick viewModesShapeTheQuickWindow 只有中央示例，缺完整矩阵。 |
| `WS/focus_mode_test.cpp` | `groupScopeSurvivesSelectingAnotherTab` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。组全屏后同组切换标签应持续范围、仅显新内容和一组。Quick 范围例未在进入后切同组标签。 |
| `WS/focus_mode_test.cpp` | `headerHidingKeepsLocationSemantics` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。中央/浮窗/固定/自动隐藏与组/窗格范围下标题、禅模式、内容身份和模型保持。Quick 未覆盖完整位置语义，不能按外观删除。 |
| `WS/focus_mode_test.cpp` | `hidingFocusedBarsPreservesTheActiveContent` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。焦点在固定组标题或附加输入栏时进入禅模式，回到原活动中央内容且不误激活固定内容。Quick 无此焦点回退场景。 |
| `WS/focus_mode_test.cpp` | `inactiveWindowKeepsItsContentFocusWithoutStealingTheOtherWindow` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。非活动窗口隐藏栏不得抢另一窗口焦点，再激活回其中央内容。Quick 跨窗焦点例未覆盖非活动窗口禅模式。 |
| `WS/focus_mode_test.cpp` | `paneScopeFollowsSameWindowMovement` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。全屏目标窗格移至同窗另一标签后范围跟随稳定 PaneId 并只显目标组。Quick 移动身份例不含全屏投影。 |
| `WS/focus_mode_test.cpp` | `paneScopePreservesNestedSplitsAndBars` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。三窗格嵌套与上下栏共享四组合要求目标投影、框架身份和退出几何恢复。Quick 栏例缺完整矩阵，TabContent/PaneHost 获取也需重写。 |
| `WS/focus_mode_test.cpp` | `zenHidesAndRestoresEveryContentBar` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的范围/禅模式行为。三种范围下六条内容栏隐藏/恢复可见性和所属关系、内容填满。Quick barsFollowTheirSharingPolicy 仅其中一分支测试禅模式，覆盖不足。 |
| `WS/hierarchy_conversion_test.cpp` | `commandsUseExplicitSourceAndTargetWithoutChangingSelectionFirst` | A | External，按描述建标签。捕获上下文、转换结果和命令注册均为管理器行为 |
| `WS/hierarchy_conversion_test.cpp` | `extractingPaneCreatesATabInTheCurrentGroup` | A | External + QuickWorkspace 内容工厂与 content 查询。保留提取后组归属、窗格身份与重复提取返回值 |
| `WS/hierarchy_conversion_test.cpp` | `groupToTabKeepsIdentityAndEveryOriginalPaneSubtree` | A | 保留 LayoutModel 逻辑用例。子树身份、修订号、活动窗格及序列化为纯模型契约 |
| `WS/hierarchy_conversion_test.cpp` | `invalidConversionsAreAtomic` | A | 保留 LayoutModel 逻辑用例。非法转换的原子性与修订号不依赖 Widgets |
| `WS/hierarchy_conversion_test.cpp` | `reparentCallbacksCannotRestoreStaleLayoutOrBars` | C | 补 quick-workspace-content 的栏重挂载重入。原例在 ParentChange 回调关闭、替换、恢复和销毁管理器，现有 Quick 可见性回调不是相同入口 |
| `WS/hierarchy_conversion_test.cpp` | `tabToGroupKeepsItsSideAndTheOriginalActiveTab` | A | 保留 LayoutModel 逻辑用例。中央和四边固定转换的归属及活动状态为纯模型契约 |
| `WS/hierarchy_conversion_test.cpp` | `targetPickerOffersEveryPaneAndKeepsCapturedSource` | C | 补 quick-dialogs / quick-popup-menu 的层级目标选择。需核对每个窗格全部方向、捕获来源及切换活动标签后选择仍正确 |
| `WS/hierarchy_conversion_test.cpp` | `wholeGroupAndTabMoveIntoTargetPane` | A | 保留 LayoutModel 逻辑用例。整组及标签转换的子树与方向无需界面 |
| `WS/hierarchy_conversion_test.cpp` | `widgetConversionKeepsContentBarsAndNeverClosesConnections` | C | 补 quick-workspace-content 的 reciprocal 转换与关闭守卫。现有 hierarchyConversionKeepsTheExistingBars 覆盖移入，未覆盖此例两向转换、文字及零关闭检查 |
| `WS/input_method_native_test.cpp` | `inputProcessorModeFollowsTabScope` | C | 补 WS/quick_content_host_test.cpp 或 Quick 原生输入法定向套件。真实 TSF/IMM 配置、实际点击标签/编辑器后按标签恢复转换模式。Quick 假后端跨窗例不能覆盖真实输入法。 |
| `WS/input_method_native_test.cpp` | `installedLayoutsRoundTripAndTabsRestore` | C | 补 WS/quick_content_host_test.cpp 或 Quick 原生输入法定向套件。真实枚举系统布局并捕获/恢复，还要求捕获不创建 QWidget 原生子窗。Quick 仅 FakeBackend，缺真实平台路径，控件专属断言需审定。 |
| `WS/input_method_native_test.cpp` | `settingsComboUsesExistingKeyboardPath` | C | 补 WS/quick_settings_test.cpp 的作用域键盘编辑。Home/End 修改输入法作用域后保留焦点，Tab 离开。Quick 设置例未覆盖该下拉的键盘和焦点。 |
| `WS/input_method_scope_test.cpp` | `creationInheritsBeforeItsFirstFocus` | A | 原逻辑套件保留，调用 focus(model, PaneId, QWindow*)，管理器改 External。新标签首次焦点前继承状态，仅换稳定 PaneId 的焦点调用，值断言保持。 |
| `WS/input_method_scope_test.cpp` | `deletedActiveRangeRestoresSurvivingRange` | A | 原逻辑套件保留，调用 focus(model, PaneId, QWindow*)，管理器改 External。删除活动范围后清理状态并恢复幸存范围，假后端值/状态数断言保持。 |
| `WS/input_method_scope_test.cpp` | `mergeDropsOnlyTheDisappearingRange` | A | 原逻辑套件保留，调用 focus(model, PaneId, QWindow*)，管理器改 External。合组只移除消失组状态，纯 synchronize/count 原位保留。 |
| `WS/input_method_scope_test.cpp` | `movingPreservesFineScopesAndUsesCoarseDestination` | A | 原逻辑套件保留，调用 focus(model, PaneId, QWindow*)，管理器改 External。六作用域跨窗后细粒度随内容、粗粒度取目标状态，非 Widgets focus 重载可保留。 |
| `WS/input_method_scope_test.cpp` | `paneMoveUsesItsOwnOrItsNewTabsState` | A | 原逻辑套件保留，调用 focus(model, PaneId, QWindow*)，管理器改 External。窗格作用域随 PaneId、标签作用域取目标标签状态，换焦点调用保留值断言。 |
| `WS/input_method_scope_test.cpp` | `scopeKeysMatchHierarchy` | A | 原逻辑套件保留，调用 focus(model, PaneId, QWindow*)，管理器改 External。六种作用域层级 key、非法 PaneId 与枚举键往返均为纯模型逻辑。 |
| `WS/input_method_scope_test.cpp` | `settingChangesClearStatesAndValidateAtomically` | A | 原逻辑套件保留，调用 focus(model, PaneId, QWindow*)，管理器改 External。作用域变化清缓存、继承当前值及非法管理器行为整体拒绝，无控件专属断言。 |
| `WS/input_method_scope_test.cpp` | `settingsAndPersistenceShareAllSixValues` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_settings_test.cpp 的输入法接线。包含 SettingsPage 下拉六值编辑/外部回显、持久化和非法输入原子拒绝。Quick 尚无该控件完整用例，纯保存覆盖不足。 |
| `WS/input_method_scope_test.cpp` | `switchingRestoresWholeOpaqueValues` | A | 原逻辑套件保留，调用 focus(model, PaneId, QWindow*)，管理器改 External。假后端保存/恢复不透明值及切换顺序，可通过 PaneId/QWindow 入口保留。 |
| `WS/input_method_scope_test.cpp` | `temporaryUiAndSameRangeNeverCallBackend` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_settings_test.cpp 的输入法接线。混合 QWidget 祖先解析、对话框临时输入、外部控件及同范围不调用后端。Quick 不用此入口，需按 Quick 临时面板/焦点路径重写行为断言。 |
| `WS/interface_appearance_test.cpp` | `familyAliasesPreserveFallbacks` | A | 保留字体别名与 fallback 逻辑检查。别名查询去重及 fallback 顺序不依赖控件 |
| `WS/interface_appearance_test.cpp` | `familyListsAndValidation` | A | 保留 InterfaceAppearance 逻辑检查。字体链解析、非法配置和范围约束为共用规则 |
| `WS/interface_appearance_test.cpp` | `menusFollowFontChanges` | C | 补 quick-menu-bar / quick-popup-menu 的字体热更新。启动前后修改界面字体需传到实际菜单，Widgets 的 WA_SetFont 断言需审定 |
| `WS/interface_appearance_test.cpp` | `railsFollowFontChanges` | C | 补 quick-rail 的字体更新接线。四边轨道在启动前后字体变化的传递尚无已确认的对应 Quick 用例 |
| `WS/interface_appearance_test.cpp` | `savedToolbarWidthsRemainValidAtAnotherZoom` | C | 补 quick-toolbar / toolbar-layout 的跨缩放恢复。原例依赖 CommandStripHost，需确认新的布局入口覆盖最小宽度跨缩放读写 |
| `WS/interface_appearance_test.cpp` | `settingsAndCommandsShareOneState` | C | 补 quick-settings 的字体字号与缩放双向同步。需保留字号增减重置命令、设置控件和缩放控制器的同步 |
| `WS/interface_appearance_test.cpp` | `zoomUpdatesExistingAndFutureWindowsWithoutReplacingContent` | C | 补 quick-workspace 的即时缩放与内容身份。旧例同时测现有及未来窗口、四边布局和恢复缩放，Quick 未确认等价覆盖 |
| `WS/localization_test.cpp` | `catalogsHaveNoUnfinishedMessages` | A | 保留翻译目录与嵌入目录行为检查。原文、译文、复数和嵌入资源校验不依赖 Widgets |
| `WS/localization_test.cpp` | `customColorNamesRemainContent` | A | 保留配色名称翻译逻辑检查。预设译名与用户原文边界不依赖控件 |
| `WS/localization_test.cpp` | `dynamicMenuTextIsTranslated` | C | 补 quick-menu-bar 的动态路径与用户原文。需保留动态菜单路径翻译、自定义标题原文及多语言命令名称组合 |
| `WS/localization_test.cpp` | `embeddedCatalogsTranslateCommandsAndMenus` | C | 补 quick-menu-bar 的完整目录接线。旧例混合命令定义原文与实际菜单译文，不能只用标题裁切用例替代 |
| `WS/localization_test.cpp` | `languageSettingsStaySynchronizedAndSeparateFromLayout` | C | 补 quick-settings 的跨管理器语言设置同步。需核对两个设置页、请求语言和布局不写全局语言字段 |
| `WS/localization_test.cpp` | `missingHostCatalogDoesNotInstallPartialLanguage` | A | 保留 TranslationManager 逻辑检查。缺失目录的原子安装不依赖控件 |
| `WS/localization_test.cpp` | `pluralMessagesUseCorrectForms` | C | 补 quick-toolbar 状态栏的双语言复数。原例通过实际 WorkspaceStatusBar 同时核对用户标题、空标题和标签计数复数 |
| `WS/localization_test.cpp` | `requestedLanguageNeedsRestart` | A | 保留 TranslationManager 逻辑检查。请求语言、当前语言和重启状态不依赖窗口 |
| `WS/localization_test.cpp` | `sourceLanguageIsEnglish` | A | 保留源语言及目录行为检查。源语言和命令原文字段为翻译契约 |
| `WS/localization_test.cpp` | `stableLanguageIds` | A | 保留翻译 ID 逻辑检查。稳定 ID、别名和语言名不依赖控件 |
| `WS/localization_test.cpp` | `standardQtDialogsUseEmbeddedChineseCatalog` | C | 由 Claude 审定 Qt Widgets 标准对话框覆盖的归属。该例仅测试 QMessageBox 按钮译文，去 Widgets 后需审定旧功能退役及 Quick 对话框译文对应范围 |
| `WS/memory_lifecycle_test.cpp` | `closedLayoutsReleaseTheirPaneTrees` | A | 原套件保留，External+QuickWorkspace，更新对象遍历入口。每次恢复新稳定 ID 后旧模型树和内容释放、窗口/标签数有界，可保留 weak_ptr/QPointer 断言。 |
| `WS/memory_lifecycle_test.cpp` | `commandMayUnloadWhileItsMenuIsBeingCreated` | C | 补 WS/quick_toolbar_test.cpp、quick_menu_bar_test.cpp 与 quick_workspace_content_test.cpp 的生命周期。enabled 查询自卸载命令后无残留菜单/action，资源回基线。Quick aCommandQueryCanReleaseTheWorkspace 是销毁工作区，未覆盖自卸载与菜单资源残留。 |
| `WS/memory_lifecycle_test.cpp` | `kindChangesKeepOneCommandSubscriptionPerWindow` | C | 补 WS/quick_toolbar_test.cpp、quick_menu_bar_test.cpp 与 quick_workspace_content_test.cpp 的生命周期。逐窗口预条件恰调用一次依赖旧 chrome 订阅和 Widgets 统计。需审定 Quick 无重复资源/行为检查，不能改 External 变成零调用。 |
| `WS/memory_lifecycle_test.cpp` | `removedCommandsReleaseDynamicMenus` | C | 补 WS/quick_toolbar_test.cpp、quick_menu_bar_test.cpp 与 quick_workspace_content_test.cpp 的生命周期。100轮贡献在工作台/浮窗保留共享菜单、释放临时 action/菜单且对象数不增。Quick structureUpdatesRetainInputAndReleaseRetiredRows 仅局部弹出菜单，缺窗口菜单完整生命周期。 |
| `WS/memory_lifecycle_test.cpp` | `repeatedLayoutAndWindowCyclesReleaseOwnedResources` | C | 补 WS/quick_toolbar_test.cpp、quick_menu_bar_test.cpp 与 quick_workspace_content_test.cpp 的生命周期。28轮操作精确检查 QObject/Widgets 类别数、原生句柄释放与最终一个 storage。Quick 现有身份例缺全链有界资源，统计口径需改写审定。 |
| `WS/menu_structure_test.cpp` | `applicationTreeUsesSemanticGroupsAndCoversEveryCommand` | A | 原 menu-structure 套件，已用 External，保留菜单描述树断言。现已用 External，验证菜单描述树分组、顺序、分隔符和每条命令唯一归类，不建 Widgets 视图，原样保留。 |
| `WS/menu_structure_test.cpp` | `directionalFamiliesHaveAConsistentOrder` | A | 原 menu-structure 套件，已用 External，保留菜单描述树断言。现已用 External，断言菜单描述树方向家族及窗口切换项顺序，原样保留。 |
| `WS/menu_structure_test.cpp` | `externalMenuHostCanOutliveItsManager` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少外部菜单宿主比管理器活得更久时排队命令取消、刷新禁用/隐藏保留项、销毁呈现后旧项仍不能执行的等价生命周期覆盖，Quick 所有者关闭释放测试不等价。 |
| `WS/menu_structure_test.cpp` | `floatingMenuContributionsStayIncrementalAndClassified` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少浮窗已有菜单/关闭项身份不变、晚贡献完整本地命令且排除应用命令、反复刷新不复制项的覆盖。 |
| `WS/menu_structure_test.cpp` | `floatingMenuExecutionKeepsItsTabAndPaneContext` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。Quick menuBarUsesLateCommandsAndItsOwnWindow 只断言 window/tab，缺少 owner 已激活时浮窗多窗格命令仍传本地 group/pane、局部显隐独立的覆盖。 |
| `WS/menu_structure_test.cpp` | `floatingMenusAreLocalAndExcludeWholeApplicationOperations` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少实际 Quick 浮窗菜单按所有命令的 floatingMenuExclusion 完整归类，以及排除应用操作/工作台栏并保持本地菜单壳的覆盖。 |
| `WS/menu_structure_test.cpp` | `groupContextMenusDoNotPretendToOperateOnASingleTab` | A | 原 menu-structure 套件，已用 External，保留菜单描述树断言。现已用 External，验证组菜单声明命令一次覆盖、排除单标签/窗格命令和分隔符，不依赖控件。 |
| `WS/menu_structure_test.cpp` | `lateContributionsRespectMetadataAndPreserveExistingMenus` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。Quick 晚注册用例未覆盖贡献 menuPath/order 排序、既有文件菜单/关闭项身份保持、重复刷新不增项及可滚动子菜单的完整组合。 |
| `WS/menu_structure_test.cpp` | `lateTopLevelMenusJoinToolbarCustomizationWithoutResettingChoices` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少晚注册根菜单进入工具栏自定义菜单，同时已隐藏 view 根菜单及保存排除项保持的覆盖。 |
| `WS/menu_structure_test.cpp` | `layoutMenusIdentifyTheSourceObjectAndConversionTarget` | A | 原 menu-structure 套件，已用 External，保留菜单描述树断言。现已用 External，断言布局菜单对象分组、转换命令归属、唯一性和无旧分组，原样保留。 |
| `WS/menu_structure_test.cpp` | `menuExecutionAndShortcutsUseTheRegistryExactlyOnce` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少同一菜单命令触发一次、快捷键再触发一次、改绑定更新提示且无菜单自有快捷键重复分发、禁用后触发无效的完整覆盖。 |
| `WS/menu_structure_test.cpp` | `nestedContextCommandsOperateOnTheClickedContent` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。Quick 子菜单用例仅测贡献命令，未测未激活标签的嵌套移动菜单和非活动窗格移到另一标签后保留原内容身份、剩余窗格及目标活动状态。 |
| `WS/menu_structure_test.cpp` | `stateRefreshKeepsChecksVisibilityAndIndependentWindows` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少菜单勾选回写与双窗口独立性，以及侧栏未固定/已固定时 pin/unpin 可见性即时更新的覆盖。 |
| `WS/menu_structure_test.cpp` | `tabAndPaneContextMenusRetainScopeAndUsefulGroups` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少实际 Quick 标签/窗格菜单保留宿主编辑项顺序、关闭范围/移动分组、共享栏勾选和全部上下文声明唯一归类的组合覆盖。 |
| `WS/menu_structure_test.cpp` | `tabAppearanceCommandsUseOneClassifiedCheckedAction` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少两种窗口所有外观开关在各自菜单中唯一归类、勾选/可用状态与描述一致、菜单和命令各只发布一次变化且内容身份保持的覆盖。 |
| `WS/menu_structure_test.cpp` | `unknownCategoriesStayVisibleAndSortIndependentlyOfRegistration` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少未知分类进入扩展分组、按标题稳定排序、已知分类贡献分组及所有分隔符合法的实际 Quick 菜单覆盖。 |
| `WS/menu_structure_test.cpp` | `wheelRowsRequireMultilineAndResetWithIt` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少 Quick 菜单中多行/收起/滚轮展开的启用依赖、单行时保留选中但禁用和精确变化通知的覆盖。 |
| `WS/menu_structure_test.cpp` | `windowCloseAndKindChangesReleaseTheirMenuObjects` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。缺少从菜单关闭窗口后释放菜单树，以及反复升降级销毁旧完整菜单并保留内容、最终关闭释放的覆盖。 |
| `WS/menu_structure_test.cpp` | `windowListActivatesAndRestoresTheChosenWindow` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。Quick 菜单窗口列表已测激活，普通工作区已测恢复最小化，但缺少菜单路径直接恢复最小化目标、保留目标标签、刷新勾选及非法目标拒绝的组合覆盖。 |
| `WS/menu_structure_test.cpp` | `windowListGroupsFamiliesAtTheEndAndRefreshesStableItems` | C | 拟补 WS/quick_menu_bar_test.cpp（quick-menu-bar）及 WS/quick_popup_menu_test.cpp（quick-popup-menu）。Quick 动态窗口列表用例未覆盖按家族分组顺序、缩进深度、重新归属家族、移除旧项且保留其余项身份及尾部分隔符唯一。 |
| `WS/movement_test.cpp` | `commandParametersAndInvalidCallsAreAtomic` | A | External，内容由 QuickWorkspace 工厂创建。参数执行、零关闭检查及非法调用的快照原子性为管理器契约 |
| `WS/movement_test.cpp` | `directionalGroupTransfersWrapByVisibleGeometry` | A | 已按审定要求试迁 External，实际 Quick 组几何驱动四方向标签／窗格循环移动，保留目标、隐藏侧栏不介入及内容身份断言。 |
| `WS/movement_test.cpp` | `directionalTiesFollowStableModelOrder` | C | 补 quick-workspace 的方向距离相等选择。原例手工设置 QWidget 全局几何并验证模型稳定顺序，需 Quick 几何入口 |
| `WS/movement_test.cpp` | `emptyWindowReceivesSamePaneAndSourceSharing` | C | 补 quick-workspace-content 的移入空窗口。需覆盖拆出 pane 的新标签策略、原内容及栏位重挂载和源窗口释放 |
| `WS/movement_test.cpp` | `existingWindowMovementIncludesBothKindsAndWraps` | A | External + QuickWorkspace 和栏位工厂。目标窗口选择、原内容及栏位 window 身份无需 Widgets 控件内部结构 |
| `WS/movement_test.cpp` | `groupFocusWrapsWithoutEnteringHiddenRail` | A | 已按审定要求试迁 External，实际 Quick 布局驱动四方向焦点循环，保留活动标签及排除隐藏侧栏的原断言。 |
| `WS/movement_test.cpp` | `paneSwapAndFocusWrapWithinCurrentTab` | A | 已按审定要求试迁 External，实际 Quick 窗格几何驱动四方向焦点及交换，保留交换后的矩形、栏归属及内容身份断言。 |
| `WS/movement_test.cpp` | `paneTransfersToAdjacentTabKeepBarsAndAdoptTargetPolicy` | C | 补 quick-workspace-content 的相邻标签转移。需保留顶部底部独立策略、栏父项、活动栏显隐与来源标签循环 |
| `WS/movement_test.cpp` | `ratioCommandUpdatesExistingSplitterWithoutRebuilding` | C | 补 quick-workspace 的 layout.setRatio 接线。quick-split-tree 仅直连 LayoutModel，未覆盖管理器命令及已有分割项身份 |
| `WS/movement_test.cpp` | `ratioCommandUpdatesOnlyTheOwningSplitter` | C | 补 quick-workspace 的分割比例局部更新。需确认中央与固定区域命令仅改变所属分割树及保留其他几何 |
| `WS/movement_test.cpp` | `reorderingWrapsWithinItsOwnSequence` | A | External + QuickWorkspace 内容身份查询。保留中央、固定和自动隐藏序列的首尾循环及内容身份 |
| `WS/movement_test.cpp` | `splitterGapSelectsNearestProjection` | C | 补 quick-workspace 的分割间隙方向命中。原例手工注入组和 PaneHost 几何，Quick 尚缺最近投影选择覆盖 |
| `WS/movement_test.cpp` | `tLayoutNavigationRetainsOrthogonalAnchor` | C | 已按审定要求试迁，Quick 下组／窗格两行均在鼠标焦点通知后返回错误行。导航锚点的 MouseFocusReason 重置仍限 QWidget，旧 FocusIn 输入不能平迁；原 Widgets 用例与断言保留，见交接 Q3。 |
| `WS/multirow_toolbar_test.cpp` | `aNewRowCanBeCommittedBeyondTheFormerSecondRow` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少上下区域已有四行后把拖动栏提交到第五行，逻辑行顺序、占用行数和呈现行数一致的覆盖。 |
| `WS/multirow_toolbar_test.cpp` | `arbitraryRowsGrowFromTheOuterEdge` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少上下区域任意六行的实际呈现及每行稳定顺序覆盖，Quick 现有工具栏用例仅覆盖默认与新增一行。 |
| `WS/multirow_toolbar_test.cpp` | `bothAreasRoundTripAndRejectAmbiguousLayouts` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少上下稀疏多行同时保存恢复、跨区域重复ID与行非数组原子拒绝、缺 bottomRows 默认仅顶部的容器覆盖。 |
| `WS/multirow_toolbar_test.cpp` | `crossAreaMovementPreservesPresentationAndCustomization` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少跨上下区域与高行号移动时实际工具栏及视口对象身份、隐藏按钮和宽度请求全部保持的覆盖。 |
| `WS/multirow_toolbar_test.cpp` | `dragCreatesOnlyTemporaryInnermostRows` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少上下区域离开源行即回收空行、只在最内侧显示一个临时空落区、离开即回收和 Esc 恢复的覆盖。 |
| `WS/multirow_toolbar_test.cpp` | `draggingAcrossAreasUsesTheSameLiveStrip` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。Quick handleDragsMoveStrips 检查移到底部和取消，但未测两个方向跨区后源栏对象身份、可见性与两区无空占位行。 |
| `WS/multirow_toolbar_test.cpp` | `draggingOverEarlierRowsDoesNotReserveAPlaceholder` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少上下区域拖过前部已有行时无多余占位，只有落到最内行才临时增加一行、Esc 恢复的覆盖。 |
| `WS/multirow_toolbar_test.cpp` | `emptyBottomIsDiscoveredWithoutPreallocatingSpace` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少空底区平时不占空间、拖到内容区仍不展开、进入底部落区才生成临时行，Esc 清除并恢复源栏的覆盖。 |
| `WS/multirow_toolbar_test.cpp` | `publicMovementPreservesIdentityAndAppendsRows` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少公开 moveToolbar 每次仅发一次变化、追加到新行、同一行插入保持顺序且工具栏/视口/内容身份均保留的覆盖。 |
| `WS/multirow_toolbar_test.cpp` | `publicMovementRejectsUnboundedAndInvalidRequestsWithoutMutation` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。含通过 Widgets 宿主注册第二个强制弹性所有者的拒绝路径，不能只把 manager 换 External。Quick 缺全部非法区域/行/索引/冲突拒绝且零通知的对等用例。 |
| `WS/multirow_toolbar_test.cpp` | `savedLayoutsContainOnlyOccupiedTrailingRows` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少单栏切换上下区域时保存结果不留尾部空行，以及浮窗所有栏移到底部后顶部行数组为空的覆盖。 |
| `WS/multirow_toolbar_test.cpp` | `sparseLayoutsKeepLogicalRowsWithoutAllocatingEmptyWidgets` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少十万空/未知逻辑行恢复仍只分配占用行、隐藏/移动/拖动期间资源有界、取消保持对象和保存布局的覆盖。Quick 基本拖动不能代替此资源契约。 |
| `WS/multirow_toolbar_test.cpp` | `vacantRowsDoNotLeaveBlankSpace` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少移动/隐藏/跨区域后空行不占工作区空间、全部隐藏区域收为零、重新显示恢复有效行的功能覆盖。 |
| `WS/native_drag_test.cpp` | `realDragKeepsFollowingAcrossNativeMoveAndStripCapture` | C | 补入 quick-native-drag。Quick的carriedWindowsMoveInTheSystemLoopAndDropIntoStrips已有原生移动循环和再进入，缺多行/独立置顶行内移动时插入空位跟随、非置顶结果及原内容身份断言。 |
| `WS/native_frame_test.cpp` | `cachedPreparationAndCompositionRefreshKeepTheNativeFrame` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 Quick 重复准备走缓存、合成刷新强制更新后仍保留原生样式、句柄、几何及隐藏状态的覆盖。revision/重算次数是否实现细节请 Claude 审定。 |
| `WS/native_frame_test.cpp` | `captionlessRegionKeepsCompositingAfterNativeShowAndRecalculation` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。独立 Win32 证据用例，覆盖 margins/style 顺序×系统菜单标志对 captionless region 显示和重算后合成/region保持的影响。建议移入 Quick 原生套件保留，归属请 Claude 审定。 |
| `WS/native_frame_test.cpp` | `compactFirstShowAlreadyHasThePreparedNativeFrame` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 Quick 紧凑拖动预览第一次显示前后原生样式和 DWM 合成已准备，显示中每次观察仍正确且不换句柄的覆盖。 |
| `WS/native_frame_test.cpp` | `compactMaskCommitSurvivesPreviousCacheReplay` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。旧用例回放上一缓存 mask 验证紧凑状态提交不会被旧缓存覆盖，依赖 Widgets mask 管道。Quick 缺相应重入覆盖，迁移是否改变契约先由 Claude 核定。 |
| `WS/native_frame_test.cpp` | `compactPreviewHasNoFrameAcrossItsTransparentGap` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。旧用例用桌面像素检查紧凑预览透明缺口没有系统亮边，包含纯轮廓外观检查。先列 C 请 Claude 审定静态外观退役范围，并保留必要原生客户区/透明区域功能契约。 |
| `WS/native_frame_test.cpp` | `customSystemMenuFollowsWindowLifetime` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。Quick surfacesRecreateWithoutTransferringMenuOwnership 覆盖原生重建和菜单所属窗口，但缺少两种窗口的重复准备不变更隐藏位置、Qt 标志变化后完整菜单仍正确及最终释放的组合。 |
| `WS/native_frame_test.cpp` | `customSystemMenuHonorsFixedResizeAxes` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少固定一轴/两轴时 Quick 实际系统菜单 SC_SIZE 与 SC_MOVE 可用状态、查询不显示窗口且不恢复 WS_SYSMENU 的覆盖。 |
| `WS/native_frame_test.cpp` | `customSystemMenuPreservesStandardCommandsAndStates` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。Quick 只测菜单句柄和生命周期，缺少正常/最大化/最小化×两窗口类型的全部系统菜单项、顺序、可用状态、标签及查询不变更窗口的覆盖。 |
| `WS/native_frame_test.cpp` | `detachedWindowRemainsOpaqueAndKeepsItsContentWhileMoving` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 Quick detachTab 得到浮窗后多次移动仍保留原生句柄、内容身份及正确原生样式的定向覆盖。 |
| `WS/native_frame_test.cpp` | `expandedModesFillNativeClientWithoutFrameOrRoundCorners` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。Quick viewModesShapeTheQuickWindow 已测内容布局，但缺少两种窗口×三种全屏模式的原生监视器/客户区填满、句柄和内容身份、还原位置，以及 DWM 写入结果覆盖。纯边框外观断言交 Claude 审定。 |
| `WS/native_frame_test.cpp` | `frameIsPreparedBeforeTheFirstShow` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少实际 QuickWorkspace 两种窗口在首次显示前已创建并准备好原生句柄、仍不可见且样式正确的覆盖，独立 Quick 原生夹具手动调用 applyNativeFrame 不等价。 |
| `WS/native_frame_test.cpp` | `initiallyMaximizedWindowPreservesNormalGeometry` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少初始最大化经模块还原位置入口与隐藏窗口 showMaximized 两路径、两种窗口原生 rcNormalPosition 以及还原后 Qt 几何一致的覆盖。 |
| `WS/native_frame_test.cpp` | `nativeCaptionDoesNotConsumeClientSpace` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 Quick 实际原生外框/客户区与 Qt 几何的一致性、顶部不被系统标题占用且外侧缩放框保留的覆盖，属于内容可用区契约。 |
| `WS/native_frame_test.cpp` | `nativeCorrectionPreservesOtherPlacementRequests` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 Quick 原生 WM_WINDOWPOSCHANGING 对其他屏幕/尺寸/标志组合请求逐字段不修改，且不改变原生句柄、几何与最大化状态的覆盖。 |
| `WS/native_frame_test.cpp` | `nativePaintMessagesCannotReplaceTheCustomClient` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少正常/紧凑 Quick 窗口经 WM_NCPAINT/WM_NCACTIVATE 后保持自绘客户区、捕获、几何、mask和内容的覆盖。屏幕像素与内部计数断言先请 Claude 按规范审定。 |
| `WS/native_frame_test.cpp` | `nativeRegionsDisableNcRenderingDespiteTheEnabledPolicy` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。独立 Win32 证据用例，覆盖应用/清除窗口 region 后 DWM 非客户区渲染策略实际状态。Quick 无该平台基线，建议保留原断言并由 Claude 审定归属。 |
| `WS/native_frame_test.cpp` | `nativeSystemCommandsKeepWindowTransitionsAndCloseGuard` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 WM_SYSCOMMAND 最大化/恢复/最小化的实际 Quick 窗口工作区和还原位置，以及原生 SC_CLOSE 的拒绝/接受守卫、内容与菜单释放覆盖。 |
| `WS/native_frame_test.cpp` | `preparationIsIdempotentAndDoesNotShowOrMoveTheWindow` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少实际 QuickWorkspace 隐藏窗口反复原生准备不改句柄、位置、可见性和样式的覆盖。 |
| `WS/native_frame_test.cpp` | `preparationLeavesSystemAnimationPreferencesUntouched` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 Quick 原生准备前后 SPI_GETANIMATION 和 SPI_GETCLIENTAREAANIMATION 系统偏好原样保持的覆盖。 |
| `WS/native_frame_test.cpp` | `presentationChangesRetainHandleCaptureAndCompositing` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少实际 Quick 在整窗/紧凑预览切换期间句柄、捕获、位置、内容身份不变，region 与合成模式对应并在结束释放捕获的覆盖。 |
| `WS/native_frame_test.cpp` | `publicSizeLimitsRemainEffective` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少两种实际 Quick 窗口原生 MINMAXINFO 最小/最大拖拽尺寸、工作区限制、Qt与SC_MAXIMIZE 两路径最大化和恢复后仍保留限制的覆盖。 |
| `WS/native_frame_test.cpp` | `qtFlagChangesKeepTheNativeFrame` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 Quick 两种窗口置顶标志往返变化时原生样式保持且隐藏窗口不被显示的覆盖。 |
| `WS/native_frame_test.cpp` | `qtStateChangesKeepNativeStylesAndRestoreGeometry` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少 Quick 两种窗口最小化/恢复/最大化往返保留原生样式、工作区和原始普通位置，关闭后原生窗口不可见的完整覆盖。 |
| `WS/native_frame_test.cpp` | `removingSysMenuMakesTheNativeMenuUnavailable` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。独立 Win32 证据用例，不建 Qt/Widgets，覆盖移除 WS_SYSMENU 前缓存/未缓存菜单的差异。Quick 无该平台基线，建议在 quick-native-frame 保留原断言并由 Claude 审定归属。 |
| `WS/native_frame_test.cpp` | `reorderedWholeWindowDragCannotRestoreTheStockCaption` | C | 拟补 WS/quick_native_frame_test.cpp（quick-native-frame），拖动集成补入 quick-native-drag / quick-workspace-drag。缺少自定义按钮栏重排后整窗/标签拖动多次往返与取消、原生绘制消息不能恢复系统标题、捕获及所有内容身份与模型保持的 Quick 覆盖。像素外观和内部计数先请 Claude 审定。 |
| `WS/pane_bar_policy_test.cpp` | `aTabBarBelongsToOneOwner` | C | 补 quick-workspace-content 的跨标签栏独占归属。现有 barOwnership 拒绝 tab 到 pane 的复用，仍需确认跨 tab、上下和内容入口复用的全部拒绝路径 |
| `WS/pane_bar_policy_test.cpp` | `changingPolicyPreservesWidgetsFocusAndSplitModel` | C | 补 quick-workspace-content 的策略热切换。需同时保持焦点、内容身份和分割树，旧例还有 PaneHost 层级断言 |
| `WS/pane_bar_policy_test.cpp` | `closingPaneDestroysOnlyItsBars` | C | 补 quick-workspace-content 的双栏局部释放。现有所有权用例只设置被关闭 pane 的顶部栏，需核对底部栏一并释放及其他栏保留 |
| `WS/pane_bar_policy_test.cpp` | `invalidCallsLeaveThePoliciesUnchanged` | A | External，按描述建标签。非法策略与标签 ID 的返回值、共享策略及快照保持为管理器契约 |
| `WS/pane_bar_policy_test.cpp` | `layoutRestoresPolicyWithoutSerializingWidgets` | C | 补 quick-workspace-content 的恢复与栏策略。需复原上下策略、工厂次数以及实际栏挂载，当前 Quick 未确认覆盖 |
| `WS/pane_bar_policy_test.cpp` | `policiesAndBarsSurviveMovesAndPaneExtraction` | C | 补 quick-workspace-content 的 pane 提取及共享策略。现有栏所有权用例未确认提取为新标签后上下策略继承的完整组合 |
| `WS/pane_bar_policy_test.cpp` | `replacingTabBarsDefersDeletionLikePaneBars` | C | 补 quick-workspace-content 的栏替换与信号中替换。需保留替换后立即隐藏、延迟释放以及 clicked 回调中仍存活的时序 |
| `WS/pane_bar_policy_test.cpp` | `tabFallbackStaysSingleAndMissingPaneBarsTakeNoSpace` | C | 补 quick-workspace-content 的缺省栏与空栏。需覆盖回退标签栏只出现一次和缺失 pane 栏不占空间的功能边界 |
| `WS/pane_bar_policy_test.cpp` | `topAndBottomPoliciesAreIndependent` | C | 补 quick-workspace-content::barsFollowTheirSharingPolicy 的独立上下组合。现有 perPane 单一布尔同时控制上下栏，未覆盖旧例四种独立组合 |
| `WS/pane_minimum_test.cpp` | `dynamicMinimumChangesPropagateWithoutRebuildingContent` | C | 补 WS/quick_split_tree_test.cpp 与 quick_workspace_content_test.cpp 的最小尺寸传播。动态提示/显式下限增减更新页面/窗口并保留内容/容器身份。Quick 栏高度例未覆盖内容双轴最小值。 |
| `WS/pane_minimum_test.cpp` | `fallbackDoesNotOverrideExplicitOrHintedSmallMinimums` | C | 补 WS/quick_split_tree_test.cpp 与 quick_workspace_content_test.cpp 的最小尺寸传播。区分无值回退、小显式值和 minimumSizeHint。Quick 对应内容最小值协议不同，需先审定三种来源行为。 |
| `WS/pane_minimum_test.cpp` | `initialPlacementDoesNotLowerContentMinimum` | C | 补 WS/quick_split_tree_test.cpp 与 quick_workspace_content_test.cpp 的最小尺寸传播。超出屏幕可用宽度的内容下限不能被初始定位或再缩小降低。Quick 无此窗口摆放回归，不能按外观删除。 |
| `WS/pane_minimum_test.cpp` | `nestedSplittersPropagateBothAxes` | C | 补 WS/quick_split_tree_test.cpp 与 quick_workspace_content_test.cpp 的最小尺寸传播。三窗格嵌套的双轴传播、窗口限制和内容不重叠，Quick 当前 SplitTree 仅两叶，覆盖不足。 |
| `WS/pane_minimum_test.cpp` | `sharedAndPerPaneBarsContributeTheirOwnMinimums` | C | 补 WS/quick_split_tree_test.cpp 与 quick_workspace_content_test.cpp 的最小尺寸传播。共享/独立栏宽高下限、动态宽度、切换后窗口缩小及内容容纳。Quick barsFollowTheirSharingPolicy 仅高度增长/不重叠，缺完整宽度和窗口接线。 |
| `WS/pane_minimum_test.cpp` | `sharingResizeMayDestroyTheManager` | C | 补 WS/quick_split_tree_test.cpp 与 quick_workspace_content_test.cpp 的最小尺寸传播。共享切换的内容 resize 回调销毁管理器时安全返回并延迟释放。Quick 直接销毁 SplitTree view 未覆盖此 manager/共享链。 |
| `WS/pane_minimum_test.cpp` | `splittersAndWindowHonorContentMinimums` | C | 补 WS/quick_split_tree_test.cpp 与 quick_workspace_content_test.cpp 的最小尺寸传播。四方向×显式/提示最小值，检查窗格不重叠及窗口下限。Quick 两叶布局仅覆盖树内下限，缺窗口和最小值来源接线。 |
| `WS/pane_settings_test.cpp` | `choiceControlsMarkTheirDefaults` | C | 补 WS/quick_settings_test.cpp 的设置页行为。十种选择控件默认项和值及选项文字，Quick 只检查一个下拉 Default 标记，缺完整默认数据契约，外观部分待审定。 |
| `WS/pane_settings_test.cpp` | `extractedPaneCarriesItsOwnBarsAndPreservesTabFallback` | C | 补 WS/quick_workspace_content_test.cpp 的栏所有权/提取行为。extractPane 携带双栏到新窗，原标签回退栏保持，关闭提取标签只释放其栏。Quick 栏例是 movePane，缺完整提取路径。 |
| `WS/pane_settings_test.cpp` | `fullscreenAndZenAreCommandsInsteadOfPersistentSettings` | C | 补 WS/quick_settings_test.cpp 的设置页行为。页只提供默认范围，改默认不进入模式，命令进退且临时模式不持久化。Quick 未覆盖完整目录/命令联动。 |
| `WS/pane_settings_test.cpp` | `internalSettingsRestoreWithoutHostContentFactory` | C | 补 WS/quick_settings_test.cpp 的设置页行为。恢复内建设置页无宿主工厂，值回显与重复打开唯一性保持。Quick 设置例未恢复内建页快照。 |
| `WS/pane_settings_test.cpp` | `internalSettingsSplitAndExtractWithoutHostContentFactory` | C | 补 WS/quick_settings_test.cpp 的设置页行为。内建页经 split 命令复制/提取而不用宿主工厂，保持两个页面身份。Quick 无该类型生命周期例。 |
| `WS/pane_settings_test.cpp` | `newTabPositionComboSupportsKeyboardAndExternalSynchronization` | C | 补 WS/quick_settings_test.cpp 的设置页行为。四位置真实键盘、保存恢复、外部回显无回声且其他设置不变。Quick 仅鼠标选一值/回显另一值，覆盖不足。 |
| `WS/pane_settings_test.cpp` | `newTabPositionSearchPreservesTheExistingSettingsAndNavigation` | C | 补 WS/quick_settings_test.cpp 的设置页行为。真实输入精确 ID 只显一行，清空恢复全目录/类别且标签不变。Quick 未断言全 ID 集和清空/身份。 |
| `WS/pane_settings_test.cpp` | `omittedNewTabPositionUsesItsDefault` | C | 补 WS/quick_settings_test.cpp 的设置页行为。省略字段恢复时默认与下拉回显一致，其他设置/目录保持。Quick 未覆盖省略字段恢复接线。 |
| `WS/pane_settings_test.cpp` | `paneBarAdoptionRejectsInvalidAndAlreadyManagedWidgets` | C | 补 WS/quick_workspace_content_test.cpp 的栏所有权/提取行为。parentWidget/windowSystemManaged 实现断言及同栏不可占两个位置。Quick barOwnershipFollowsTabsAndPanesRatherThanPages 有部分拒绝，完整改写边界待审定。 |
| `WS/pane_settings_test.cpp` | `paneBarsShareCompactSlotsAndFollowFocus` | C | 补 WS/quick_workspace_content_test.cpp 的栏所有权/提取行为。实际内容焦点切共享上下栏/回退，混有 TabPage/PaneHeader 实现和外观断言。Quick 栏例缺原实际 FocusIn 接线，需审定混合断言。 |
| `WS/pane_settings_test.cpp` | `settingControlsUpdateImmediatelyAndReflectExternalChanges` | C | 补 WS/quick_settings_test.cpp 的设置页行为。多控件写入、外部回显和依赖 enabled，含收缩宽度边界。Quick 两个设置例覆盖子集，缺拆窗/空窗策略等完整接线。 |
| `WS/pane_settings_test.cpp` | `settingNotificationsAllowSynchronousHostDestruction` | C | 补 WS/quick_settings_test.cpp 的设置页行为。全部列举控件同步通知可销毁页面或管理器，一次保存、值正确、删除时序安全。Quick 无通知内销毁矩阵。 |
| `WS/pane_settings_test.cpp` | `settingsCategoryIndicatorUsesItsOwnSpacedGutter` | C | Claude 审定静态外观退役范围，暂保留用例。强调色指示条/箭头像素、固定间距/字重属静态外观候选，交 Claude 审定，当前不删。 |
| `WS/pane_settings_test.cpp` | `settingsCategoryNavigationScrollsAndFollowsTheViewport` | C | 补 WS/quick_settings_test.cpp 的设置页行为。类别点击滚到对应区，手动滚动同步类别，包含末尾嵌套类别。混有字体/分隔宽外观，须审定并保留功能覆盖。 |
| `WS/pane_settings_test.cpp` | `settingsCategoryTreeSupportsKeyboardExpansion` | C | 补 WS/quick_settings_test.cpp 的设置页行为。Left/Right 展开折叠、Down 选下类别且不删实际行。Quick 未覆盖类别键盘导航。 |
| `WS/pane_settings_test.cpp` | `settingsDisclosureHoverChangesOnlyTheChevron` | C | Claude 审定静态外观退役范围，暂保留用例。抓图像素比较箭头悬停颜色/变化区域，属静态外观候选。只登记 C，交 Claude 确认后处理，不造像素替代。 |
| `WS/pane_settings_test.cpp` | `settingsOpenAsOneMovableWorkspaceTab` | C | 补 WS/quick_settings_test.cpp 的设置页行为。重复打开同页、拆出后再次打开定位既有浮窗页。Quick windowSettingsEditTheBehavior 仅同窗口标签数，缺拆出后的唯一性。 |
| `WS/pane_settings_test.cpp` | `settingsRowsUseTheWorkspaceSurfaceInsteadOfTheSystemPalette` | C | Claude 审定静态外观退役范围，暂保留用例。抓图首像素与主题 surface 比较及主题颜色更新属静态外观候选，先 C 交 Claude 审定。 |
| `WS/pane_settings_test.cpp` | `settingsSearchAndCategoryNavigationKeepRealRows` | C | 补 WS/quick_settings_test.cpp 的设置页行为。完整设置 ID 集、中文类别、点击类别不筛行及多种搜索。Quick 仅少量键，缺完整目录/类别导航契约。 |
| `WS/pane_settings_test.cpp` | `settingsSearchKeepsMatchingCategoriesAndTheirAncestors` | C | 补 WS/quick_settings_test.cpp 的设置页行为。嵌套祖先、空结果全部隐藏与清空恢复全 ID 集。Quick 只 delay/位置普通搜索，缺完整往返。 |
| `WS/pane_settings_test.cpp` | `shortcutsUseOneTabAndEditTheCommandRegistry` | C | 补 WS/quick_settings_test.cpp 的设置页行为。快捷键页唯一性、精确搜索、应用及冲突禁用。Quick shortcutBindingsValidateAndPreserveDrafts 覆盖编辑/冲突，未测重复打开仅一页。 |
| `WS/pane_settings_test.cpp` | `singleTabPoliciesHaveIndependentSettingsAndUpdateTheirWindows` | C | 补 WS/quick_settings_test.cpp 的设置页行为。两类窗口策略独立编辑实际栏、外部回显无回声且内容不变。Quick viewModesShapeTheQuickWindow 仅浮窗隐藏，缺双控件联动。 |
| `WS/pane_settings_test.cpp` | `titleWidthControlsKeepFocusDuringContinuousInput` | C | 补 WS/quick_settings_test.cpp 的设置页行为。工作台/浮窗×普通/侧栏连续逐键、退格、步进、无关更新时焦点/光标/查询保持。Quick 现有输入例未检查连续编辑状态。 |
| `WS/pane_settings_test.cpp` | `titleWidthUpdatesKeepScrollPositions` | C | 补 WS/quick_settings_test.cpp 的设置页行为。输入/步进/拖动时页偏移保持且活动标签完整可见。Quick 无该组合，几何用于可读/可操作契约，不能按外观删除。 |
| `WS/pane_settings_test.cpp` | `titleWidthUpdatesKeepTheSettingsPageMounted` | C | 补 WS/quick_settings_test.cpp 的设置页行为。多次即时改宽/持续拖滑块，页面和分组仍挂载、拖动不中断。Quick 滑块例不覆盖设置更新重同步。 |
| `WS/pane_settings_test.cpp` | `wheelNeverEditsAnyValueControl` | C | 补 WS/quick_settings_test.cpp 的设置页行为。Widgets 应用级规则涵盖多类控件、不抢焦点且滚动条/弹出列表正常滚。Quick 控件/输入路径不同，须先审定适用场景并补行为覆盖。 |
| `WS/pane_settings_test.cpp` | `wheelOverValueControlsScrollsThePageWithoutEditing` | C | 补 WS/quick_settings_test.cpp 的设置页行为。真实滚轮经过下拉/数值时页面双向滚动而值不变，键盘仍可编辑。Quick 设置例缺真实滚轮路由。 |
| `WS/peek_size_test.cpp` | `aSynchronousSaverCanDeleteTheManager` | A | 原套件保留，迁 External。保存通知内删除管理器，API 返回及保存模型方向策略正确，只依赖管理器/序列化。 |
| `WS/peek_size_test.cpp` | `autoAxisCommandsPreserveViewportStretch` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。命令零值为自动轴，换边后方向偏好独立且实际拉满自动轴。Quick 对话框空高度例不是完整命令/换边链。 |
| `WS/peek_size_test.cpp` | `bothDimensionsAndCornerResizingWorkOnEverySide` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。四边角拖双轴、同一编辑内容、固定 extent 不变，保存恢复实际尺寸。Quick 单主轴拖动未覆盖。 |
| `WS/peek_size_test.cpp` | `commandsUseTheClickedTabAndStayOutOfOtherContexts` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。实际菜单 action 显隐/可用与命令作用于被点侧栏标签，固定/中央上下文拒绝。Quick 无完整尺寸菜单上下文。 |
| `WS/peek_size_test.cpp` | `defaultPanelsStretchOneAxisAndUseUniformCompactMargins` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。混有 margin/手柄宽外观，但同时验证四边拉满一轴、副轴拖动保存/重开。功能不可随外观删，待审定并补四边副轴。 |
| `WS/peek_size_test.cpp` | `dialogCancellationAndTargetLossAreSafe` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。取消、目标关闭、固定和管理器销毁安全结束且不误写。Quick 改名目标关闭例未覆盖尺寸对话框这些分支。 |
| `WS/peek_size_test.cpp` | `directionalPoliciesRejectInvalidSizesWithoutReplacingContent` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。非法方向/尺寸/目标和同值不改快照/通知/活面板几何/内容。Quick 无整组活面板 API 原子性。 |
| `WS/peek_size_test.cpp` | `innerEdgeDraggingSavesOnlyTheActiveAxis` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。四边拖动只保存活动轴，另一轴自动、固定区不变，重开正文/身份保持。Quick 底边高度拖动仅子集。 |
| `WS/peek_size_test.cpp` | `invalidApiCallsPreserveTheLivePanelAndSnapshot` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。非法尺寸/API 不改活面板几何/身份/快照/信号，同值无通知。Quick 对话框约束不能代替完整 API 原子性。 |
| `WS/peek_size_test.cpp` | `invalidCommandArgumentsAreAtomic` | A | 原套件保留，迁 External。合并参数类型/边界行，带参拒绝时快照、信号、默认策略不变，无面板控件断言。 |
| `WS/peek_size_test.cpp` | `oppositeSidesShareSizesWithoutChangingTheOtherOrientation` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。左右共一策略、上下另一策略，换边实际显示/方向重置/持久化独立。Quick 单侧尺寸例未覆盖方向关联。 |
| `WS/peek_size_test.cpp` | `perTabPreferencesUpdateTheLivePanelWithoutRebuilding` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。同组两标签独立偏好，修改/重置活面板保留正文、焦点/身份。Quick 底边拖动无完整按标签偏好更新。 |
| `WS/peek_size_test.cpp` | `pinnedPartitionsUseIndependentExtents` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。四边固定 extent 与面板尺寸独立，解除/恢复固定、重置均保持各自状态。Quick 两类示例未覆盖完整往返。 |
| `WS/peek_size_test.cpp` | `preferencesMoveAndRestoreWithTheTab` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。策略随拆出/换边、恢复后实际显示，非法小数恢复须在建内容前拒绝并保留原内容。Quick 无完整持久化/非法事务。 |
| `WS/peek_size_test.cpp` | `queuedInputIsHarmlessUntilDeferredDeletion` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。管理器已亡、视图延迟删除间的按钮/标签/固定区/标题排队输入不改状态/不调用失效对象。Quick 无完整排队存活场景。 |
| `WS/peek_size_test.cpp` | `resizeCallbacksCanReplaceModelRecords` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。四边调整中 resize 可同步替换模型，最新策略/句柄有效。Quick 无重入，精确 handle 外观部分另待审定。 |
| `WS/peek_size_test.cpp` | `resizingContentCanDeleteTheManager` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。活面板 resize 内销毁管理器，只回调一次且保存已更新，随后内容/窗口释放。Quick 直接 view 销毁例缺完整宿主链。 |
| `WS/peek_size_test.cpp` | `shortcutOpensAKeyboardEditableSizeDialog` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。从内容快捷键开对话框，初始双轴默认文字、Tab/Enter 改双轴并实际显示。Quick panelSizeAcceptsOnlyUsableSizes 仅宽度，缺快捷键/双轴键盘。 |
| `WS/peek_size_test.cpp` | `shortenedPanelsFollowTheirTriggerAndClampToTheViewport` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。四边短面板跟随实际标签/滚轮并夹在视口、保持面板/内容。Quick 底边默认例无触发跟随。 |
| `WS/peek_size_test.cpp` | `viewportClampingPreservesTheRequestedSize` | C | 补 WS/quick_workspace_test.cpp 的侧栏尺寸行为。大请求只夹显示不改保存，缩放往返后从可见边缘拖动且只改活动轴。Quick 缺请求/显示分离覆盖。 |
| `WS/pinned_ui_test.cpp` | `createCommandsSplitPinnedGroupsInEveryDirection` | C | workspace.splitGroup.* 仍调用 Widgets 内容工厂及专用建组入口，与已确认的交接 Q2 相同；保留原四边×四方向数据及全部断言，待命令入口修复后重试。 |
| `WS/pinned_ui_test.cpp` | `dragGuidesSplitPinnedGroupsAndRemainReachableAfterAvoidance` | C | 补入 quick-workspace-drag。Quick普通组导向用例缺四边×四方向窄固定组避让后仍可达、中央组保持及原内容身份。 |
| `WS/pinned_ui_test.cpp` | `resizeCommandUsesCapturedPinnedGroup` | A | 迁至 Presentation::External。只执行捕获上下文的尺寸命令并比较模型固定树比例、中央树和当前标签，无实际控件交互。改建内容入口即可保留断言。 |
| `WS/pinned_ui_test.cpp` | `resizingANestedPinnedBoundaryLeavesOtherRegionsUnchanged` | C | 补入 quick-workspace。Quick的pinnedRegionsSitAroundTheCenterAndKeepTheirExtent只改外部分区尺寸，缺固定区内部嵌套分割比例变化时中央及相对侧区域不变。 |
| `WS/pinned_ui_test.cpp` | `restoringPinnedExtentsMayReplaceUiFromContentResize` | C | 补入 quick-workspace。Quick尚无内容缩放回调销毁管理器/关闭窗口/重建组或窗格树的覆盖，原例依赖QResizeEvent及QSplitter延后销毁语义，须按Quick生命周期补等价行为。 |
| `WS/pinned_ui_test.cpp` | `synchronousExtentSaversMayReplaceTheirOwningUi` | C | 补入 quick-workspace。Quick现有固定区拖动未在同步保存通知中关闭窗口/销毁管理器/重建停靠树，缺安全释放与其他窗口内容不受损的检查。 |
| `WS/popup_menu_test.cpp` | `cascadingMenusDoNotRemoveItemsOrToggleParentSeparators` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），下拉控件接线补入 quick-settings。Quick 子菜单更新用例不等价，缺少反复展开二/三级窗口菜单时父菜单项、活动入口、几何保持且分隔符/窗口项不产生多余变化的覆盖。内部调用次数断言请 Claude 审定。 |
| `WS/popup_menu_test.cpp` | `comboDropdownUsesTheAnchoredPopupPolicy` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），下拉控件接线补入 quick-settings。缺少 Quick 下拉框在屏幕边缘按触发区左右/上下回退、移到屏幕起点后恢复下方位置、弹窗关闭不改选项的覆盖。旧原生子窗口检查需 Claude 审定适用性。 |
| `WS/popup_menu_test.cpp` | `dropdownUsesItsActualScreen` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），下拉控件接线补入 quick-settings。Quick screenEdgeAndScrollingKeepTheLastCommandReachable 只断言菜单在屏内，缺锚定下拉按实际屏幕向右对齐/向上回退的精确位置覆盖。 |
| `WS/popup_menu_test.cpp` | `menuBarContextUsesCapturedGroupAndGlobalAppearanceCommands` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），下拉控件接线补入 quick-settings。Quick 菜单栏上下文仅验证 numbers 开关，缺少捕获组、图标/新建按钮/编号勾选、组关闭可用且修改全局图标不改组归属的覆盖。 |
| `WS/popup_menu_test.cpp` | `positionPrefersLeftBelowAndFallsBackRightAbove` | A | 原 popup-menu 定位逻辑用例保留，改为不建 Widgets 的共享/Quick 定位测试入口。纯 popupMenuPosition 输入输出表，覆盖左右/上下回退、负坐标屏幕、超大弹窗和屏外触发区，不依赖任何控件，保留全部预期坐标。 |
| `WS/popup_menu_test.cpp` | `rightClickUsesItsTargetWithoutActivatingIt` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），下拉控件接线补入 quick-settings。Quick workspaceMenusKeepSelectionAndCloseTheCapturedTab 有普通标签/侧栏/面板入口，但缺少已固定边缘组的非活动标签右击路径及 closeGuard 精确收到该标签，不能覆盖旧 pinned 数据行。 |
| `WS/popup_menu_test.cpp` | `tabBarContextExposesMultilineAndItsOwnPlacement` | C | 拟补 WS/quick_popup_menu_test.cpp（quick-popup-menu），下拉控件接线补入 quick-settings。缺少普通/已固定标签组上下文分别提供中央/侧栏独立图标与编号命令、多行/收起/滚轮依赖和各自 placement 独立更新的覆盖。 |
| `WS/presentation_test.cpp` | `checkboxTogglesWithSpace` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）、quick_popup_menu_test.cpp（quick-popup-menu）或 quick_toolbar_test.cpp（quick-toolbar）。Quick 设置页只通过鼠标切换开关，缺少实际复选框获得焦点后连续两次空格切换选中/取消的用例，普通 ButtonItem 无障碍动作不能代替。 |
| `WS/presentation_test.cpp` | `comboDefaultAndDetailsPreserveItemData` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）、quick_popup_menu_test.cpp（quick-popup-menu）或 quick_toolbar_test.cpp（quick-toolbar）。Quick windowSettingsEditTheBehavior 仅检查默认标记出现和选择应用，缺少默认项按 itemData 匹配、详情不改原文本、清除/未知默认值及无障碍描述清理的覆盖。 |
| `WS/presentation_test.cpp` | `comboOpensAndClosesItsPopup` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）、quick_popup_menu_test.cpp（quick-popup-menu）或 quick_toolbar_test.cpp（quick-toolbar）。Quick 设置页打开下拉后选值，缺少独立验证下拉显示后主动关闭且弹窗已释放的对等控件覆盖。 |
| `WS/presentation_test.cpp` | `popupOutsidePressDoesNotReplayAndReopen` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）、quick_popup_menu_test.cpp（quick-popup-menu）或 quick_toolbar_test.cpp（quick-toolbar）。Quick outsideClickOnlyDismissesAndReleasesCapture 验证普通内容收到抑制后的输入，缺少标签列表按钮上的外部按压释放不会重开菜单且按钮按压态清理的专门入口覆盖。 |
| `WS/presentation_test.cpp` | `sizingCommandChangesOnlyTheRequestedStripAndRejectsInvalidInput` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）、quick_popup_menu_test.cpp（quick-popup-menu）或 quick_toolbar_test.cpp（quick-toolbar）。混合 API 验证与阻塞 Widgets 选择器，Quick 缺无参数尺寸选择器只列可编辑栏、键盘选择与非法输入不改其余栏策略的完整覆盖。 |
| `WS/presentation_test.cpp` | `spinButtonsChangeTheValue` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）、quick_popup_menu_test.cpp（quick-popup-menu）或 quick_toolbar_test.cpp（quick-toolbar）。Quick 数字控件用例只键入值，缺少按上箭头按钮以 singleStep 修改值的实际输入覆盖。 |
| `WS/presentation_test.cpp` | `tabListButtonTogglesItsOpenMenu` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）、quick_popup_menu_test.cpp（quick-popup-menu）或 quick_toolbar_test.cpp（quick-toolbar）。Quick 已测标签列表选择，但缺少同一按钮连续点击打开/关闭/再打开，Esc 关闭的切换流程覆盖。 |
| `WS/shortcut_dispatch_test.cpp` | `additionalBindingArgumentsDriveCompositeCommands` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。不同绑定的复合实参顺序外，还要求字符不落入编辑器。Quick 单键上下文例覆盖不足。 |
| `WS/shortcut_dispatch_test.cpp` | `bindingSignalsCanDestroyTheManager` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。绑定变化同步通知销毁管理器，返回值与 QPointer 生命周期可保留。 |
| `WS/shortcut_dispatch_test.cpp` | `bindingValidationIsAtomicAndNotifiesOnlyChanges` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。注册表去重、非法操作原子性与实际变化通知，原位保留。 |
| `WS/shortcut_dispatch_test.cpp` | `callbacksCanMutateOrDestroyTheRegistry` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。enabled/execute 中扩展或销毁注册表的安全返回，无控件。 |
| `WS/shortcut_dispatch_test.cpp` | `capturedContextIsRecheckedAfterEnabledCallbacks` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。enabled 关闭捕获目标后重新验证，禁止执行失效命令且其他标签保持。 |
| `WS/shortcut_dispatch_test.cpp` | `changedBindingsAndInputMethodsCancelTheChord` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。改绑、输入法 preedit/commit 取消组合键，后续字符正常输入。Quick 假后端作用域例未测输入法事件联动。 |
| `WS/shortcut_dispatch_test.cpp` | `chordLengths` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。两/三/四击只末步执行、上下文正确且编辑器无字符。Quick 只有二击成功子例，缺完整长度与文本断言。 |
| `WS/shortcut_dispatch_test.cpp` | `compositeCommandsAbortWhenTheirSourceWindowDisappears` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。首步关源窗后终止后续，其他窗保留，模型与次数断言保持。 |
| `WS/shortcut_dispatch_test.cpp` | `compositeCommandsRetainTargetsAndUseIndividualArguments` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。复合命令完整目标和各步实参，只依赖 CommandRegistry。 |
| `WS/shortcut_dispatch_test.cpp` | `compositeDepthCapacityAndReentrantCyclesAreBounded` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。嵌套深度、容量边界、重入环与失败后恢复为纯命令逻辑。 |
| `WS/shortcut_dispatch_test.cpp` | `compositeFailureAndDisabledCommandsStopTheSequence` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。失败/禁用/缺失中间命令停止后续，检查执行顺序。 |
| `WS/shortcut_dispatch_test.cpp` | `compositeFirstStepRespectsItsCapturedContext` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。首步用捕获标签，后续用同窗新活动内容，纯上下文结果保持。 |
| `WS/shortcut_dispatch_test.cpp` | `compositesCloseTheNewlyFocusedGroup` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。空间焦点后关闭新组标签，QuickWorkspace 承载几何，目标模型断言保持。 |
| `WS/shortcut_dispatch_test.cpp` | `compositesUseNewActiveContentWithinTheirOriginalWindow` | C | External 下 pane.split.* 内置命令仍调用 QWidget 工厂，组合命令在分割步骤失败。原 Widgets 夹具与断言保留，产品缺陷交 Claude，见 [交接 Q2](../../development/handoff.md#请下一位回答)。 |
| `WS/shortcut_dispatch_test.cpp` | `directTabCommandsKeepTheirOwnContextContract` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。直接标签级命令与 executeInContext 完整目标验证各保留其契约。 |
| `WS/shortcut_dispatch_test.cpp` | `disabledBindingsDoNotReserveNativeShortcutInput` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。禁用命令不接受 ShortcutOverride、不启等待计时器，换 Quick 接收目标可保留 accepted/计时器状态。 |
| `WS/shortcut_dispatch_test.cpp` | `dispatchFollowsCatalogAndBindingChanges` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。增删目录/改绑/共享首键与删除后字符透传验证索引失效。Quick 单键/二击成功例无动态目录和编辑结果。 |
| `WS/shortcut_dispatch_test.cpp` | `enabledCallbacksCanDestroyTheInputReceiver` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。enabled 删除接收 ShortcutOverride 的 QWidget 子控件。Quick 在窗口分发，需明确等价接收项销毁入口后补生命周期。 |
| `WS/shortcut_dispatch_test.cpp` | `exactBindingWinsOverAnEarlierLongerPrefix` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。精确短键优先较早注册的长前缀，只断言次数，可从 Quick 窗口送键保留。 |
| `WS/shortcut_dispatch_test.cpp` | `findSurvivesRemovalInTheMiddle` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。纯注册表删除中间项后查询与目录顺序，原位保留。 |
| `WS/shortcut_dispatch_test.cpp` | `finishedOrCancelledChordsRestoreTheCurrentStatus` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。十种完成/取消恢复等待期间后台状态，命令消息可覆盖，含标签文字/提示和文本无泄漏。Quick 状态栏例只有成功组合键，缺完整恢复矩阵。 |
| `WS/shortcut_dispatch_test.cpp` | `focusAndWindowChangesCancelPendingChords` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。FocusOut/跨窗口送键取消等待，裸字进入目标编辑器。Quick 跨窗焦点例未覆盖半途组合键和文本透传。 |
| `WS/shortcut_dispatch_test.cpp` | `invalidEscapeTimeoutAndModifierInputDoNotLeak` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。修饰键、非法续键、Esc、超时分别控制执行和字符透传。Quick 无取消分支及编辑结果。 |
| `WS/shortcut_dispatch_test.cpp` | `malformedCompositeDescriptionsHaveNoSideEffects` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。全部错误描述须在首步前拒绝，纯注册表无副作用。 |
| `WS/shortcut_dispatch_test.cpp` | `multipleBindingsRetainArgumentsAndPrimaryCompatibility` | A | 原套件保留，纯注册表原位，管理器迁 External，按键/空间焦点按需接 QuickWorkspace。多绑定、主快捷键字段、实参与清空均为纯注册表契约。 |
| `WS/shortcut_dispatch_test.cpp` | `overrideReservesWithoutAdvancingAndAvoidsNativeShortcut` | C | 补 WS/quick_workspace_content_test.cpp 的组合键/可编辑内容路由。ShortcutOverride 不推进、原生 QShortcut 不执行与文本无泄漏。Quick 快捷键/编辑目标不同，需重建完整行为。 |
| `WS/shortcut_settings_test.cpp` | `captureAcceptsUnmodifiedContinuationLettersAndDigits` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。Quick keyRecorderKeepsBoundKeys 有裸字母与四键录入，缺数字、Tab 裸续键且从快捷键页应用后保留原绑定并增加第二绑定的完整接线。 |
| `WS/shortcut_settings_test.cpp` | `compositeArgumentsValidateAndRetainExecutionOrder` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。缺少 Quick 快捷键页 runCommands 绑定帮助、参数 JSON/命令存在性/args类型校验，成功执行严格保持命令顺序且非法草稿不覆盖已存绑定的覆盖。 |
| `WS/shortcut_settings_test.cpp` | `compositeBindingsCanBeAddedEditedAndRemovedIndependently` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。缺少 Quick 快捷键页多个 runCommands 绑定独立增改删、各自参数和顺序不互相覆盖、选中第一条修改/删除后第二条完整保留的覆盖。 |
| `WS/shortcut_settings_test.cpp` | `externalChangeDoesNotOverwriteAnUnfinishedBindingEdit` | B | WS/quick_settings_test.cpp::shortcutBindingsValidateAndPreserveDrafts。已读实际 Quick 断言。外部把绑定改为 F9 后草稿 F11 保持，Apply/Remove 均禁用，重新选择命令后读到 F9 并可删除，等价覆盖草稿不覆盖外部新版本及显式重载。 |
| `WS/shortcut_settings_test.cpp` | `multipleBindingsCanBeAddedEditedAndRemovedIndependently` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。Quick shortcutBindingsValidateAndPreserveDrafts 只成功增加一条绑定，缺第二条添加/选中编辑/删除时首条完整保留、旧快捷键别名正确恢复的覆盖。 |
| `WS/shortcut_settings_test.cpp` | `recordSearchFindsSequenceBindingsWithoutExecutingOrEditing` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。Quick shortcutKeySearchKeepsCommandsInactive 仅测一组双修饰键序列，缺前缀阶段同时显示两项、重新录制清空前序、裸 G/7 续键及清空搜索恢复全表的覆盖。 |
| `WS/shortcut_settings_test.cpp` | `secondaryBindingsAppearInSearchAndConflictChecks` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。Quick 已测首绑定冲突，但缺次级绑定参与文字搜索、列表显示及编辑另一命令时诊断该次级冲突的覆盖。 |
| `WS/sidebar_menu_test.cpp` | `peekCloseRemovesTheActualTab` | C | 补入 quick-popup-menu/quick-workspace。Quick的workspaceMenusKeepSelectionAndCloseTheCapturedTab测菜单关闭，未检查四边预览关闭按钮、中文提示、守卫调用目标和次数、实际内容及侧栏按钮释放。 |
| `WS/sidebar_menu_test.cpp` | `rejectedPeekCloseKeepsThePanelAndContent` | C | 补入 quick-popup-menu/quick-workspace。Quick尚无预览关闭按钮被守卫拒绝后保持面板、原编辑文字/对象身份及可见状态的覆盖。 |
| `WS/sidebar_menu_test.cpp` | `sidebarContextMenuCanPinTheClickedInactiveTab` | C | 补入 quick-popup-menu/quick-workspace。Quick右键菜单只测关闭，缺菜单保持原选择时固定被点击的非活动标签、同组另一标签仍隐藏和原内容身份。 |
| `WS/sidebar_menu_test.cpp` | `sidebarContextMenuUsesTheExistingTabCommands` | C | 补入 quick-popup-menu/quick-workspace。Quick的workspaceMenusKeepSelectionAndCloseTheCapturedTab只验证关闭，缺四边菜单命令清单、左右/其他/全部/拆出/固定命令可用性及不自动打开预览。 |
| `WS/sidebar_menu_test.cpp` | `tabListHoverExposesCloseWithoutChangingSelection` | C | 补入 quick-popup-menu/quick-workspace。Quick的tabListActionsFollowTheTabAcrossReordering仅等待一个操作按钮可见，缺跨行及按钮移动后关闭按钮隐藏/重现、选择不变。保留悬停行为，不按外观删除。 |
| `WS/sidebar_presentation_test.cpp` | `bulkFirstPinPreservesTheNewGroupOrder` | A | 迁至 Presentation::External。只检查pinEdge批量操作后模型edgeGroups顺序及反复取消固定/固定，Widgets内容只作输入，改用无控件内容描述即可保留全部断言。 |
| `WS/sidebar_presentation_test.cpp` | `firstPinUsesTheOuterPartition` | C | 补入 quick-workspace。Quick固定区用例缺四边通过预览按钮/固定组/全部固定/同组单项首次固定时的外层分区顺序和反复固定秩保持。 |
| `WS/sidebar_presentation_test.cpp` | `hiddenRailsDoNotOccupySpaceWhenExplicitlyOpeningATab` | C | 补入 quick-workspace。Quick没有侧栏隐藏后显式打开标签时预览可见、从停靠面左边界开始且不保留侧栏空间的覆盖，需保留防留空契约。 |
| `WS/sidebar_presentation_test.cpp` | `hidingKeepsPinnedContentAndCentralTabs` | C | 补入 quick-workspace。Quick缺工作台/浮窗侧栏开关对预览、固定内容、中央标签栏的独立影响，以及菜单勾选、快捷键和重建继承偏好。含Widgets专用QAction/QMenu，不能直接迁A。 |
| `WS/sidebar_presentation_test.cpp` | `visibilityRestoresAndRejectsMalformedSnapshots` | A | 迁至 Presentation::External + QuickWorkspace。管理器侧栏可见偏好的存取、非法类型拒绝和缺字段默认，读取Quick停靠面的edgeBarRects即可保持现有可见性与原子性断言。 |
| `WS/status_bar_test.cpp` | `commandItemsUseTheirOwnWindowAndSupportKeyboard` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。Quick 状态项仅测鼠标计数，缺多个窗口中实际命令上下文 window/tab、Tab可聚焦及空格触发命令的覆盖。截图分支请 Claude 审定。 |
| `WS/status_bar_test.cpp` | `contextMenuReflectsEachControlAndCommandsUpdateIt` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。Quick 状态栏用例只隐藏 message 一项，缺两项菜单完整列出、勾选状态随命令变化，以及再次打开菜单重新显示该项的覆盖。 |
| `WS/status_bar_test.cpp` | `controlVisibilityCallbacksMayDeferManagerDestruction` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。缺少状态项隐藏回调延后销毁管理器后，先完成偏好/可见性，再只释放一次实际栏和窗口并取消队列回调的覆盖。 |
| `WS/status_bar_test.cpp` | `controlsRemainIndependentOfWholeBarVisibility` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。缺少隐藏单项后整栏隐藏/显示仍保留单项偏好，其他项正常显示的实际 Quick 状态栏覆盖。 |
| `WS/status_bar_test.cpp` | `emptyCommandItemsPreserveVisibilityPreferences` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。缺少空文字状态项隐藏、菜单使用命令标题，文字清空/重新设置不改隐藏偏好且重新显隐按命令生效的覆盖。 |
| `WS/status_bar_test.cpp` | `heightFollowsThemeMetricAndFont` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。混合固定22/30像素静态高度与大字体不裁字的功能契约，先列 C，请 Claude 审定纯尺寸断言范围，Quick 需保留大字体文字完整可见的功能覆盖。 |
| `WS/status_bar_test.cpp` | `invalidIdsAndTypesDoNotMutatePreferences` | A | 原 status-bar 套件改用 External，保留管理器偏好与快照断言。纯管理器公开API非法ID/窗口/类型原子拒绝，比较保存快照不变，不需要实际状态栏。 |
| `WS/status_bar_test.cpp` | `menuActionsDoNotOutliveTheManager` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。缺少状态栏菜单可被外部持有时管理器销毁前后重复触发已保留菜单项都安全，且排队执行不越过管理器生命周期的覆盖。 |
| `WS/status_bar_test.cpp` | `optionsAndWindowKindChangesKeepTheSamePreference` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。缺少 WindowOptions 初始隐藏项经历浮窗/工作台多次转换后实际状态栏始终按同一偏好显示的覆盖。 |
| `WS/status_bar_test.cpp` | `preferencesRoundTripWithoutChangingOtherWindows` | A | 原 status-bar 套件改用 External，保留管理器偏好与快照断言。仅断言各窗口状态项可见偏好保存/恢复后相互独立且模型相等，改 External 和值式 TabOptions/内容工厂即可保留。 |
| `WS/status_bar_test.cpp` | `removedCommandItemsCancelQueuedClicks` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。缺少状态按钮点击排队后删除再用同一ID/命令注册新项不会复活旧点击，及管理器销毁取消排队命令的 Quick 覆盖。 |
| `WS/status_bar_test.cpp` | `statusContentReflectsMessagesAndCurrentTabs` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的状态栏用例。Quick 已验证初始标题含 tab 名和消息文本，但未验证切换两个活动标签后完整中文标签数量文本随之更新。 |
| `WS/sticky_tabs_test.cpp` | `activatingAScrolledTabRevealsItPastTheStickyTabs` | C | 补入 quick-group/quick-workspace-drag。Quick未验证激活被置顶冻结区遮住的标签后完整显示、保留清晰范围，以及选择置顶标签不引发滚动。 |
| `WS/sticky_tabs_test.cpp` | `blockedCloseShortcutShowsAnotherGroupsTab` | C | 补入 quick-workspace-content。Quick未验证关闭受保护置顶标签的快捷键改激活另一组普通标签，以及全为置顶时保持原选择。 |
| `WS/sticky_tabs_test.cpp` | `closeProtectionFollowsTheSetting` | C | 补入 quick-workspace-content/quick-tab-bar-input。Quick未覆盖四种置顶关闭保护、关闭标签/关闭窗格快捷键、中键关闭及显式命令可关闭的组合。 |
| `WS/sticky_tabs_test.cpp` | `collapsedRowsKeepTheStickyRowShown` | C | 补入 quick-group/quick-workspace-drag。Quick未覆盖收起模式仍显示置顶行与活动普通行、选择置顶保留普通行，以及滚轮展开所有标签。 |
| `WS/sticky_tabs_test.cpp` | `compactStickyTabsShowTheirIconOrInitialWithoutAPin` | C | 补入 quick-group/quick-workspace-drag。Quick缺紧凑置顶尾端点击只选择、不误触解除置顶。图标颜色、居中、16像素及首字母像素检查需Claude审定。 |
| `WS/sticky_tabs_test.cpp` | `draggingBetweenRowsSticksAndUnsticks` | C | 补入 quick-group/quick-workspace-drag。Quick拖动缺在独立置顶行和普通行之间拖放改变置顶状态与最终索引的覆盖。 |
| `WS/sticky_tabs_test.cpp` | `draggingIntoAndOutOfASizedRunUsesEachSegmentsWidth` | C | 补入 quick-group/quick-workspace-drag。Quick真实拖动缺进出紧凑置顶段时预览宽度随段切换、提交顺序和sticky标志匹配。 |
| `WS/sticky_tabs_test.cpp` | `fullStickyRowsDoNotMakeTheStripScroll` | C | 补入 quick-group/quick-workspace-drag。Quick未验证置顶多行及新建控件出现后所有标签可达且不产生无效溢出。固定相邻/行高断言需Claude审定。 |
| `WS/sticky_tabs_test.cpp` | `separateRowHoldsStickyTabsAboveTheOthers` | C | 补入 quick-group/quick-workspace-drag。Quick未验证独立置顶行的命中/拖动分段、两行独立滚动及多行换行组合。固定行高/排列部分需Claude审定。 |
| `WS/sticky_tabs_test.cpp` | `shrunkStickyTitlesUseTheirOwnLimit` | C | 补入 quick-group/quick-workspace-drag。Quick未测置顶缩短标题限制与普通裁剪独立，以及置顶段插入预览采用同一尺寸。固定宽度/相邻部分需Claude审定。 |
| `WS/sticky_tabs_test.cpp` | `sizedStickyTabsStayInViewWhileOthersScroll` | C | 补入 quick-group/quick-workspace-drag。Quick缺紧凑/缩短置顶段冻结滚动、点击遮挡优先级和插入索引，以及普通置顶/多行不冻结的组合。 |
| `WS/sticky_tabs_test.cpp` | `sizedStickyTabsUseTheirWidths` | C | 补入 quick-group/quick-workspace-drag。断言主要为三种置顶尺寸、相邻关系和解除置顶后的固定宽度，未确认独立功能契约。先列C交Claude按静态外观规范审定，本步保留。 |
| `WS/sticky_tabs_test.cpp` | `tabActionTooltipsDescribeTheAction` | C | 补入 quick-popup-menu/quick-tab-bar-input。Quick的hoverTipsFollowThePointer只检查普通标签标题和新建按钮，缺置顶尺寸/行模式组合中关闭与解除置顶操作提示。 |
| `WS/sticky_tabs_test.cpp` | `tabListCanUnstickWithoutActivatingOrClosing` | C | 补入 quick-popup-menu。Quick的tabListActionsFollowTheTabAcrossReordering仅检查取消置顶结果，缺活动/非活动两分支的选择、邻标签置顶状态、最终顺序、内容身份及重新打开列表。 |
| `WS/sticky_tabs_test.cpp` | `tabListMarksStickyTabs` | C | 补入 quick-popup-menu。Quick未完整检查列表每行是否提供解除置顶操作、提示/无障碍名称以及独立关闭仍关闭原标签。固定尺寸和对齐需Claude审定。 |
| `WS/sticky_tabs_test.cpp` | `tabListUnstickResolvesItsTargetAfterPopupCallbacks` | C | 补入 quick-popup-menu。Quick对应列表用例在unstick分支不重排，未覆盖弹窗关闭回调同步删除前项后仍按原稳定ID解除置顶且不误关闭。 |
| `WS/strip_drag_test.cpp` | `authoredMouseStreamPreservesInputButtonsKeyboardAndTimers` | C | 补入 quick-tab-bar-input，夹具契约待Claude审定。用例直接依赖Widgets authoredMouseInput过滤来源，Quick未有同等屏蔽/恢复QPA移动同时保留按钮、键盘和计时器的检查。 |
| `WS/strip_drag_test.cpp` | `capturedDragWheelScrollsTheHoveredStrip` | C | 补入 quick-workspace-drag。Quick的aDragScrollsTheStripUnderThePointer只有中央栏单次滚动，缺未拆出/已拆出×固定/侧栏分支、像素与角度输入、选择/捕获保持及取消后视口恢复。 |
| `WS/strip_drag_test.cpp` | `capturedWheelMayDestroyTheManager` | C | 补入 quick-workspace-drag。Quick滚轮拖动只检查偏移，缺滚动通知中销毁管理器后释放鼠标捕获和拖动预览的重入场景。 |
| `WS/strip_drag_test.cpp` | `detachmentNotificationMayDestroyTheManager` | C | 补入 quick-workspace-drag。Quick缺拆出layoutChanged通知中立即销毁管理器时窗口、内容、预览及鼠标捕获全部安全释放。 |
| `WS/strip_drag_test.cpp` | `firstExitCollapsesTheEmptySourceGroup` | C | 补入 quick-workspace-drag。Quick的tabsDragWithinAndAcrossGroups验证拆空后剩组填满，但没有Esc恢复被回收原组ID、两组数量及源控件释放的完整生命周期。 |
| `WS/strip_drag_test.cpp` | `firstExitCreatesACarrierAndReentryKeepsIt` | C | 补入 quick-workspace-drag。Quick有再进入演示，但缺中央/固定/自动隐藏组合下同一载体和内容所属保持、原预览回收及Esc完整模型恢复。 |
| `WS/strip_drag_test.cpp` | `leavingAnAutoScrolledStripKeepsItsViewport` | C | 补入 quick-workspace-drag。Quick缺自动滚动后离开源条使偏移保持、停止自动滚动，以及取消回归后中央/固定/自动隐藏各位置锚点。 |
| `WS/strip_drag_test.cpp` | `liveContentStaysInPlaceUntilFirstExit` | C | 补入 quick-workspace-drag。Quick没有中央/固定/自动隐藏及预览开启/关闭组合中首次退出前内容父对象/几何/显隐与模型均不变、Esc清理预览的完整检查。 |
| `WS/strip_drag_test.cpp` | `nestedTargetPickerMayLoseItsCarrierOrManager` | C | 补入 quick-workspace-drag。Quick的tabDuringADragChoosesTheTargetWindow测正常选择，缺目标选择器可见时关闭载体/销毁管理器、弹窗自动退出及剩余模型/内容生命周期。 |
| `WS/strip_drag_test.cpp` | `queuedScrollRestoreYieldsToKeyboard` | C | 补入 quick-rail。Quick滚动用例缺四边侧栏排队恢复遇Home/End/方向键时让位于新输入，原断言还绑定QScrollBar键盘语义，需确认Quick相同行为。 |
| `WS/strip_drag_test.cpp` | `releaseInTheOriginalStripCommitsAnEndpointOrder` | C | 补入 quick-workspace-drag。Quick同条排序只有中央及左侧栏部分场景，缺首尾双向×中央/四边固定/四边自动隐藏组合和内容身份/捕获清理。 |
| `WS/strip_drag_test.cpp` | `releasingWithinTheOriginalLabelKeepsItsOrderAndNeighbours` | C | 补入 quick-workspace-drag。Quick缺标签内刚达到拖动阈值就释放时原插入槽、邻项、内容位置、完整模型及窗口数量保持的边界。 |
| `WS/strip_drag_test.cpp` | `sourceGeometryChangesRefreshThePhaseWithoutMouseMovement` | C | 补入 quick-workspace-drag。Quick尚未覆盖无新鼠标移动时源窗口移动/缩小触发首次退出、开始拆出且原内容继续由载体持有。 |
| `WS/strip_drag_test.cpp` | `stationaryStripDragAutoScrolls` | C | 补入 quick-workspace-drag。Quick的aDragScrollsTheStripUnderThePointer只测滚轮，缺中央和四边侧栏原地静止拖动自动滚动、可见空位、模型不变及取消后停止。 |
| `WS/strip_drag_test.cpp` | `tearOutPreservesSourceScroll` | C | 补入 quick-workspace-drag。Quick缺中央/四边固定/四边自动隐藏标签拆出、取消后保留源视口偏移，且显式选首项仍可导航显示的组合。 |
| `WS/strip_drag_test.cpp` | `wheelOnAPeekDoesNotScrollTheCoveredStrip` | C | 补入 quick-workspace-drag。Quick尚未覆盖预览遮住标签栏时拒绝滚动其底层，以及组全屏后隐藏原标签栏不接收滚轮。 |
| `WS/surface_transfer_test.cpp` | `aFloatingWindowCanTransferItsLastTabAndBeReleased` | C | 补 quick-workspace 的末标签跨窗与释放。旧例前提是浮窗已建但隐藏，Quick 默认投影即显示，需审定前置条件及释放等待方式 |
| `WS/surface_transfer_test.cpp` | `edgeTransfersLeaveNoCallbacksIntoReleasedSurfaces` | A | External + QuickWorkspace，每次移入移出后完成投影。保留四边反复转移并排空延迟销毁，确保实际 Quick 表面经历释放 |
| `WS/tab_appearance_test.cpp` | `appearanceAndEveryPaletteStateRoundTrip` | A | 原 tab-appearance 套件改用 External，保留配置、命令及持久化断言。管理器配置值、变化信号次数、各配色状态结构以及遗漏字段默认恢复均与控件无关，保留原断言。 |
| `WS/tab_appearance_test.cpp` | `defaultsHaveSeparatePlacementAndConservativeFlags` | A | 原 tab-appearance 套件改用 External，保留配置、命令及持久化断言。仅读取管理器默认策略、独立 placement/标志、配色预设数据与查找结果，改 External 后保留全部值断言。 |
| `WS/tab_appearance_test.cpp` | `explicitPaletteValuesRoundTripWithBuiltinIds` | A | 原 tab-appearance 套件改用 External，保留配置、命令及持久化断言。验证配色配置值反复序列化、保存恢复保真，内置ID不覆盖用户数据，不依赖呈现。 |
| `WS/tab_appearance_test.cpp` | `flagCommandsUseSameAppearanceAndValidateArguments` | A | 原 tab-appearance 套件改用 External，保留配置、命令及持久化断言。纯命令/API共享配置、严格参数类型、多行与滚轮状态依赖、独立placement及序列化保持的行为，改 External 可保留。 |
| `WS/tab_appearance_test.cpp` | `invalidAppearanceIsAtomic` | A | 原 tab-appearance 套件改用 External，保留配置、命令及持久化断言。解析/恢复非法布局配置必须不变更快照且零通知，保留包括每个非法类型、枚举和颜色状态的数据行。 |
| `WS/tab_appearance_test.cpp` | `invalidPublicAppearanceDoesNotEmitOrMutate` | A | 原 tab-appearance 套件改用 External，保留配置、命令及持久化断言。公开外观配置API的非法枚举、ID、配色、宽度边界原子拒绝与零通知，改 External 保留全部数据行。 |
| `WS/tab_appearance_test.cpp` | `paletteApplyAllowsSynchronousHostDestruction` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。缺少 Quick 配色应用发 layoutChanged 时同步销毁设置页/管理器两路径，保存已完成配色且页面/按钮完整释放的生命周期覆盖。 |
| `WS/tab_appearance_test.cpp` | `settingsExposeEachFlagAndOnlyEnableWheelExpansionInMultiline` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。Quick tabSettingsKeepDependentValues 只测部分多行/置顶依赖，缺中央/固定placement独立设置、滚轮展开收起依赖及单行保留选中值、全部图标/新增/编号开关双向同步。 |
| `WS/tab_appearance_test.cpp` | `settingsPaletteEditorValidatesBeforeApplyingAndKeepsDraft` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。Quick tabColorsApplyEditedJson 已有非法草稿与外部修改保护，但缺替换配色ID集合后旧ID消失、新ID可查且应用不覆盖无关 showNumbers 的完整断言。 |
| `WS/tab_appearance_test.cpp` | `sidebarFlagsHaveIndependentDefaultsPersistenceCommandsAndControls` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。混合配置/命令和 Widgets 控件断言，Quick 缺侧栏图标/编号控件独立于中央设置的双向接线及保存默认/非法输入全过程，不能整例直接归A。 |
| `WS/tab_appearance_test.cpp` | `sidebarTitleClippingControlsStayIndependent` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。缺少中央与侧栏两套裁切控件各自开关/宽度、命令更新、不活动宽度保留、边界及保存恢复的实际 Quick 页面覆盖。 |
| `WS/tab_appearance_test.cpp` | `sidebarTitleClippingIsIndependentInLayoutSnapshots` | A | 原 tab-appearance 套件改用 External，保留配置、命令及持久化断言。独立中央/侧栏标题裁切设置、边界值、保存恢复和可选字段默认值，只涉及配置和管理器快照。 |
| `WS/tab_appearance_test.cpp` | `stickySizingRoundTripsThroughJsonAndLayout` | A | 原 tab-appearance 套件改用 External，保留配置、命令及持久化断言。纯枚举/尺寸策略序列化、遗漏字段默认、布局往返和无效枚举拒绝，管理器改 External 即可保留。 |
| `WS/tab_appearance_test.cpp` | `titleClippingRetainsWidthAndDefaultsOmittedFields` | C | 拟补 WS/quick_settings_test.cpp（quick-settings）。Quick 已测主标题裁切宽度依赖，但缺开关往返保留宽度、上下边界、布局恢复与遗漏字段默认值反映回同一设置控件的完整覆盖。 |
| `WS/tab_bar_presentation_test.cpp` | `compactIconsLeaveARealCloseTarget` | C | 补入 quick-tab-bar-input。Quick的inputsAndFramesDriveTheSharedController只测中键，缺带图标紧凑标签的真实左键关闭区域命中及不发选择变化。 |
| `WS/tab_bar_presentation_test.cpp` | `departingFramesSurviveTheirSourceStrip` | C | 补入 quick-tab-presentation/quick-foreground-resources。Quick已有源销毁后已发布帧保留，但未覆盖四方向标签删除离场帧、动画终止与源销毁后标题/几何数据仍有效。 |
| `WS/tab_bar_presentation_test.cpp` | `hiddenSettingRestoresAndSurvivesNewGroups` | C | 补入 quick-workspace/quick-group。Quick缺保存隐藏偏好后新中央组/固定组/侧栏继承，重开侧栏预览和菜单勾选同步。原例依赖QMenu动作，需补Quick菜单断言。 |
| `WS/tab_bar_presentation_test.cpp` | `hidingAppliesToTheWholeWindow` | C | 补入 quick-workspace/quick-group。Quick的viewModesShapeTheQuickWindow只测中央栏开关，缺工作台/浮窗、固定标题和侧栏预览独立保持，以及内容身份和完整模型不变。 |
| `WS/tab_bar_presentation_test.cpp` | `invalidPresentationSettingsLeaveTheLayoutUnchanged` | C | 补入 quick-workspace/quick-group。包含非法PaneBarSharing时未接管调用方QLabel及父对象不变，不能仅切External保留Widgets接管断言。需在quick-workspace-content补当前内容接口的校验和布局原子性，Widgets专属部分交Claude审定。 |
| `WS/tab_bar_presentation_test.cpp` | `lastClosePublishesTheFinalGeometryBeforeRemoval` | A | 迁至 Presentation::External + QuickWorkspace。通过窗口位置和管理器关闭请求检查最后关闭通知内保存的坐标尺寸及模型移除，改取Quick窗口即可保留原行为断言。 |
| `WS/tab_bar_presentation_test.cpp` | `newTabControlsCreateTabsAndManageTheirMenu` | C | 补入 quick-group/quick-popup-menu。Quick新建类型菜单检查动态命令，缺普通新建、两按钮共同悬停、菜单打开/关闭状态和载体期间控件显隐。 |
| `WS/tab_bar_presentation_test.cpp` | `scrollbarDraggingDoesNotSelectTabs` | C | 补入 quick-tab-bar-input。Quick输入用例未拖动滚动条，缺滚动条拖动改变偏移但不选标签、隐藏标签栏后视口为空。 |
| `WS/tab_bar_presentation_test.cpp` | `singleTabDefaultsFollowWindowKindAndContentCount` | C | 补入 quick-workspace/quick-group。Quick只测浮窗单项隐藏，缺两种窗口默认、增项/分组/关闭后的整窗计数、原内容身份和实际可见区域不留空。 |
| `WS/tab_bar_presentation_test.cpp` | `singleTabPoliciesNeverOverrideExplicitHidingOrEdgeHeaders` | C | 补入 quick-workspace/quick-group。Quick缺隐藏策略与显式开关优先级、含自动隐藏标签的整窗计数、固定标题/预览标题始终保留及原内容可见性。 |
| `WS/tab_bar_presentation_test.cpp` | `singleTabVisibilityFollowsPromotionAndRestore` | C | 补入 quick-workspace/quick-group。Quick缺窗口提升/降级和两种单标签策略保存恢复后实际标签栏显隐及原内容身份保持。 |
| `WS/tab_bar_presentation_test.cpp` | `tabListMarksTheActiveTabWithoutChangingSelection` | C | 补入 quick-popup-menu。Quick列表动作只测执行结果，缺每行活动标记/勾选状态、悬停强调与当前项保持，不能按静态外观整例删除。 |
| `WS/tab_carrier_source_test.cpp` | `metadataFontAndLayoutInvalidateOwnedContent` | C | 补入 quick-tab-presentation/quick-carrier-resources。Quick的stoppedWidgetPublishesToQuickAndCanBeDestroyed只修改标题和拖动，缺标签ID/选择/字体/布局变更使owned content失效且旧内容保留。 |
| `WS/tab_carrier_source_test.cpp` | `queuedPublicationsCoalesceAndIgnoreAnOldPresentation` | C | 补入 quick-tab-presentation/quick-carrier-resources。Quick尚未验证停靠面载体发布排队合并、呈现世代切换忽略旧请求以及关闭载体呈现后不再发布。 |
| `WS/tab_carrier_source_test.cpp` | `releasedFrameBuffersAreReused` | C | 补入 quick-tab-presentation/quick-carrier-resources。Quick资源用例检查原生资源重用，没有验证发布来源在100次sample变化中帧缓冲有界复用且旧快照不变，需保留资源契约。 |
| `WS/tab_carrier_source_test.cpp` | `stoppedWidgetPaintingDoesNotStopStateNotifications` | C | 补入 quick-tab-presentation/quick-carrier-resources。Quick的stoppedWidgetPublishesToQuickAndCanBeDestroyed检查presentationInvalidated和拖动前景，原例为carrierInvalidated及同content样本更新，未确认同一发布通道等价。 |
| `WS/tab_carrier_source_test.cpp` | `unchangedReadsShareFramesAndSamplesDoNotCopyContent` | C | 补入 quick-tab-presentation/quick-carrier-resources。Quick资源用例消费人工构造帧，未验证真实来源不变读取共用帧、仅sample变化不复制content且旧帧保持不可变。 |
| `WS/tab_creation_test.cpp` | `catalogValidationIsAtomicAndDisabledCommandsDoNotCreate` | A | External，保留新建命令目录规则。目录原子性、禁用命令、重复选择、缺失目标及单次执行是管理器行为 |
| `WS/tab_creation_test.cpp` | `creationCanStartInAnEmptyWindow` | A | External，按描述创建内容。空窗口上下文为空、后续新建进入现有组的状态可直接保留 |
| `WS/tab_creation_test.cpp` | `creationUsesClickedGroupInsteadOfFocusedGroup` | C | 补 quick-popup-menu / quick-workspace 的默认新建按钮入口。newTabTypesRefreshAndUseTheirButtonGroup 仅验证类型菜单，未覆盖默认按钮、目标内容上下文及组计数的全部断言 |
| `WS/tab_creation_test.cpp` | `enabledCallbackMayDestroyManagerAndPopupParent` | C | 补 quick-popup-menu 的新建菜单查询中销毁管理器。旧例直接调用 GroupTabs 的菜单回调并销毁 manager，现有 Quick 查询销毁 workspace 覆盖对象不同 |
| `WS/tab_creation_test.cpp` | `executionMayDestroyManager` | A | External 管理器，不建不必要窗口视图。执行回调销毁管理器后的返回值及 QPointer 失效为命令生命周期契约 |
| `WS/tab_creation_test.cpp` | `explicitContextRejectsStaleTargetsAndDeletionDuringQuery` | A | External，保留显式上下文执行规则。过期窗格／窗口／组和查询中销毁 manager 的保护无需 Widgets |
| `WS/tab_creation_test.cpp` | `explicitIndexOverridesBehaviorAndModelStillAppends` | A | External 与现有 LayoutModel 部分。显式索引、默认追加和非法索引快照为模型／管理器行为 |
| `WS/tab_creation_test.cpp` | `invalidInsertionPreservesOwnershipAndSnapshot` | C | 审定内容重载移除后的归属，核对 quick-content-host。旧例向 manager.addTab 传 QWidget，新的描述接口没有这一所有权边界，需审定原子性与内容工厂覆盖去向 |
| `WS/tab_creation_test.cpp` | `invalidSettingRejectedBeforeFactory` | A | External + 必要 QuickWorkspace 工厂夹具。保留非法设置拒绝、工厂零调用及快照不变 |
| `WS/tab_creation_test.cpp` | `positionFollowsActiveTab` | A | External，保留标签位置规则。按描述建标签即可核对插入位置、活动标签与模型有效性 |
| `WS/tab_creation_test.cpp` | `settingRoundTripsAndOmissionDefaultsToAfterActive` | A | External，保留当前格式存取断言。插入策略存取、缺省值及恢复后插入位置不依赖控件 |
| `WS/tab_creation_test.cpp` | `staleTargetFromEnabledCallbackCannotCreateElsewhere` | A | External，保留回调撤销目标的原子性。查询期间删除目标组后拒绝执行，不依赖窗口控件 |
| `WS/tab_group_test.cpp` | `adjacentCommandDoesNotDeleteContentTwice` | C | 补入 quick-workspace-content/quick-popup-menu。QWidget内容在接管属性事件中自删，原断言绑定此接管入口；Quick需以实际内容创建/挂载回调复现防双重释放，并保留失败时模型和revision不变。 |
| `WS/tab_group_test.cpp` | `adjacentCommitReturnsItsStableIdAfterNotification` | C | 补入 quick-workspace-content/quick-popup-menu。原例从被接管内容的paneId属性取得提交ID，并在同步layoutChanged关闭新项/销毁管理器。Quick内容创建和通知时序不同，需确认或补同等稳定ID/释放次数行为。 |
| `WS/tab_group_test.cpp` | `adjacentPreparationHonorsParticipantLifetimes` | C | 补入 quick-workspace-content/quick-popup-menu。原例在接管QWidget时修改windowSystemManaged/paneId触发事件，验证调用方父对象、focusPolicy和原属性回滚。Quick内容由另一路工厂创建，不能只换窗口保留接管阶段断言。 |
| `WS/tab_group_test.cpp` | `adjacentPreparationPreservesARestoredModelWithTheSameRevision` | C | 补入 quick-workspace-content/quick-popup-menu。原例通过QWidget接管属性回调恢复同revision不同模型，Quick无相同接管入口。需保留模型代际被替换时新增组不能覆盖恢复结果的独立覆盖。 |
| `WS/tab_group_test.cpp` | `adjacentPreparationPreservesReentrantModelChanges` | C | 补入 quick-workspace-content/quick-popup-menu。重入来自QWidget接管时的动态属性事件，External模型操作没有相同入口，需Quick内容工厂/挂载重入保持已提交模型及调用方所有权的等价行为。 |
| `WS/tab_group_test.cpp` | `autoHideRailDoesNotAdvertiseHiddenInternalGroupSort` | A | 迁至 Presentation::External。仅检查自动隐藏组的排序命令拒绝执行且保存布局不变，无实际侧栏事件或外观断言，可迁External。 |
| `WS/tab_group_test.cpp` | `cancelColorPickerDoesNotChangeLayout` | C | 补入 quick-workspace-content/quick-popup-menu。Quick的pickersApplyToTheTabTheyStartedOn测正常选色，未验证无参数选色取消不改布局、原颜色及layoutChanged次数。原QMenu同步执行语义需按Quick异步菜单补行为。 |
| `WS/tab_group_test.cpp` | `canceledNamedCollectionLayerRegistersNoCommandsOrStorage` | A | 迁至 Presentation::External。只枚举命令并检查保存JSON没有已取消的collection字段，不依赖Widgets，可迁External保留。 |
| `WS/tab_group_test.cpp` | `collectingGroupsLeavesThePeekClosed` | C | 补入 quick-workspace。管理器收拢整组后真实预览关闭、内容隐藏及显式重开可用为混合呈现契约，Quick缺四边×窗口类型×原固定状态的等价覆盖，需补quick-workspace。 |
| `WS/tab_group_test.cpp` | `colorCommandsRejectUnknownPresetsAndPersistSelections` | A | 迁至 Presentation::External。只检查管理器颜色命令、未知预设拒绝和清除后的保存结果，颜色值为配置传递契约，使用External即可保留。 |
| `WS/tab_group_test.cpp` | `colorSelectionSurvivesLayoutRestart` | A | 迁至 Presentation::External + QuickWorkspace。颜色、组ID、恢复内容工厂调用一次及内容存在为保存恢复契约，QuickWorkspace可承接内容工厂和查询，保留原检查值。 |
| `WS/tab_group_test.cpp` | `detachedGroupsInheritTheDestinationKindVisibilityDefaults` | A | 迁至 Presentation::External + QuickWorkspace。按目标窗口类型继承栏显隐偏好以及组/窗格/内容身份稳定，管理器API和Quick内容查询即可保留全部语义。 |
| `WS/tab_group_test.cpp` | `directionalGroupExchangeWrapsWithinTheVisibleGroupLayer` | A | 已按审定要求试迁 External，中央组的四向循环交换使用实际 Quick 停靠区几何，保留稳定顺序及模型有效性断言。 |
| `WS/tab_group_test.cpp` | `directionalMergeIncludesPinnedGroupsAndAppendsInStableOrder` | A | 已按审定要求试迁 External，四向合并包含固定组，使用实际 Quick 停靠区几何，保留追加顺序、活动标签及内容身份断言。 |
| `WS/tab_group_test.cpp` | `exchangeAndMergeCommandsUseExplicitTargetWithoutRecreatingContent` | A | 迁至 Presentation::External + QuickWorkspace。显式目标的捕获上下文交换/合并、稳定顺序及内容身份均为管理器契约，Quick替换内容供给与查询即可。 |
| `WS/tab_group_test.cpp` | `fixedGroupsParticipateInExplicitReparentAndDetachCommands` | A | 迁至 Presentation::External + QuickWorkspace。固定组、中央组及自动隐藏组经显式命令重挂/拆出，断言仅模型、上下文和内容身份，可用Quick承接内容并保持语义。 |
| `WS/tab_group_test.cpp` | `groupCloseRespectsGuardAndCopiedMembership` | A | 迁至 Presentation::External。关闭守卫接收复制成员列表、拒绝无改变及守卫重入改色后关闭，全部为管理器模型契约，可迁External。 |
| `WS/tab_group_test.cpp` | `groupMovementAndCloseUseTheCapturedGroup` | A | 迁至 Presentation::External。只经管理器命令移动/关闭捕获组并检查模型归属及成员，可使用External内容描述，保留模型预期。 |
| `WS/tab_group_test.cpp` | `groupMovesPreserveAllLiveWidgets` | A | 迁至 Presentation::External + QuickWorkspace。管理器移动组/跨窗API后组ID、成员及内容对象身份不变，改用Quick内容对象和查询，保留身份语义。 |
| `WS/tab_group_test.cpp` | `groupTargetArgumentsRejectInvalidNumbersAtomically` | A | 迁至 Presentation::External。非法数字参数须原子拒绝、布局及变更通知数不变，纯管理器命令契约，使用External保留输入数据表和断言。 |
| `WS/tab_group_test.cpp` | `groupTargetPickerCancellationDoesNotChangeTheLayout` | C | 补入 quick-workspace-content/quick-popup-menu。原例覆盖swap/merge/moveToGroup无参数目标菜单取消，Quick现有managerMenuRequestsUseTheQuickWindow没有这三个选择器取消后布局不变的断言。 |
| `WS/tab_group_test.cpp` | `newCompleteWindowStartsItsHostTabBeforeCloseEmptyPolicy` | A | 迁至 Presentation::External + QuickWorkspace。管理器新窗口命令先调用宿主建标签，再应用空窗关闭策略，Quick窗口只替换可见性观察，保留创建次数和模型断言。 |
| `WS/tab_group_test.cpp` | `newWindowCreationChecksFactoryLifetime` | A | 迁至 Presentation::External。命令回调中销毁管理器后的返回值/弱指针为管理器生命周期契约，可改用无Widgets内容描述保留全部断言。 |
| `WS/tab_group_test.cpp` | `pinnedTabBarsAcceptAllSixSortCommands` | A | 迁至 Presentation::External + QuickWorkspace。管理器排序命令覆盖四边固定组，观测组序、活动项、窗格和内容身份，不向控件发送输入，Quick替换内容工厂即可保留。 |
| `WS/tab_group_test.cpp` | `sortCommandsUseAllSixRecordedOrders` | A | 迁至 Presentation::External + QuickWorkspace。六种排序、活动项和原内容身份属管理器契约，Quick内容工厂/身份查询可替代QWidget供给，保留六个固定顺序预期。 |
| `WS/tab_group_test.cpp` | `splitFactoriesCanDestroyTheirManagerBeforeReturning` | C | 补入 quick-workspace-content/quick-popup-menu。原例QWidget工厂返回前销毁作为其父对象的窗口与管理器，Quick工厂属于QuickWorkspace且创建时序不同，不能直接迁A，需补两种分割命令的安全释放。 |
| `WS/tab_group_test.cpp` | `stickyTabsLeadTheirStripAndSurviveRangeCloses` | C | 补入 quick-workspace-content/quick-popup-menu。前半为管理器置顶/范围关闭/持久化，末尾通过真实标签栏固定图标取消置顶。Quick缺范围关闭与图标接线的完整组合，需保留前半并补quick-tab-bar-input/quick-workspace。 |
| `WS/tab_group_test.cpp` | `validColorIdMatchingOldCancelMarkerWorksThroughCommand` | A | 迁至 Presentation::External。显式颜色参数允许__cancel__作为合法预设ID且清除有效，直接用External保留命令和模型检查。 |
| `WS/tab_group_test.cpp` | `wholeGroupExchangeIncludesPinnedNeighbors` | A | 已按审定要求试迁 External，四向交换包含固定组，使用实际 Quick 停靠区几何，保留中央／固定位置和内容身份断言。 |
| `WS/tab_strip_layout_test.cpp` | `accessibilityListsTabsAndFolders` | C | 补入 quick-accessibility。已有tabAndFolderActionsFollowStableIdentities检查身份与动作，但未检查所有标签role/名称/选择及标签、夹标题的childAt命中。 |
| `WS/tab_strip_layout_test.cpp` | `activationCanDestroyTheStrip` | C | 补入 quick-group/quick-tab-bar-input。Quick的inputsAndFramesDriveTheSharedController有鼠标激活删除TabBarItem，但没有键盘激活同步删除。需保留pointer/keyboard两分支生命周期覆盖。 |
| `WS/tab_strip_layout_test.cpp` | `clippedTitlesKeepTheirFullText` | C | 补入 quick-group/quick-tab-bar-input。Quick的无障碍用例保留完整标题但没有标题裁剪开启/关闭场景，缺裁剪后完整标题仍供列表/无障碍使用及解除裁剪恢复。 |
| `WS/tab_strip_layout_test.cpp` | `closeTargetShowsHoverAndPressFeedback` | C | 补入 quick-group/quick-tab-bar-input。Quick输入仅覆盖中键关闭，缺左键关闭按钮按下后移出释放取消、留在目标释放才关闭的行为。 |
| `WS/tab_strip_layout_test.cpp` | `collapsedRowsShowACountAndExpand` | C | 补入 quick-group/quick-tab-bar-input。Quick缺收起计数按钮、Push/Overlay两种展开对内容区域影响、选标签/外部点击后收起规则及恢复布局。 |
| `WS/tab_strip_layout_test.cpp` | `displayFlagsChangeActualLabelWidthsAndCreationControls` | C | 补入 quick-group/quick-tab-bar-input。Quick缺图标/编号开关传至实际标签、showNewTabButton控制新建入口显隐。固定宽度差值部分需Claude按静态外观规范审定。 |
| `WS/tab_strip_layout_test.cpp` | `dragOverACollapsedStripShowsEveryRow` | C | 补入 quick-group/quick-tab-bar-input。Quick缺拖动暂时展开收起标签栏时作为覆盖层、不改变内容几何，离开后恢复收起及隐藏标签计数。 |
| `WS/tab_strip_layout_test.cpp` | `dragSettingsCombinationsPreserveContentAndWheelRouting` | C | 补入 quick-group/quick-tab-bar-input。Quick尚无四向/多行/收起策略/滚轮/置顶尺寸/独立行288组合的内容保持、选择不变及各滚动轨道偏移合法。可按规范归并行为，不能直接取消覆盖。 |
| `WS/tab_strip_layout_test.cpp` | `endControlsEndTheFirstRow` | C | 补入 quick-group/quick-tab-bar-input。Quick缺固定/普通×新建显隐×多行组合的末端控件不遮挡可点标签。固定贴边、行高及精确相邻断言需Claude审定。 |
| `WS/tab_strip_layout_test.cpp` | `floatingCarrierUsesTheTargetStyleAndRestoresItsOwn` | C | 补入 quick-workspace-drag/quick-group。缺载体进入竖栏紧凑置顶/普通段分别使用目标样式，退出后恢复源方向、样式及置顶数。 |
| `WS/tab_strip_layout_test.cpp` | `folderHeadersToggleOpenMenusAndCarry` | C | 补入 quick-group/quick-tab-bar-input。Quick的aFolderTearsOutWithItsTabs覆盖拆出，缺横/竖标题点击折叠、竖向新建、右键入口、折叠时只留活动标签及落到折叠夹末尾。 |
| `WS/tab_strip_layout_test.cpp` | `groupNamesEndTheStripUnlessTurnedOff` | C | 补入 quick-group/quick-tab-bar-input。Quick缺组名文字与开关接线及组名不遮挡标签/菜单目标，精确排列部分需Claude按规范审定；本步保留C。 |
| `WS/tab_strip_layout_test.cpp` | `incomingGapRemainsVisibleWithoutResizingContent` | C | 补入 quick-group/quick-tab-bar-input。Quick缺上/下栏多行尾部插入超长标题时空位仍在视口内、不改变内容大小且插入索引稳定的边界。 |
| `WS/tab_strip_layout_test.cpp` | `incomingVisualMatchesTheInStripLiftAndAxis` | C | 补入 quick-group/quick-tab-bar-input。Quick拖动只检查载荷包含指针，缺四边方向中外来载荷与原地提起矩形相同、只沿对应轴移动以及清除后无残留。 |
| `WS/tab_strip_layout_test.cpp` | `insertionFollowsThePointerRow` | C | 补入 quick-group/quick-tab-bar-input。Quick缺多行第二行插入索引、空位单行高度/所在行、后续标签流动以及空位打开后索引保持稳定。 |
| `WS/tab_strip_layout_test.cpp` | `lastStickyTileStaysStickyUntilTheVisibleDivider` | C | 补入 quick-group/quick-tab-bar-input。Quick未覆盖左右竖栏末尾置顶块从原位置直到可见分割线仍判置顶、越线后改普通段的命中边界。 |
| `WS/tab_strip_layout_test.cpp` | `liftedCompactVerticalTabKeepsItsTileSize` | C | 补入 quick-group/quick-tab-bar-input。Quick未覆盖左右侧紧凑置顶标签提起后尺寸保持。现有断言仅尺寸，先列C由Claude按静态外观规范审定是否有必要保留行为。 |
| `WS/tab_strip_layout_test.cpp` | `liftedLabelPaintsAboveCreationControls` | C | 补入 quick-group/quick-tab-bar-input。Quick尚无标签拖动层在新建控件上方且不截获鼠标的等价检查。原例绑定QWidget子对象堆叠及WA属性，需改为Quick绘制/命中层契约。 |
| `WS/tab_strip_layout_test.cpp` | `liftedRowsKeepTheirContentAndFollowThePointer` | C | 补入 quick-group/quick-tab-bar-input。Quick缺上/下栏×多行组合中提起末项跨置顶/普通行移动时内容几何保持、标签跟随及空位留在视口内。 |
| `WS/tab_strip_layout_test.cpp` | `middleClickClosesManagedStripAndRailTabs` | C | 补入 quick-workspace。Quick尚无实际工作区普通标签和侧栏标签的中键关闭接线，缺只关闭目标而保留另项。 |
| `WS/tab_strip_layout_test.cpp` | `middleClickClosesTheTabUnderThePointer` | C | 补入 quick-group/quick-tab-bar-input。Quick输入只测同一普通标签中键关闭，缺按下与释放在不同标签时取消、选择保持，以及侧栏中键关闭不打开预览/移出取消。 |
| `WS/tab_strip_layout_test.cpp` | `multilineDragReordersAndTearsOffOnlyPastEveryRow` | C | 补入 quick-workspace-drag。Quick真实拖动缺上/下多行内移动不拆出、越过全部行才拆出，以及Esc恢复原序和内容身份。 |
| `WS/tab_strip_layout_test.cpp` | `multilineWrapsAndKeepsEveryTab` | C | 补入 quick-group/quick-tab-bar-input。Quick缺多行换行后所有标签保留、互不重叠、无无效滚动，以及点击末行标签实际切换内容。固定行高相邻部分需Claude审定。 |
| `WS/tab_strip_layout_test.cpp` | `newTabButtonsShareTheRowAndOpenTheTypeMenu` | C | 补入 quick-popup-menu。Quick的newTabTypesRefreshAndUseTheirButtonGroup只左击类型按钮，缺新建及类型两个按钮各自右键菜单入口与关闭释放。 |
| `WS/tab_strip_layout_test.cpp` | `paintingAndHitTestingAgree` | C | 补入 quick-group/quick-tab-bar-input。Quick的inputsAndFramesDriveTheSharedController只点击三标签部分情形，缺四向×多行×置顶大小组合中所有可见标签点击对应、标签/文件夹不重叠和命中一致。 |
| `WS/tab_strip_layout_test.cpp` | `placementPutsTheStripOnItsSide` | C | 补入 quick-group/quick-tab-bar-input。Quick的stripJoinsTheCardInEveryPlacement只设置centralPlacement，缺centralPlacement与pinnedPlacement相反时两组各选对应方向，尚未确认原独立策略完整覆盖。 |
| `WS/tab_strip_layout_test.cpp` | `stickyTabsAlwaysShowTheirAction` | C | 补入 quick-group/quick-tab-bar-input。Quick尚未覆盖置顶标签左键操作请求解除置顶、普通标签左键请求关闭，且两类动作互不串用。 |
| `WS/tab_strip_layout_test.cpp` | `titleWidthChangesKeepTheActiveTabInPlace` | C | 补入 quick-group/quick-tab-bar-input。Quick缺标题裁剪长度变化时活动标签保持位置、仅越过尾边界才调整、往返不漂移及活动项不可见时保留前沿锚点。 |
| `WS/tab_strip_layout_test.cpp` | `tooltipsNameTheDisplayedTab` | C | 补入 quick-popup-menu。Quick的hoverTipsFollowThePointer只有一行普通标签，缺多行所有显示标签按各自命中位置返回正确提示。 |
| `WS/tab_strip_layout_test.cpp` | `verticalStripResizesAndFoldsToIcons` | C | 补入 quick-group/quick-tab-bar-input。Quick尚无竖栏拖动调宽/双击恢复、组名与折叠图标模式、悬停覆盖展开不改内容几何的完整交互用例。固定尺寸部分需Claude审定。 |
| `WS/tab_strip_layout_test.cpp` | `wheelExpandedRowsSurviveRebuildsUntilCollapsed` | C | 补入 quick-workspace。Quick尚未验证展开后增标签/改图标仍保持展开及内容身份，手动收起后再增标签保持收起。 |
| `WS/tab_strip_layout_test.cpp` | `wheelExpandsAndCollapsesRows` | C | 补入 quick-group/quick-tab-bar-input。Quick缺真实滚轮展开/收起多行、收起保持当前项可见，以及关闭设置后滚轮不改变展开状态。 |
| `WS/tab_strip_layout_test.cpp` | `wheelScrollsAOneRowStrip` | C | 补入 quick-group/quick-tab-bar-input。Quick输入用例没有单行标签栏滚轮双向滚动并回零的验证，不能用侧栏滚轮用例代替。 |
| `WS/tab_viewport_test.cpp` | `activeTabMovesOnlyAfterTheShrinkingEdgeReachesIt` | C | 补入 quick-group/quick-workspace。Quick缺逐步缩小到活动标签留空耗尽之前保持偏移，超过后仅作最小调整且仍完整显示的边界。 |
| `WS/tab_viewport_test.cpp` | `directScrollInputCancelsQueuedRestoration` | C | 补入 quick-group/quick-workspace。Quick输入用例缺pixel-wheel与滚动条轨道点击两分支，不能仅依据共享控制器推断排队恢复会让位于实际输入。 |
| `WS/tab_viewport_test.cpp` | `explicitScrollingCancelsQueuedFocusProtection` | C | 补入 quick-group/quick-workspace。Quick缺缩放/restoreViewportState排队保护之后，新的滚轮偏移优先且处理队列后不被回滚。 |
| `WS/tab_viewport_test.cpp` | `initialWindowWithShortTabsDoesNotRetainPrelayoutOffsets` | C | 补入 quick-group/quick-workspace。Quick窗口初显用例未验证工作台/浮窗短标签在预布局后偏移和最大值归零，且全部标签位于可见视口。 |
| `WS/tab_viewport_test.cpp` | `lastActiveTabNeverManufacturesTrailingSpace` | C | 补入 quick-group/quick-workspace。Quick缺当前末项随反复缩放保持可见、内容自然宽度不增长、不制造尾部空白及滚轮显式输入不被恢复覆盖。 |
| `WS/tab_viewport_test.cpp` | `localResizePreservesTheVisibleAnchor` | C | 补入 quick-group/quick-workspace。Quick缺四方向局部尺寸变化×活动标签可见/不可见组合下，活动项完整显示或原屏幕锚点保持、偏移稳定。 |
| `WS/tab_viewport_test.cpp` | `movingAWindowOrVerticallyResizingNeverScrollsItsStrip` | C | 补入 quick-group/quick-workspace。Quick缺单独移动窗口、只改高度或不触碰活动项边界的宽度调整后原偏移保持。 |
| `WS/tab_viewport_test.cpp` | `pinnedEdgesPreserveTheCentralAnchor` | C | 补入 quick-group/quick-workspace。Quick的pinnedRegionsSitAroundTheCenterAndKeepTheirExtent只测区域尺寸，缺四边固定/取消固定后中央标签屏幕锚点保持。 |
| `WS/tab_viewport_test.cpp` | `realWindowResizePreservesTheUnchangedSide` | C | 补入 quick-group/quick-workspace。Quick缺左右真实窗口边缘缩放×活动可见性组合的全局锚点、后续独立移动不被当作左缩放，以及新滚轮输入优先的回归覆盖。 |
| `WS/tab_viewport_test.cpp` | `reconstructedGroupsPreserveTheirAnchor` | C | 补入 quick-group/quick-workspace。Quick缺四方向拆分重建后原组非活动可见锚点保持、内容身份不变及后续排队恢复不漂移。 |
| `WS/tab_viewport_test.cpp` | `restoringAnUnshownViewportClampsToNaturalContent` | C | 补入 quick-group/quick-workspace。Quick未验证从尚无有效bounds的未显示视口恢复到宽窗口时，偏移按自然内容收敛且每项可见。 |
| `WS/tab_viewport_test.cpp` | `splittingKeepsTheOriginalGroupsVisibleActiveTab` | C | 补入 quick-group/quick-workspace。Quick缺左右分组×多个活动索引×先显示/后显示×API/命令路径后，原组活动标签实际完整可见且内容身份不变。 |
| `WS/tab_viewport_test.cpp` | `underfilledStripTracksBothDirectionsWithoutOverscroll` | C | 补入 quick-group/quick-workspace。Quick缺短内容组左右局部平移/缩放后最大偏移始终零、所有项可见且设置巨大偏移被钳制。 |
| `WS/toolbar_policy_test.cpp` | `conflictingHostPoliciesAreRejectedAtomically` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少第二强制弹性策略安装/移行/恢复/拖入同排均原子拒绝，不变更布局及现有约束的覆盖。 |
| `WS/toolbar_policy_test.cpp` | `dragDemotesIncomingElasticAndCancellationRestoresIt` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少拖入强制弹性行时普通弹性栏降级、取消完整恢复并在公开移动时同样处理的覆盖。 |
| `WS/toolbar_policy_test.cpp` | `movingTheOwnerResolvesTheDestinationRow` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少强制弹性所有者移到另一行后把目标普通弹性栏降级、再移出后目标允许重新启用弹性的覆盖。 |
| `WS/toolbar_policy_test.cpp` | `policyRequestsDuringDragUseTheCommittedRows` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少拖动预览暂时让出源行时所有策略API仍以可取消的提交行校验、不能引入两个强制所有者的覆盖。 |
| `WS/toolbar_policy_test.cpp` | `requiredSizingOverridesApiAndCustomization` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少强制弹性策略覆盖API与两种自定义菜单、隐藏所有者时同排普通栏仍不能成为第二弹性所有者的覆盖。 |
| `WS/toolbar_policy_test.cpp` | `savedContentTitleRecoversMenuResetAndRightAnchoring` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。缺少已保存 content 标题策略被归一为强制弹性后，菜单重置保留右锚点、排除溢出、其他标题按钮保持且缩放恢复正确的回归覆盖。固定尺寸部分请 Claude 审定。 |
| `WS/toolbar_policy_test.cpp` | `windowTitlePolicyCannotBeChanged` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）。含实际宿主强制弹性约束与移行后菜单策略可编辑性，Quick 当前未等价测两种窗口标题不得改content、同排菜单不得弹性而移出该行后允许的完整契约。 |
| `WS/toolbar_profile_test.cpp` | `cancellingToolbarDragDoesNotPublish` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的档案与拖动用例。Quick handleDragsMoveStrips 只比较窗口 strips，缺少两种窗口拖动预览及Esc过程中 toolbarProfiles 始终不发布、工具栏实际对象身份不变的覆盖。 |
| `WS/toolbar_profile_test.cpp` | `closingEveryFloatingWindowKeepsItsDefault` | A | 原 toolbar-profile 套件改用 External + QuickWorkspace，以公开工具栏API布置并从保存窗口记录取布局，保留JSON/身份断言。管理器关闭最后浮窗后仍保存浮窗档案，重启再建浮窗采用该档案而工作台不混入浮窗菜单。用公开工具栏API与保存窗口记录可保留原预期。 |
| `WS/toolbar_profile_test.cpp` | `exclusionsMergePerButton` | A | 原 toolbar-profile 套件改用 External + QuickWorkspace，以公开工具栏API布置并从保存窗口记录取布局，保留JSON/身份断言。断言同类型不同窗口的隐藏按钮按ID独立合并、恢复单项不清除另一项、新窗口继承最新排除集合，改公开工具栏API/快照查询可保留。 |
| `WS/toolbar_profile_test.cpp` | `incompleteSnapshotsDoNotReconstructProfiles` | A | 原 toolbar-profile 套件改用 External + QuickWorkspace，以公开工具栏API布置并从保存窗口记录取布局，保留JSON/身份断言。管理器预校验缺少档案或混合窗口类型的快照时零内容工厂调用、模型/内容身份和快照原子保持，Quick 内容工厂可保留全部断言。 |
| `WS/toolbar_profile_test.cpp` | `latestEditSurvivesSavingOlderWindows` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的档案与拖动用例。依赖旧视图 restoreStripLayout 只改本地显示且不发布为编辑，Quick 无公开等价入口。现有新窗口继承测试未覆盖旧窗口恢复/几何/保存不倒灌，以及后来局部编辑只更新一维，先列C。 |
| `WS/toolbar_profile_test.cpp` | `malformedProfilesLeaveTheManagerUntouched` | A | 原 toolbar-profile 套件改用 External + QuickWorkspace，以公开工具栏API布置并从保存窗口记录取布局，保留JSON/身份断言。所有数据行都检查非法档案恢复前置拒绝、零内容创建且原窗口/内容身份和快照保持。初始宽度用公开工具栏API，External 配 QuickWorkspace 保留行为。 |
| `WS/toolbar_profile_test.cpp` | `synchronousSavingObservesOnlyTheCompletedRestore` | C | External 恢复完成通知内立即保存时，Quick 窗口尚未投影，saveViewLayout 因缺正常几何跳过全部窗口记录；原当前／休眠工具栏布局断言失败。原 Widgets 函数及全部断言保留，见交接 Q4。 |
| `WS/toolbar_profile_test.cpp` | `unrelatedEditsInOlderWindowsKeepTheNewestOrder` | A | 原 toolbar-profile 套件改用 External + QuickWorkspace，以公开工具栏API布置并从保存窗口记录取布局，保留JSON/身份断言。断言档案各字段合并语义，旧窗口修改锁定/排除项不覆盖新顺序和新宽度，显式移动只替换顺序。用管理器公开工具栏API布置，保存窗口记录替代 Widgets 取值，可保留原JSON预期。 |
| `WS/toolbar_profile_test.cpp` | `windowKindsRestoreTheirOwnLayoutAndLiveContent` | A | 原 toolbar-profile 套件改用 External + QuickWorkspace，以公开工具栏API布置并从保存窗口记录取布局，保留JSON/身份断言。断言窗口升降级保留各类型独立工具栏档案、当前布局、休眠布局和内容身份。External 配 QuickWorkspace，内容/窗口记录换等价查询后保留语义。 |
| `WS/toolbar_reset_test.cpp` | `closingTheWindowAfterThePickerIsDismissedFails` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的宽度重置选择器用例。Widgets 命令为阻塞选择器，Quick 弹窗异步完成，窗口关闭时 execute 的同步返回语义无法照搬。需 Claude 核定对应完成结果与晚到回调拒绝的契约并补 Quick 用例。 |
| `WS/toolbar_reset_test.cpp` | `closingTheWindowDismissesItsPicker` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的宽度重置选择器用例。缺少重置选择器打开时关闭所属窗口立即关闭选择器、命令不对已关闭窗口操作的覆盖。Widgets 同步失败返回在Quick异步路径中如何表达先由 Claude 核定。 |
| `WS/toolbar_reset_test.cpp` | `invalidRequestsLeaveTheLayoutUnchanged` | A | 原 toolbar-reset 套件改用 External。纯管理器 resetToolbarWidth 与显式命令非法窗口/空ID/不存在栏的原子拒绝，保留完整快照相等断言。 |
| `WS/toolbar_reset_test.cpp` | `optionsMenuDispatchesTheRegisteredCommand` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的宽度重置选择器用例。Quick 选项菜单仅验证重置入口存在，缺少触发后确实走注册命令、无 commandFailed、有layoutChanged且实际宽度回自然值和保存请求清理的覆盖。 |
| `WS/toolbar_reset_test.cpp` | `pickerSupportsKeyboardSelectionAndCancellation` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的宽度重置选择器用例。Quick toolbarCommandsOfferTheirChoicesInQuickMenus 只测移动选择器，缺重置选择器键盘选栏/取消、取消不改布局及选择恢复自然宽度的覆盖。 |
| `WS/toolbar_reset_test.cpp` | `publicApiAndExplicitCommandRestoreNaturalWidth` | C | 拟补 WS/quick_toolbar_test.cpp（quick-toolbar）的宽度重置选择器用例。缺少两种窗口API和显式命令恢复实际菜单自然宽度、保存width=-1、顺序/标题强制弹性不变、固定按钮栏拒绝改宽且可无害重置的完整覆盖。 |
| `WS/view_commands_test.cpp` | `bottomToolbarAndHiddenChromeUseNoExtraLayoutSpace` | C | 补 WS/quick_toolbar_test.cpp 的布局/命令生命周期。普通/全屏下各栏、底部固定区/侧栏顺序及隐藏后停靠区填满，防遮挡/留空。Quick 几何例缺完整组合，不能按外观退役。 |
| `WS/view_commands_test.cpp` | `chordEscapeCancelsThePrefixBeforeExitingFullscreen` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的视图快捷键。先 Esc 取消组合键保留全屏，后 Esc 才退出，另查禅往返与正文。Quick 缺此优先级。 |
| `WS/view_commands_test.cpp` | `closingTheWindowSafelyDismissesEitherToolbarMovePicker` | C | 补 WS/quick_toolbar_test.cpp 的布局/命令生命周期。两选择阶段关源窗释放菜单且命令失败。Quick 一般弹出菜单关 owner 未覆盖多阶段命令返回语义。 |
| `WS/view_commands_test.cpp` | `commandAndToolbarPickerCanDestroyTheirManagerSafely` | C | 补 WS/quick_toolbar_test.cpp 的布局/命令生命周期。enabled/execute 及两选择阶段销毁管理器都安全失败。Quick 销毁工作区例未覆盖该管理器/多阶段链。 |
| `WS/view_commands_test.cpp` | `displaySettingsSearchKeepsExactRowsAndSectionHeadings` | C | 补 WS/quick_menu_bar_test.cpp 与 quick_settings_test.cpp 的视图设置/菜单接线。完整目录、四栏搜索、已删模式零结果与默认范围所属标题。Quick 搜索仅两个键，覆盖不足。 |
| `WS/view_commands_test.cpp` | `escapeExitsFullscreenAndItsZenProjection` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的视图快捷键。三范围经组合键进禅模式后 Esc 退并恢复偏好且正文保持。Quick API 范围示例缺真实键序列。 |
| `WS/view_commands_test.cpp` | `functionKeysToggleTheirActualFullscreenScope` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的视图快捷键。三种 F11 组合切实际全屏、保持模型/身份/正文。Quick 范围例走 API，未覆盖功能键和文本不泄漏。 |
| `WS/view_commands_test.cpp` | `movedSettingsEditTheirCurrentWindow` | C | 补 WS/quick_menu_bar_test.cpp 与 quick_settings_test.cpp 的视图设置/菜单接线。移页后四栏回显、全屏/禅与显隐只改目标窗，原偏好/页身份保持。Quick 移页设置例仅普通工具栏子集。 |
| `WS/view_commands_test.cpp` | `registryEnabledCanRegisterCommandsWithoutInvalidatingItsHandler` | A | 原套件保留，管理器夹具迁 External。enabled 中批量注册后只执行一次且目录增量正确，仅注册表/上下文。 |
| `WS/view_commands_test.cpp` | `settingsChooseTheZenDefaultAndEnableItFromNormal` | C | 补 WS/quick_menu_bar_test.cpp 与 quick_settings_test.cpp 的视图设置/菜单接线。三范围下拉/外部回显、从普通命令进入实际全屏、临时状态不保存及正文/身份保持。Quick 设置例缺完整链。 |
| `WS/view_commands_test.cpp` | `settingsShowRestoresZenPreferences` | C | 补 WS/quick_menu_bar_test.cpp 与 quick_settings_test.cpp 的视图设置/菜单接线。四种设置从禅模式恢复对应偏好且保留其他，临时模式不作为设置项。Quick visibilityEditsFollowTheSettingsPage 只普通模式工具栏。 |
| `WS/view_commands_test.cpp` | `toolbarMoveCancellationInEitherPhaseKeepsTheSnapshot` | C | 补 WS/quick_toolbar_test.cpp 的布局/命令生命周期。两阶段各 Esc 取消关闭全部菜单且快照不变。Quick 工具栏选择例仅成功路径。 |
| `WS/view_commands_test.cpp` | `toolbarMoveCommandPreservesIdentityAndRejectsInvalidArguments` | C | 补 WS/quick_toolbar_test.cpp 的布局/命令生命周期。跨区域/新行保留条带/viewport/内容身份，非法 row/index 类型原子拒绝。Quick 布局编辑例未覆盖完整参数矩阵。 |
| `WS/view_commands_test.cpp` | `toolbarMovePickersChooseExistingAndNewBottomRows` | C | 补 WS/quick_toolbar_test.cpp 的布局/命令生命周期。两阶段键盘选择既有/新底行且条带/内容身份保持。Quick toolbarCommandsOfferTheirChoicesInQuickMenus 仅鼠标新行。 |
| `WS/view_commands_test.cpp` | `viewMenuReflectsFullscreenZenAndAllFourVisibilityPreferences` | C | 补 WS/quick_menu_bar_test.cpp 与 quick_settings_test.cpp 的视图设置/菜单接线。菜单三范围互斥勾选、四栏状态、禅/退出可用与点击恢复偏好，Quick 缺完整矩阵。 |
| `WS/view_commands_test.cpp` | `visibilityShortcutsChangeOnlyTheirOwnWindowPreferences` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的视图快捷键。四显隐键跨全屏进退只改本窗偏好、不写正文。Quick 设置/API 例缺完整快捷键矩阵。 |
| `WS/view_commands_test.cpp` | `zenChordStartsTheConfiguredScopeFromNormal` | C | 补 WS/quick_workspace_test.cpp 与 quick_workspace_content_test.cpp 的视图快捷键。三默认范围从普通经 Ctrl+K,Z 进入，菜单可用、实际全屏、偏好/正文保持及退出，Quick 缺完整联动。 |
| `WS/view_reuse_test.cpp` | `changedTreeRebuildsOnlyThatTab` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。只重建被改标签框架，其他框架/内容身份保持。重建对象属 Widgets 实现，需审定后补 Quick 有效内容不丢失契约。 |
| `WS/view_reuse_test.cpp` | `contentVisibilityNotificationsAreCoalesced` | A | 原套件保留，External+QuickWorkspace，保留可见性批次/PaneId 断言。订阅收到一次完整快照，同轮两次活动切换合并为最终一批变化，是宿主通知契约，可保留批次和目标断言。 |
| `WS/view_reuse_test.cpp` | `contentVisibilityTracksWindowStateAndPeek` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。peek 开/关与最小化/还原各通知实际可见性。Quick visibilityReportsActualContentAndResendsALateSnapshot 仅 hide/show/标签切换，缺 peek/最小化。 |
| `WS/view_reuse_test.cpp` | `crossWindowMoveReparentsOnlyTheMovedTab` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。移动一页时无关页/组不重建、不进 Storage，内容自身父关系不变。Quick 现有跨窗例未测无关组/页面变化范围。 |
| `WS/view_reuse_test.cpp` | `crossWindowMoveReportsHiddenBeforeReparent` | B | 随单元4退役，由 WS/quick_content_host_test.cpp::movingContent_preservesIdentityAndReportsHiddenBeforeWindowChange 覆盖。已读 Quick 实际断言。跨窗 mount 先报 hidden，回调内确认仍属源窗，再记录 windowChanged 并确认首事件 hidden，内容/编辑器身份、目标窗焦点保持。覆盖旧页跨窗前先隐藏通知的功能语义。 |
| `WS/view_reuse_test.cpp` | `deferredRootDeletionKeepsCarriedPages` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。旧根延迟删后幸存页面/内容，以及转自动隐藏再开均保留。Quick 未覆盖此延迟删除链，TabPage 获取需改写。 |
| `WS/view_reuse_test.cpp` | `mountedRebuildIsVisibleImmediately` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。split API 返回时无需事件循环，新旧内容即实际可见。Quick 排队同步时序可能不同，须先明确契约，不能改 QTRY 冒充等价。 |
| `WS/view_reuse_test.cpp` | `mutationFromSplitterLayoutKeepsReusedStrip` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。比例命令/实际 splitter resize 内回调关他标签或组，剩余条带/内容有效可见。Quick geometryCallbacksCanReplaceOrDestroyTheView 只直接改树，缺真实管理器/组链。 |
| `WS/view_reuse_test.cpp` | `pendingTabBarsFollowLazyActivationAndRemoval` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。未激活栏在 Storage 等待、替换延迟释放、激活显示、清除/关删。Quick 栏例无完整 pending 生命周期，Storage 获取需改写。 |
| `WS/view_reuse_test.cpp` | `pendingTabBarsSurviveHierarchyConversion` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。未激活标签待挂载上下栏在 convertGroupToTab 后显示并保留页面归属。Quick hierarchyConversionKeepsTheExistingBars 从已激活源出发，缺 pending。 |
| `WS/view_reuse_test.cpp` | `restoredInactiveTabsCreateViewsOnFirstActivation` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。恢复100标签后内容全创建但仅一个 TabPage，后续逐个建页。Quick 内容/页面时机不同，先明确懒创建契约，现有 Quick 仅两标签。 |
| `WS/view_reuse_test.cpp` | `restoringLayoutDiscardsPendingTabBars` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。同 ID 恢复应清理未激活旧待挂载栏。Quick 同所有者栏缓存是否清理待确认，现有栏例无恢复场景。 |
| `WS/view_reuse_test.cpp` | `reusedStripFollowsMetadataAndTheme` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。改名/关闭他组与主题更新显示新文字/图标尺寸，还规定条带何时重建。混合元数据功能与实现/静态尺寸，交 Claude 审定。 |
| `WS/view_reuse_test.cpp` | `tabViewsStayInTheirWindow` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。关闭他标签不把幸存页搬到 Storage，另组条带/页面身份和 parentChange 不变。Quick 内容身份例未等价整页复用，需审定。 |
| `WS/view_reuse_test.cpp` | `themeMetricChangeRebuilds` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。主题后刷新改变 QSplitter handleWidth，属具体重建/固定尺寸检查。先交 Claude 判断退役/提炼功能，不直接删。 |
| `WS/view_reuse_test.cpp` | `unrelatedRefreshKeepsTheFocusedChildWhilePaneNavigationStillWorks` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。无关刷新保持非默认子编辑器选区，重复聚焦保留当前子项，离开返回用默认代理。Quick 焦点例缺选区及默认/当前区别。 |
| `WS/view_reuse_test.cpp` | `unrelatedTabsKeepTheirViewWidgets` | C | 补 WS/quick_workspace_content_test.cpp 与 quick_workspace_test.cpp 的复用行为，旧实现断言交 Claude 审定。关/移/改无关标签保持内容、PaneHost/QSplitter 和 parentChange 次数。含 Widgets 缓存实现断言，Quick 未覆盖全链，需审定身份与实现对象边界。 |
| `WS/view_state_test.cpp` | `arbitraryToolbarRowsRestoreAndMalformedUnknownItemsFail` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。任意行/未知项配置回读，重复项非法保持快照，为共用布局格式。 |
| `WS/view_state_test.cpp` | `changingScopeRetainsContentAndExitingRestoresNormalBars` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。三范围切换保持模型/内容身份，普通模式恢复偏好并清空目标，可接 QuickWorkspace。 |
| `WS/view_state_test.cpp` | `explicitBarChangesApplyAcrossScopesOnlyInThisWindow` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。三范围×四栏只改本窗并跨范围保留偏好，无控件断言。 |
| `WS/view_state_test.cpp` | `focusedTargetNavigationLeavesOnlyTheFocusedScope` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。导航范围外退窗口全屏，同窗格保留原范围，只检查状态。 |
| `WS/view_state_test.cpp` | `fullscreenAndZenRestartPreserveNormalPlacement` | C | 补 WS/quick_workspace_test.cpp 或 quick_native_frame_test.cpp 的恢复放置。直接用 WorkspaceWindow::restorePlacement/normalPlacementGeometry，三范围×禅×最大化12组合保存普通位置并恢复状态。Quick 普通恢复例缺完整矩阵，要换 Quick 放置入口保留几何契约。 |
| `WS/view_state_test.cpp` | `fullscreenScopesPreserveWindowVisibility` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。三范围保存栏偏好、模型和退出状态，只查询管理器值。 |
| `WS/view_state_test.cpp` | `groupScopeFollowsItsSelectedTab` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。同组选另一标签，组范围持续并更新目标 tab，纯模型/状态。 |
| `WS/view_state_test.cpp` | `invalidOperationsLeaveStateUnchanged` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。无效模式/窗口/显隐调用保持快照且不发信号。 |
| `WS/view_state_test.cpp` | `malformedDisplaySnapshotsFailBeforeCreatingContent` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。所有错误类型/目标/工具栏布局在工厂前拒绝，快照/内容身份保持。Quick 工厂仅确认零创建，不要求旧成功恢复预制语义。 |
| `WS/view_state_test.cpp` | `nonPersistentWindowFieldsAreRejected` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。全部范围/禅/最大化数据行的临时 viewState 拒绝恢复，工厂零调用且快照不变，当前格式逻辑保留。 |
| `WS/view_state_test.cpp` | `omittedOptionalDisplayFlagsUseDefaults` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。省略工具栏/状态字段恢复默认及普通/非禅，无控件依赖。 |
| `WS/view_state_test.cpp` | `oversizedFullscreenSnapshotsRestoreVisibleNormalPlacement` | C | 补 WS/quick_workspace_test.cpp 或 quick_native_frame_test.cpp 的恢复放置。超整个桌面快照在三范围/禅/最大化恢复后修复到最大可用屏幕，模型/偏好保持。Quick 缺超屏修复回归，不能按外观删。 |
| `WS/view_state_test.cpp` | `paneScopeFollowsItsStablePaneWithinTheWindow` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。窗格移入另一标签后范围跟稳定 PaneId，更新组/标签/上下文且保留内容身份。 |
| `WS/view_state_test.cpp` | `removingFocusedTargetsRestoresNormalView` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。关窗格/标签数据行，目标移除后退出禅模式恢复偏好，只管理器查询。 |
| `WS/view_state_test.cpp` | `viewNotificationAllowsManagerDestruction` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。状态通知同步销毁管理器仍安全返回，纯生命周期。 |
| `WS/view_state_test.cpp` | `zenDefaultScopePersistsAndRejectsInvalidValues` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。当前格式默认范围往返、非法值原子拒绝、字段省略回默认，管理器数据契约。 |
| `WS/view_state_test.cpp` | `zenEntryNotificationAllowsManagerDestruction` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。进入禅模式通知中销毁管理器，返回值/生命周期保持。 |
| `WS/view_state_test.cpp` | `zenFromNormalEntersTheConfiguredScopeAtomically` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。三默认范围单通知原子进禅，模型/身份/偏好保持并可退出。 |
| `WS/view_state_test.cpp` | `zenKeepsAnExistingScopeAndRequiresValidContent` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。已有范围不被默认覆盖，空窗按内容所需校验，拒绝无变更。 |
| `WS/view_state_test.cpp` | `zenPreservesPreferencesAndShowRestoresThem` | A | 原套件保留，管理器迁 External，内容身份按需接 QuickWorkspace。三范围×四栏的禅模式有效显隐/偏好分离，显式显示恢复偏好，只管理器值。 |
| `WS/visibility_profile_test.cpp` | `detachedTabsAndExtractedPanesUseTargetKind` | A | External 管理器，保留原行为断言。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/visibility_profile_test.cpp` | `hostMayExplicitlyBypassProfiles` | A | External 管理器，保留原行为断言。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/visibility_profile_test.cpp` | `invalidProfileRejectedAtomically` | A | External 管理器，保留原行为断言。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/visibility_profile_test.cpp` | `kindChangesKeepTheirOwnHistory` | A | External 管理器，保留原行为断言。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/visibility_profile_test.cpp` | `latestEditSurvivesOlderWindowsAndRestart` | A | External 管理器，保留原行为断言。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/visibility_profile_test.cpp` | `newWindowCommandUsesLatestDefaultsAfterRestart` | A | External 管理器，保留原行为断言。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/visibility_profile_test.cpp` | `zenDoesNotOverwritePreferences` | A | External 管理器，保留原行为断言。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/window_placement_test.cpp` | `closeAllCancelsDragBeforeApprovalAndSaving` | C | 补 quick-workspace-drag 的关闭全部与拖动取消。需在关闭批准前取消拖动并核对批准快照、保存快照及拒绝关闭后的布局 |
| `WS/window_placement_test.cpp` | `closeAllSurvivesManagerDeletionWhileCancellingDrag` | C | 补 quick-workspace-drag 的取消时管理器销毁。旧例直接断言 QWidget::mouseGrabber，Quick 的捕获与取消重入需独立验证 |
| `WS/window_placement_test.cpp` | `firstFullscreenHasAVisibleRestorePlacement` | C | 补 quick-workspace 的首次显示即全屏及还原位置。旧例先获取已创建且隐藏的 QWidget，Quick 默认投影即显示，前置条件不能原样迁移 |
| `WS/window_placement_test.cpp` | `freshManagerRestoresNormalPlacement` | A | External + QuickWorkspace，位置与状态取 QQuickWindow。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/window_placement_test.cpp` | `maximizeStateChangeNotifiesTheHost` | C | Quick 的移动／尺寸／最大化状态未向管理器发布位置变更通知，原 layoutChanged 断言在两种窗口类型下失败；原 Widgets 函数及数据保留，见交接 Q5。 |
| `WS/window_placement_test.cpp` | `maximizedAndMinimizedPreserveNormalPlacement` | C | Quick 窗口最大化后 normalGeometry 缓存为空，保存时跳过该窗口记录，原普通几何持久化断言失败；原全部数据及 Widgets 函数保留，见交接 Q6。 |
| `WS/window_placement_test.cpp` | `movingAndResizingNotifyTheHost` | C | Quick 的移动／尺寸／最大化状态未向管理器发布位置变更通知，原 layoutChanged 断言在两种窗口类型下失败；原 Widgets 函数及数据保留，见交接 Q5。 |
| `WS/window_placement_test.cpp` | `newWorkbenchesFitTheirInitialScreen` | C | 旧夹具在首次 show 前取得 QWidget 并设置屏外位置；Quick 投影时已自动显示，取得 QQuickWindow 后改位置属于后续用户移动，不能平迁这项初始放置输入。保留原函数与数据，3.4 按新窗口可达性的实际入口整理，不增加测试专用显示钩子。 |
| `WS/window_placement_test.cpp` | `normalizationUsesAllLogicalScreenAreas` | A | 保留 normalizeWindowPlacement 纯逻辑检查。虚拟屏幕、可用区域和可恢复几何不依赖控件 |
| `WS/window_placement_test.cpp` | `partiallyOffscreenSnapshotsBecomeReachable` | A | External + QuickWorkspace，位置与状态取 QQuickWindow。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/window_placement_test.cpp` | `queuedNotificationDoesNotOutliveAClosedWindow` | A | External + QuickWorkspace，位置与状态取 QQuickWindow。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/window_placement_test.cpp` | `restoringOverAnExistingMaximizedWindowHonorsNormalState` | A | External + QuickWorkspace，位置与状态取 QQuickWindow。保留现有偏好／几何／状态／通知断言，替换内容及窗口获取入口 |
| `WS/workspace_test.cpp` | `aBoundCommandExecutesFromContentKeyboardInput` | C | 3.4 已合并到 quick-workspace-content 的 `editorStateSurvivesCommandsAndRejectedRestore`；原 Widgets 用例退役，映射和证据见 3.4 进度。 |
| `WS/workspace_test.cpp` | `allCommandsCanBeBoundAndDefaultsHaveNoCollisions` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。目录唯一、默认键冲突、改绑和重复命令拒绝均为注册表检查。 |
| `WS/workspace_test.cpp` | `bulkCloseCommandsRespectTheSelectedTabAndGroup` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。左右/其他/全部数据行，保留目标组幸存标签集与对应内容存活状态。 |
| `WS/workspace_test.cpp` | `closeAllAllowsUnchangedFocusAfterApproval` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。许可中重复激活原标签不算布局变化，应允许关闭并保存原快照。 |
| `WS/workspace_test.cpp` | `closeAllApprovesOnceAndSavesTheCompleteWorkspace` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。全部家庭仅确认一次，关闭前保存完整快照，保存/关闭信号次数与内容释放不变。 |
| `WS/workspace_test.cpp` | `closeAllFromLayoutNotificationEmitsClosedOnce` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。布局通知内全关只发一次关闭和保存通知并清空模型。 |
| `WS/workspace_test.cpp` | `closeAllHandlesManagerDestruction` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。许可/通知/关闭回调销毁管理器数据行，保留返回值与生命周期检查。 |
| `WS/workspace_test.cpp` | `closeAllRejectsChangesDuringCallbacks` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。许可/通知两阶段的新增窗格/标签/窗口、显隐、位置、布局六类变更均取消过期关闭，位置接 Quick 窗口。 |
| `WS/workspace_test.cpp` | `closeAllRejectsContentReplacementWithTheSameIds` | C | 补 WS/quick_content_host_test.cpp 与 quick_workspace_content_test.cpp 的内容契约。许可期间恢复同 ID 必须替换内容并使关闭失效。Quick 稳定 PaneId 缓存是否重建须先确认，现有 Quick 无此例。 |
| `WS/workspace_test.cpp` | `closeAllRejectsModelChangeFromGuard` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。许可/通知中改名取消关闭，模型与内容身份保持。 |
| `WS/workspace_test.cpp` | `closeAllRejectsToolbarEditFromGuard` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。许可中工具栏锁定状态改变取消关闭，并保留内容与窗口。 |
| `WS/workspace_test.cpp` | `closeRejectsPanesAddedDuringConfirmation` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。关闭窗格/标签数据行，确认中新增窗格应拒绝旧请求，保留 PaneId 集和对象身份。 |
| `WS/workspace_test.cpp` | `closeWindowRejectsContentAddedDuringConfirmation` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。新标签/新浮窗数据行，许可中新增内容必须阻止过期关闭，随后合法关闭释放全部对象。 |
| `WS/workspace_test.cpp` | `closingAnUnrelatedPaneCannotCloseTheTab` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。未知 PaneId 关闭失败，模型与当前内容对象不变。 |
| `WS/workspace_test.cpp` | `closingFloatingWindowOnlyReleasesItsOwnContent` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。只有浮窗标签参与许可/释放，根内容与模型保持。 |
| `WS/workspace_test.cpp` | `customCommandsReceiveArgumentsAndRespectTheirPrecondition` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。前置条件、执行次数、实参与捕获的窗口/标签/窗格上下文不依赖控件。 |
| `WS/workspace_test.cpp` | `detachPromoteDemoteAndRedockPreserveLiveContent` | C | 3.4 已合并到 quick-workspace-content 的 `liveEditorsSurviveWindowAndPaneTransfers`；原 Widgets 用例退役，映射和证据见 3.4 进度。 |
| `WS/workspace_test.cpp` | `edgePinAndReturnToCenterPreserveTheSameWidget` | C | 3.4 已合并到 quick-workspace-content 的 `liveEditorsSurviveWindowAndPaneTransfers`；原 Widgets 用例退役，映射和证据见 3.4 进度。 |
| `WS/workspace_test.cpp` | `emptyRootWithFloatingContentSurvivesAutomaticClosePolicy` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。根虽空但家庭有浮窗内容时保留，最后浮窗关闭后模型清空。 |
| `WS/workspace_test.cpp` | `failedAdoptionLeavesCallerOwnershipIntact` | C | 补 WS/quick_content_host_test.cpp 与 quick_workspace_content_test.cpp 的内容契约。依赖被删除的 QWidget* addTab/splitPane、parentWidget 和 windowSystemManaged。Quick ContentHost 有部分拒绝所有权，缺管理器无效目标/重复内容完整入口，需审定改写边界。 |
| `WS/workspace_test.cpp` | `failedRestoreReleasesPreparedContentsAndPreservesCurrentContents` | C | 补 WS/quick_content_host_test.cpp 与 quick_workspace_content_test.cpp 的内容契约。旧工厂在恢复返回前预制，第二次返回空导致恢复失败及预制清理。External 的 Quick 工厂不在管理器恢复事务中，缺同等失败/部分清理契约。 |
| `WS/workspace_test.cpp` | `invalidRestorePreservesTheCurrentModelAndWidgets` | C | 3.4 已合并到 quick-workspace-content 的 `editorStateSurvivesCommandsAndRejectedRestore`；原 Widgets 用例退役，映射和证据见 3.4 进度。 |
| `WS/workspace_test.cpp` | `invalidTabArgumentCannotChangeEdgeLayout` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。非法命令发失败信号，模型、内容身份和所属窗口不变，QuickWorkspace 可保留。 |
| `WS/workspace_test.cpp` | `optionalBarsTravelWithTheTabAndAreReleasedOnClose` | C | 补 WS/quick_content_host_test.cpp 与 quick_workspace_content_test.cpp 的内容契约。包含 QLabel 文字、拆回窗口、清除立即隐藏及延迟删除时序。Quick barOwnershipFollowsTabsAndPanesRatherThanPages 未覆盖全部语义，需改写控件断言。 |
| `WS/workspace_test.cpp` | `recursiveCloseAllCannotBypassConfirmation` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。全关许可内六种递归关闭入口均拒绝，保持完整快照。 |
| `WS/workspace_test.cpp` | `recursiveCloseCannotBypassConfirmation` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。许可内递归关闭须拒绝，外层拒绝保留内容，之后合法关闭释放。 |
| `WS/workspace_test.cpp` | `refusedBulkCloseIsAtomicAndDoesNotCloseNewTabs` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。批量拒绝保持模型，确认中新标签不在原关闭集合内且须幸存。 |
| `WS/workspace_test.cpp` | `refusedCloseAllPreservesEveryFamily` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。全关拒绝保留家庭、内容和快照，不发最终保存/关闭通知。 |
| `WS/workspace_test.cpp` | `refusedFamilyCloseLeavesEveryWindowAndContentAlive` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。拒绝关闭时家庭全部模型、内容/窗口存活，允许后全部释放，Quick 可保留身份和生命周期断言。 |
| `WS/workspace_test.cpp` | `repeatedCrossWindowMovesDoNotRecreateOrDestroyContent` | C | 3.4 已合并到 quick-workspace-content 的 `liveEditorsSurviveWindowAndPaneTransfers`；原 Widgets 用例退役，映射和证据见 3.4 进度。 |
| `WS/workspace_test.cpp` | `restoreFactoriesMayDestroyTheirManager` | C | 补 WS/quick_content_host_test.cpp 与 quick_workspace_content_test.cpp 的内容契约。旧工厂第1/2次返回前销毁管理器，要求恢复失败、全部准备内容同步销毁。Quick 工厂独立于恢复事务，须明确等价生命周期入口。 |
| `WS/workspace_test.cpp` | `restoreFactoryMayReplaceItselfWithoutDestroyingItsCaptures` | C | 补 WS/quick_content_host_test.cpp 与 quick_workspace_content_test.cpp 的内容契约。一次恢复中后续窗格使用替换工厂，原捕获活到首回调返回，且正文正确。Quick 工厂尚无同等完整覆盖。 |
| `WS/workspace_test.cpp` | `restoredWindowTitlesSurviveFreshAndExistingHosts` | A | 原套件保留，迁 External，内容/窗口存活与身份按需接 QuickWorkspace。恢复新/既有窗口标题及 window.new 的标题来源映射至 Quick 窗口，保留字符串断言。 |
| `WS/workspace_test.cpp` | `splittingAndExtractingMovesExistingPaneWidgets` | C | 3.4 已合并到 quick-workspace-content 的 `liveEditorsSurviveWindowAndPaneTransfers`；原 Widgets 用例退役，映射和证据见 3.4 进度。 |
| `WS/workspace_test.cpp` | `validRestoreCreatesContentsForExistingStableIds` | C | 补 WS/quick_content_host_test.cpp 与 quick_workspace_content_test.cpp 的内容契约。同 ID 恢复要求重建内容、恰好调用工厂、保留正文/窗口并释放旧实例。Quick 按稳定 PaneId 保留内容，可能行为不同，不能仅换获取接口。 |

##### Lua 适配

| 源码 | 用例 | 类别 | 去向与依据 |
| --- | --- | --- | --- |
| `Lua/script_host_test.cpp` | `appearanceAndComponentBarsUseTypedDescriptions` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。文字栏 API、主题/设置类型与值以及内容身份可保留，不检查 QLabel 外观。 |
| `Lua/script_host_test.cpp` | `cancellingANestedHostCallRejectsStaleReplies` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。取消后旧回复不得继续副作用，后续上下文查询仍可执行。 |
| `Lua/script_host_test.cpp` | `cancellingModuleLoadingDoesNotCacheOrContinueTheModule` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。取消加载后不得继续改名或缓存模块，后续任务可执行。 |
| `Lua/script_host_test.cpp` | `componentServicesFollowProviderLifetime` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。服务在 GUI 线程执行，provider 销毁后拒绝调用，原结果断言保留。 |
| `Lua/script_host_test.cpp` | `customChromeIsReleasedWithItsScriptGeneration` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。脚本卸载清理工具栏、状态项、命令与资源计数，无控件断言。 |
| `Lua/script_host_test.cpp` | `eventCallbacksAreBoundedAndCanUnsubscribe` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。保留事件退订、队列上限错误与积压资源释放断言。 |
| `Lua/script_host_test.cpp` | `eventCallbacksPreserveOrderAndReleaseQueuedValues` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。保留事件顺序、退订、队列字节与回调释放断言。 |
| `Lua/script_host_test.cpp` | `exactShortcutAliasesUpdateArgumentsWithoutDuplicates` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。检查精确别名去重、后续按键收到新实参及卸载恢复，改 Quick 输入入口。 |
| `Lua/script_host_test.cpp` | `explicitCommandContextDoesNotFollowLaterFocus` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。显式上下文关闭原标签，不受后来活动标签影响，仅检查模型。 |
| `Lua/script_host_test.cpp` | `failingPeriodicCallbacksStopAndReleaseTheirReferences` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。定时/事件回调失败后停止并释放引用，检查错误与资源计数。 |
| `Lua/script_host_test.cpp` | `foldersApiFollowsTheModel` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。Lua 标签夹增删、改名、折叠、颜色及无效输入只依赖模型与返回值。 |
| `Lua/script_host_test.cpp` | `hierarchyConversionsKeepTheSamePaneObjects` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。层级转换后的 PaneId、内容对象身份与模型有效性可通过 QuickWorkspace::content 保留。 |
| `Lua/script_host_test.cpp` | `initializationFailureReleasesPartialRegistrations` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。初始化失败释放部分命令、状态项与回调，并允许再次初始化。 |
| `Lua/script_host_test.cpp` | `inputMethodScopeSettingsAreTypedAndAtomic` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。六种作用域值与混合非法更新的原子拒绝，不操作实际输入法。 |
| `Lua/script_host_test.cpp` | `internalShortcutPrefixAliasesAreRejected` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。绑定描述须在修改两个命令前拒绝前缀别名，检查绑定与资源计数。 |
| `Lua/script_host_test.cpp` | `invalidArgumentsDoNotPartiallyReplaceBindings` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。错误绑定与显隐参数原子拒绝，检查绑定和管理器偏好。 |
| `Lua/script_host_test.cpp` | `layoutOperationsKeepContentIdentity` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。Lua 改名、侧栏位置、面板策略与拆窗后的内容身份可由 QuickWorkspace 保留。 |
| `Lua/script_host_test.cpp` | `layoutSnapshotsPreserveEmptyArrays` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。布局往返保持模型空数组与稳定标签 ID，工厂接 QuickWorkspace。 |
| `Lua/script_host_test.cpp` | `luaCommandsSupportMultipleChordBindings` | C | 补 WS/quick_workspace_content_test.cpp，并保留 Lua 宿主集成入口。原例同时验证多长度组合键、Lua 回调次数与编辑器无字符。Quick shortcutsRunInTheirQuickWindow 仅检查单快捷键窗口上下文，缺完整组合键/可编辑内容断言。 |
| `Lua/script_host_test.cpp` | `nestedCoroutinesCanCallTheWindowHost` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。嵌套协程宿主调用、yield 多值及改名结果不依赖控件。 |
| `Lua/script_host_test.cpp` | `nestedModuleLoadingCanYieldThroughUserCoroutines` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。嵌套模块/协程调用宿主返回同一标签并改名，无控件断言。 |
| `Lua/script_host_test.cpp` | `nonyieldableModuleLoadingHasNoHostSideEffects` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。不可 yield 的元方法/比较器不得调用宿主，后续合法加载仅调用一次。 |
| `Lua/script_host_test.cpp` | `osExitUsesTheHostAndDoesNotTerminateAnExternalHost` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。os.exit 经宿主服务，缺失服务报错，拒绝退出保留模型与返回值。 |
| `Lua/script_host_test.cpp` | `overloadedRuntimeDoesNotStrandEventResources` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。过载后订阅、拒绝队列与延迟事件资源释放不依赖呈现。 |
| `Lua/script_host_test.cpp` | `panelPoliciesCanBeConfiguredWithoutNestedExecution` | C | 补 WS/quick_popup_menu_test.cpp 的 Lua 面板请求行为。直接检查 FloatingPanel visible/modal/dismissOnOutsidePress，非法选项不建面板。Quick palettesRunTheirChoiceFromTheKeyboard 未覆盖两种策略与 Lua 非法参数路径。 |
| `Lua/script_host_test.cpp` | `registrationObserversCanDestroyTheScriptHost` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。注册通知中销毁宿主后撤销命令并保持模型有效。 |
| `Lua/script_host_test.cpp` | `rejectedRegistrationsReleaseFunctionReferences` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。非法命令/定时器注册循环须释放 Lua 函数引用与资源。 |
| `Lua/script_host_test.cpp` | `reloadReplacesFunctionsAndRestoresUnderlyingBindings` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。各代重载的函数值、绑定增长与卸载恢复属于宿主契约。 |
| `Lua/script_host_test.cpp` | `reloadingToolbarsPreservesPerWindowAndKindLayouts` | C | External 下撤下脚本工具栏后保存布局仍包含已移除的工具栏，3.3 首次运行保留原断言失败。原 Widgets 夹具保留，产品缺陷交 Claude 处理，见 [交接 Q1](../../development/handoff.md#请下一位回答)。 |
| `Lua/script_host_test.cpp` | `sameCommandPrefixBindingsAreAtomicAndRestoreOnUnload` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。保留短长前缀冲突、replace 原子性、按键实参及卸载恢复断言，改向 Quick 窗口送键。 |
| `Lua/script_host_test.cpp` | `scriptEffectsRespectCloseGuard` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。Lua 关闭标签/窗口遵守许可，只检查返回值与模型存活。 |
| `Lua/script_host_test.cpp` | `sharedShortcutContinuationsRemainReachable` | C | 补 WS/quick_workspace_content_test.cpp，并保留 Lua 宿主集成入口。共享续键四条路径与字符不泄漏到 QPlainTextEdit 需换 Quick 可编辑内容重建断言，现有二击例未覆盖分支可达性。 |
| `Lua/script_host_test.cpp` | `standardModuleLoadingPreservesLuaSemantics` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。合并全部数据行，检查 require 搜索器、缓存、返回值、错误与宿主可调用性。 |
| `Lua/script_host_test.cpp` | `standardModulesAllowUserSearchersAndBinaryChunks` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。自定义路径、预加载、二进制 chunk 与非 UTF-8 Lua 字符串属运行时契约。 |
| `Lua/script_host_test.cpp` | `standardModulesCanCallTheHostAtTopLevel` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。合并初始化/普通任务数据行，require 顶层调用宿主与单次加载后模型改名可保留。 |
| `Lua/script_host_test.cpp` | `standardModulesReportMissingFilesAndUnavailableCLibraries` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。缺失模块、C 库限制和旧宿主方法错误只检查脚本结果。 |
| `Lua/script_host_test.cpp` | `standardModulesUseUserPathsAndPackageLoaded` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。用户模块路径、目录模块、缓存与重载生命周期可直接保留。 |
| `Lua/script_host_test.cpp` | `stateAndLiveQueriesUseValuesOnly` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。脚本值、上下文、窗口/命令列表以及设置更新，无控件专属断言。 |
| `Lua/script_host_test.cpp` | `stickyTabSettingsAreTypedAndAtomic` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。置顶尺寸、独行与关闭保护类型及非法更新原子性不依赖 Widgets。 |
| `Lua/script_host_test.cpp` | `stickyTabsAreScriptable` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。置顶切换、排序计数及移至侧栏后的拒绝行为，仅依赖模型与 API。 |
| `Lua/script_host_test.cpp` | `timersAreCancelledOnUnloadAndOneShotsReleaseCallbacks` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。定时器执行、取消、卸载和引用释放不依赖控件。 |
| `Lua/script_host_test.cpp` | `titleClippingSettingsAreTypedAtomicAndPersistent` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。裁切设置类型、边界、部分更新原子性与快照往返是 Lua/管理器数据契约。 |
| `Lua/script_host_test.cpp` | `windowDisplayStateUsesSharedCommands` | A | 原套件保留，迁 External，内容身份及真实窗口按需接 QuickWorkspace。两种窗口数据行，QuickWorkspace 承载最大化、最小化、还原及内容身份的 Lua 断言。 |

##### 应用

| 源码 | 用例 | 类别 | 去向与依据 |
| --- | --- | --- | --- |
| `App/layout_file_test.cpp` | `invalidSettingResetsOnlyItsSection` | A | 原套件保留，保存/恢复夹具迁 External，控件工厂按需接 QuickWorkspace。合并两个设置区数据行，仅坏区重置、标签完整、原字节副本与用户消息路径保持。 |
| `App/layout_file_test.cpp` | `invalidStructureKeepsTheFile` | A | 原套件保留，保存/恢复夹具迁 External，控件工厂按需接 QuickWorkspace。非法 schema 拒绝恢复并保留原文件副本与错误消息。 |
| `App/layout_file_test.cpp` | `missingFileIsNotAnError` | A | 原套件保留，保存/恢复夹具迁 External，控件工厂按需接 QuickWorkspace。缺文件不报错、不恢复、不生成副本，无控件依赖。 |
| `App/layout_file_test.cpp` | `unreadableFileKeepsTheFile` | A | 原套件保留，保存/恢复夹具迁 External，控件工厂按需接 QuickWorkspace。无效 JSON 报错并保留逐字节副本，无控件行为断言。 |
| `App/layout_file_test.cpp` | `validFileRestoresWithoutCopy` | A | 原套件保留，保存/恢复夹具迁 External，控件工厂按需接 QuickWorkspace。有效布局恢复模型与非默认设置且不生成无效副本，为文件/管理器契约。 |
| `App/scripting_exit_child.cpp` | `main（阻塞 os.execute 后退出场景，非 QtTest）` | A | 保留子进程助手，管理器改 External，关联 scripting_test.cpp::blockedStandardCallDoesNotDelayProcessExit。main 接收状态目录，启动阻塞标准调用，blocked-ready 就绪后退出事件循环并析构脚本设置，写 shutdown-started/shutdown.json。只有这一个场景入口，参数/初始化/超时 exit code 是其错误分支，由 scripting_test.cpp::blockedStandardCallDoesNotDelayProcessExit 调用并判定耗时。 |
| `App/scripting_test.cpp` | `applicationRestorePreservesExplicitWindowTitle` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。初始快照夹具迁 External，真实应用恢复后保存的窗口 ID 与显式标题保持。 |
| `App/scripting_test.cpp` | `blockedReloadReportsThePendingStop` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。阻塞标准调用时报告停止等待诊断，解除阻塞后重新初始化，保留进程夹具。 |
| `App/scripting_test.cpp` | `blockedStandardCallDoesNotDelayProcessExit` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。保留退出子进程、关闭耗时小于2000ms、阻塞进程仍存活及释放清理断言。 |
| `App/scripting_test.cpp` | `directScriptTransportAllowsFilesLargerThanOneMiB` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。大于1MiB的文件经应用命令执行并返回标记，仅替换管理器夹具。 |
| `App/scripting_test.cpp` | `externalProgramArgumentsAreNotShellText` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。外部程序实参字面传递、中文/空格往返与完成资源释放，不依赖控件。 |
| `App/scripting_test.cpp` | `generatedConfigurationUsesRequestedLanguage` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。合并两语言数据行，生成文字、注册命令、绑定与再次生成不覆盖文件。 |
| `App/scripting_test.cpp` | `invalidUtf8ScriptsAreRejected` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。合并错误字节数据行，保留 UTF-8 诊断、无执行副作用与原字节断言。 |
| `App/scripting_test.cpp` | `launchCompletedObserverMayDestroyServices` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。完成观察者销毁服务时不得继续发布事件，子进程仍正确收到实参。 |
| `App/scripting_test.cpp` | `longRunningStatusOffersStopAndClearsAfterwards` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。只检查管理器状态项文字/命令、停止和后续任务，无 QLabel 断言。 |
| `App/scripting_test.cpp` | `malformedConfigurationKeepsBuiltinCommandsAndUserFiles` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。错误配置报告失败并保留内置命令、绑定与用户原文件。 |
| `App/scripting_test.cpp` | `missingConfigurationDoesNotCreateUserFiles` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。缺配置时文件策略与显式生成配置不依赖呈现。 |
| `App/scripting_test.cpp` | `notificationsHaveBoundedLifetimeAndPlainText` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。实际只断言通知计数上限、reset 清理及错误声音参数。接 QuickWorkspace 承载通知，保留服务断言。 |
| `App/scripting_test.cpp` | `osExitSavesTheActualApplicationLayout` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。真实 Muxora 子进程经 Lua 退出并保存模型/窗口，保留既有应用入口。 |
| `App/scripting_test.cpp` | `osExitUsesApplicationCommandAndHonorsClosePermission` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。拒绝退出时 application.exit 与许可各调用一次，标签/窗口模型保留。 |
| `App/scripting_test.cpp` | `reloadRetainsUserOverridesWithoutWritingIntermediateState` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。各代重载保持用户绑定和已保存字节，检查 Lua 值与文件。 |
| `App/scripting_test.cpp` | `removedCommandDefaultsDoNotAccumulate` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。纯 CommandRegistry/UserPreferences 的默认命令跟踪和持久化，原位保留。 |
| `App/scripting_test.cpp` | `runCommandAcceptsInlineAndFileScripts` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。行内/文件执行结果与冲突参数拒绝属于应用脚本入口契约。 |
| `App/scripting_test.cpp` | `standardLibraryHandlesChinesePathsAndEnvironment` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。Lua 标准库中文文件、环境变量与模块缓存集成不依赖 Widgets。 |
| `App/scripting_test.cpp` | `startupLoadsScriptCommandsBeforeUserBindings` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。脚本/用户绑定加载顺序、命令执行及原文件保持不依赖编辑器控件。 |
| `App/scripting_test.cpp` | `utf8ProcessManifestRetainsApplicationCompatibility` | A | 原套件保留，管理器夹具迁 External，通知或真实窗口按需接 QuickWorkspace。检查进程代码页与 manifest 条目，不创建管理器，原位保留。 |

#### 3.3 迁移进度

已完成全部 A 类。原行为断言与数据行保留；不能平迁或暴露产品缺陷的用例已改 C，保留原 Widgets 正文，未当成修复。接下来只做 3.4 第一组“内容与生命周期”，完成后等 Claude 审查。各源码证据如下。

| 源码 | 状态 | 验证 |
| --- | --- | --- |
| `WS/visibility_profile_test.cpp` | 全部迁到 External，窗口快照及内容工厂接入 Quick | [记录](../../development/verification-log/2026-11.md#可见性策略测试迁到-external2026-11-07) |
| `App/layout_file_test.cpp` | 全部迁到 External，完整快照由 Quick 窗口提供 | [记录](../../development/verification-log/2026-11.md#布局文件测试迁到-external2026-11-07) |
| `App/scripting_test.cpp` 及 `scripting_exit_child.cpp` | 全部迁到 External，通知及窗口快照接入 Quick | [记录](../../development/verification-log/2026-11.md#应用脚本测试迁到-external2026-11-07) |
| `Lua/script_host_test.cpp` | 39 项 A 已迁，工具栏卸载保存缺陷对应的一项改 C，等待 Claude 处理；原三项 C 保留 | [记录](../../development/verification-log/2026-11.md#lua-宿主的-a-类测试迁到-external2026-11-07) |
| `WS/workspace_test.cpp` | 22 项 A 已迁，13 项 C 保留 | [记录](../../development/verification-log/2026-11.md#工作区的-a-类测试迁到-external2026-11-07) |
| `WS/view_state_test.cpp` | 18 项 A 已迁，两个 C 主用例保留 | [记录](../../development/verification-log/2026-11.md#视图状态的-a-类测试迁到-external2026-11-07) |
| `WS/shortcut_dispatch_test.cpp` | 16 项 A 已迁，内置分割命令依赖 Widgets 的一项改 C，等待 Claude 处理；原九项 C 保留 | [记录](../../development/verification-log/2026-11.md#快捷键分发的-a-类测试迁到-external2026-11-07) |
| `WS/tab_group_test.cpp` | 原 16 项 A 及三个审定允许试迁的方向用例已迁，十项 C 保留 | [记录](../../development/verification-log/2026-11.md#标签组的-a-类测试迁到-external2026-11-07) |
| `WS/custom_chrome_test.cpp` | 两项管理器 A 已迁，两项纯注册表 A 原样保留，五项 C 保留 | [记录](../../development/verification-log/2026-11.md#自定义工具栏源码的-a-类测试迁到-external2026-11-07) |
| `WS/drag_presentation_test.cpp` | 四项 A 已是纯共用算法，无呈现依赖，原样保留并定向验证；其余 C 保留 | [记录](../../development/verification-log/2026-11.md#拖动导向的-a-类无需呈现迁移2026-11-07) |
| `WS/edge_tab_interaction_test.cpp` | 两项 A 已迁，内容身份及原快捷键事件接入 Quick；其余 C 保留 | [记录](../../development/verification-log/2026-11.md#侧边栏标签的-a-类测试迁到-external2026-11-07) |
| `WS/extension_api_test.cpp` | 唯一 A 已是纯注册表逻辑，原样保留并定向验证；六项 C 保留 | [记录](../../development/verification-log/2026-11.md#扩展绑定的-a-类无需呈现迁移2026-11-07) |
| `WS/hierarchy_conversion_test.cpp` | 两项管理器 A 已迁，四项模型 A 原样保留，三项 C 保留 | [记录](../../development/verification-log/2026-11.md#层级转换的-a-类测试迁到-external2026-11-07) |
| `WS/input_method_scope_test.cpp` | 八项 A 使用模型／假后端及 PaneId，管理器改 External，两个 C 正文保留 | [记录](../../development/verification-log/2026-11.md#输入法作用域的-a-类测试移除-widgets-夹具2026-11-07) |
| `WS/interface_appearance_test.cpp` | 两项 A 已是共用字体链逻辑，原样保留并定向验证；其余 C 保留 | [记录](../../development/verification-log/2026-11.md#字体链规则的-a-类无需呈现迁移2026-11-07) |
| `WS/localization_test.cpp` | 六项 A 无旧视图依赖，原样保留并定向验证；五项 C 保留 | [记录](../../development/verification-log/2026-11.md#本地化的-a-类无需呈现迁移2026-11-07) |
| `WS/memory_lifecycle_test.cpp` | 唯一 A 已迁，保留 12 轮替换释放，四项 C 正文保留 | [记录](../../development/verification-log/2026-11.md#布局替换释放的-a-类测试迁到-external2026-11-07) |
| `WS/menu_structure_test.cpp` | 四项 A 已使用 External 菜单描述树，原样保留并定向验证；其余 C 保留 | [记录](../../development/verification-log/2026-11.md#菜单描述的-a-类已使用-external2026-11-07) |
| `WS/movement_test.cpp` | 原三项 A 及三个方向试迁用例已迁；T 形鼠标焦点通知试迁失败，保持 C，七项 C 正文保留 | [记录](../../development/verification-log/2026-11.md#移动命令的-a-类测试迁到-external2026-11-07) |
| `WS/pane_bar_policy_test.cpp` | 唯一 A 已迁 External 模型标签，其余 C 保留 | [记录](../../development/verification-log/2026-11.md#栏共享策略的-a-类测试迁到-external2026-11-07) |
| `WS/peek_size_test.cpp` | 两项 A 已迁 External，原参数数据及 C 保留 | [记录](../../development/verification-log/2026-11.md#临时面板尺寸的-a-类测试迁到-external2026-11-07) |
| `WS/pinned_ui_test.cpp` | 一项 A 已迁；固定组创建依赖同一分割命令缺陷，改 C 并保留原用例，其余 C 保留 | [记录](../../development/verification-log/2026-11.md#固定组尺寸命令的-a-类测试迁到-external2026-11-07) |
| `WS/popup_menu_test.cpp` | 唯一 A 已是纯定位逻辑，原样保留并定向验证；其余 C 保留 | [记录](../../development/verification-log/2026-11.md#弹窗定位的-a-类无需控件夹具2026-11-07) |
| `WS/sidebar_presentation_test.cpp` | 两项 A 已迁，显隐快照及实际侧栏查询接入 Quick，三个 C 保留 | [记录](../../development/verification-log/2026-11.md#侧栏显隐的-a-类测试迁到-external2026-11-07) |
| `WS/status_bar_test.cpp` | 两项 A 已迁，逐窗口偏好快照接入 Quick，其余 C 保留 | [记录](../../development/verification-log/2026-11.md#状态栏偏好的-a-类测试迁到-external2026-11-07) |
| `WS/surface_transfer_test.cpp` | 唯一 A 已迁，每次转移完成 Quick 投影再处理延迟释放，另一项 C 保留 | [记录](../../development/verification-log/2026-11.md#边缘转移释放的-a-类测试迁到-external2026-11-07) |
| `WS/tab_appearance_test.cpp` | 八项配置／命令 A 已迁 External，六项 C 保留 | [记录](../../development/verification-log/2026-11.md#标签配置的-a-类测试迁到-external2026-11-07) |
| `WS/tab_bar_presentation_test.cpp` | 唯一 A 已迁 Quick，关闭前几何保存断言保留，其余 C 保留 | [记录](../../development/verification-log/2026-11.md#最后关闭保存几何的-a-类测试迁到-quick2026-11-07) |
| `WS/tab_creation_test.cpp` | 九项 A 已迁，非法恢复的工厂检查接入 Quick，三个 C 保留 | [记录](../../development/verification-log/2026-11.md#新建标签的-a-类测试迁到-external2026-11-07) |
| `WS/toolbar_profile_test.cpp` | 六项 A 已迁；完成通知内同步保存缺窗口记录的一项改 C，等待 Claude 处理，三个 C 正文保留 | [记录](../../development/verification-log/2026-11.md#工具栏档案的-a-类测试迁到-external2026-11-07) |
| `WS/toolbar_reset_test.cpp` | 唯一 A 已迁 External，其余 C 保留 | [记录](../../development/verification-log/2026-11.md#工具栏重置的-a-类测试迁到-external2026-11-07) |
| `WS/view_commands_test.cpp` | 唯一 A 已迁 External 模型夹具，其余 C 正文保留 | [记录](../../development/verification-log/2026-11.md#命令-enabled-重入的-a-类测试迁到-external2026-11-07) |
| `WS/view_reuse_test.cpp` | 唯一 A 已迁 Quick，显隐快照与合并批次断言保留，其余 C 正文保留 | [记录](../../development/verification-log/2026-11.md#内容显隐批次的-a-类测试迁到-quick2026-11-07) |
| `WS/window_placement_test.cpp` | 五项 A 已迁或保持纯逻辑；初次显示夹具的一项及三项暴露位置通知／缓存缺陷的用例改 C，七项 C 正文保留 | [记录](../../development/verification-log/2026-11.md#窗口位置的-a-类测试迁到-quick2026-11-07) |

#### 3.4 内容与生命周期进度

进行中，只处理审定的第一组，完成后等 Claude 审查。3.2 清单保留原用例名和类别作为对应关系，已合并的行更新去向，不再计作仍待补的缺口。

| 功能 | 状态 | 证据与原用例映射 |
| --- | --- | --- |
| 编辑内容的身份、状态与输入 | 六项 workspace C 合并为两个 Quick 行为用例，原用例退役 | [记录](../../development/verification-log/2026-11.md#内容状态与转移的-c-类合并到-quick2026-11-07) |

收尾（Claude）：核对 [浮窗设计](../../design/floating-window-design.md)、[窗口交互细则](../../design/window-interaction-design.md) 和标签栏设计里的行为在 Quick 版都有；用户试用；全量测试先取得单独授权；更新设计文档里关于 Widgets 的描述和预览说明。未完成全界面迁移、试用与帧测量时，不宣称整体 GPU 化或达到 GPUI 同级性能。
