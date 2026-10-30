# 阶段 10：保持外观与交互的 Qt Quick 迁移

上游：[ADR 0013](../../adr/0013-qt-quick-rendering.md)、[Quick 呈现设计](../../design/quick-rendering-design.md)。本阶段按用户要求优先推进，原有未完成步骤保留。

## 10.1 基线、帧动画和保留绘制基础

**状态**：绘制与帧基础已实现并定向验证，尚未接入默认应用。主体阴影的冷资源、保留纹理与尺寸顶点更新已定向验证；接合阴影的 [自有冷资源](../../development/verification-log/2026-10.md#接合阴影的自有冷资源2026-10-28) 已分离，主体与单接合补片的 [GPU 合成](../../development/verification-log/2026-10.md#quick-接合阴影的保留合成2026-10-28) 已定向验证，文字和完整应用的呈现测量未完成，继续 10.2；不能据此视为整体高刷新验收。结果见 [基线记录](../../development/verification-log/2026-10.md#按窗口帧取样的动画值与迁移前基线2026-10-27)、[阴影资源](../../development/verification-log/2026-10.md#主体阴影资源与绘制后端分离2026-10-28)、[主体阴影 GPU 记录](../../development/verification-log/2026-10.md#quick-主体阴影保留纹理与连续缩放2026-10-28) 和 [GPU 记录](../../development/verification-log/2026-10.md#quick-保留绘制与屏幕刷新验证2026-10-27)。

- 保存当前 Widgets 分隔条和绘制基线；把动画时长与曲线从 QWidget 适配中分离，增加按单调帧时间推进的值动画。
- 增加可选 Quick 构建目标和有界路径几何；建立 `QQuickItem` 到保留场景图节点的同步，复用已有接合轮廓。
- 检查四方向、非凸轮廓、DPI、重定向动画、资源生命周期和空闲停止；分别记录逻辑成本和 GPU 帧提交。
- 完成条件：基础接口与定向检查可运行，无逐帧整窗栅格化，未把基础渲染测量写成完整应用性能。

## 10.2 分离标签交互与呈现

**状态**：进行中。标签栏的状态、布局、输入和动画已集中到 Gui 层的 `TabStripController`，Widgets 的 `TabBar` 和 Quick 的 `TabBarItem` 只做适配，见 [控制器记录](../../development/verification-log/2026-10.md#标签状态与-widgets-门面分离2026-10-30)。Quick 已有标签表面、标题、图标、编号、文件夹头、条带装饰、边缘渐隐和拖动前景的保留绘制组件，基础层与前景层经统一帧发布接到控制器，见 [统一发布](../../development/verification-log/2026-10.md#标签栏基础与前景的统一发布2026-10-29) 和 [审查记录](../../development/verification-log/2026-10.md#gpt-审查-deepseek-提交标签合成与运行段资源2026-10-29)。

未完成：尾部控件和完整应用窗口接线；离屏合成时的字体外观待用户试用。各子步的改动和验证按日期记在 [验证日志](../../development/verification-log/2026-10.md)。

- 从 `TabBar` 分离输入、滚动、展开、拖拽与动画状态，保留已有布局计算；Widgets 和 Quick 读取同一份呈现数据，不复制规则。
- 把 `TabDragVisual` 中的 QPainter 回调改成有界渲染描述，明确资源所属窗口。
- 标签卡片、阴影、字形、图标、渐隐与命中保持现有设计。验证各方向、置顶尺寸、多行与独立置顶行、拖入拖出、右侧镜像及收起浮层。

## 10.3 Quick 窗口、内容与原生交互

**状态**：进行中，2026-10-30 起由 Claude 负责。已有部件：共用分割布局与 `SplitTreeItem`、`GroupSurfaceItem` 背板、接到控制器的 `TabBarItem`、`ContentHost` 内容宿主、以 QWindow 为核心的原生框架与系统菜单，见 [分割记录](../../development/verification-log/2026-10.md#quick-组与窗格的共用分割布局2026-10-29) 和 [收尾记录](../../development/verification-log/2026-10.md#quick-输入qwindow-与内容宿主收尾2026-10-30)。组容器、整窗和内容提供者还没有接通。

用户 2026-10-30 明确：整个界面必须完全迁移到 Quick，Widgets 路径在 10.6 删除。迁移期间两个后端共存，按下面的结构过渡。

**过渡结构**

- `WindowManager`、`LayoutModel`、命令、布局事务和 `DragController` 的判断规则只有一份。它们目前直接调用 `WorkspaceWindow`、`DockSurface` 和 `GroupTabs`；先把这些调用收拢成两个内部接口：窗口（标题栏、拖动形态、放置）和停靠表面（重建、命中矩形、插入预览、提起与落位）。Widgets 和 Quick 各实现一份，10.6 删除 Widgets 实现时接口随之并回 Quick 类，不保留抽象层。
- 拖动和原生移动改用 `QWindow`：两种后端的顶层窗口都有 `QWindow`，捕获、`startSystemMove()`、坐标换算只依赖它。
- 后端在创建 `WindowManager` 时选定，整个进程只用一种。Muxora 用启动参数 `--quick` 选择 Quick，默认仍是 Widgets，直到 10.6。
- 宿主内容：`ContentFactory` 和 `addTab`、`splitPane` 增加 `QQuickItem` 版本，Quick 后端只接受这一种；栏位（`setTabTopBar` 等）同样。Muxora 的占位内容改写为 Quick 项。
- 不在 Quick 窗口里嵌 QWidget，也不用原生子窗口遮挡 Quick 浮层。

**子步**（按顺序，每步可单独构建和验证）

0. 先修好 Widgets 路径上已有的失败：交出时 7 个套件 20 个用例失败（拖出、侧栏拖放、视口锚定、置顶等），清单见 [记录](../../development/verification-log/2026-10.md#标签组规则移到共用控制器发现已有的-widgets-失败2026-10-30)。标签组规则已移到共用的 `TabGroupController`。
1. 标签组容器 `GroupItem`：`TabBarItem`、`GroupSurfaceItem`、内容区和竖栏尾部，布局用 `layoutTabGroup`，表面与焦点动画用共用帧状态；组按 `GroupId` 复用，内容由 `ContentHost` 挂载。
2. 停靠表面 `DockSurfaceItem`：中央与固定区域的分割树、侧栏轨道、悬停展开面板，按模型重建并保留未变的组。
3. 整窗 `WorkspaceQuickWindow`：`QQuickWindow` 加原生框架、标题栏、工具栏占位与状态栏；最大化、全屏、Snap、DPI 和窗口族规则与现有窗口一致。
4. 抽出两个内部接口，`WindowManager` 按选定后端创建窗口；Muxora `--quick` 能打开、关闭、切换标签和分割。
5. 拖动：栏内换位、拖出成浮窗、跨窗口停靠、拖入侧栏，全部经 `QWindow` 完成。
6. 宿主内容和栏位的 Quick 接口，Muxora 占位内容迁移；输入法、焦点、显隐通知与按需创建保持。
7. 完成条件：`--quick` 下两个组、窗格、分隔条和标签跨窗口移动都能完整使用；缺项不切换默认。

## 10.4 工具栏、侧栏、菜单与设置

**状态**：未开始，需 10.3。弹出菜单、命令面板和设置页同样改为 Quick 窗口，不保留 `QMenu` 或 QWidget 对话框。

- 迁移剩余 chrome、侧栏浮层、命令面板、设置与快捷键页，复用命令、颜色、尺寸和已有外观。
- 保留工具栏弹性/压缩/重置、分层菜单、状态项、布局保存及主窗口和浮窗设置区分。
- 迁移可访问性、键盘与 IME 接线，保留平台菜单和窗口系统行为。

## 10.5 帧成本与资源调优

**状态**：整体调优未开始；各前置步骤发现的局部成本当步处理。分隔条尺寸更新与实际比例提交的定向对照已完成，见 [测量记录](../../development/verification-log/2026-10.md#分隔条比例提交路径的成本2026-10-28)。多标签的无效接合轮廓计算已减少，单标签组分隔条卡顿仍待后续迁移，见 [取样与分隔条对照](../../development/verification-log/2026-10.md#标签接合轮廓只为使用者生成2026-10-28)。Quick 基准已加入 30 标签整栏场景，三轮 p95 为 7.43–8.01 ms，仍高于 144 Hz 单周期预算，见 [整栏基准](../../development/verification-log/2026-10.md#quick-三十标签整栏基准2026-10-29)；该场景不含宿主内容和输入，不能代表完整应用。

- 对照基线测量分隔条、标签拖动/切换、多行展开、侧栏展开及多窗口。
- 优化节点保留、局部更新、批次、文字/阴影缓存和上传；在目标刷新周期内控制 CPU、GPU 与同步成本，不停用视觉效果换取成绩。
- 检查高刷新、多屏混合刷新/DPI、隐藏恢复及反复关闭后的资源回落，性能结论按实际运行记录。

## 10.6 等价验收与默认切换

**状态**：未开始，需 10.2–10.5。

- 核对全部既有行为清单、相关套件和用户试用，全量测试先取得单独授权。
- 部署依赖与发行许可跟随真正使用的模块更新，固定预览不依赖开发 SDK。
- 完成后切换默认呈现，删除被替换的重复绘制和过渡入口。未完成全界面迁移、试用与帧测量时，不宣称整体 GPU 化或达到 GPUI 同级性能。
