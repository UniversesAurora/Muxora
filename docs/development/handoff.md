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

## 2026-11-06 · Claude：10.4e–i 完成，Quick 外观对齐 Widgets 参考版

- **最后提交**：`main` 已推送，与 `origin/main` 一致；工作区干净。
- **本轮完成**（上次交接后 22 个提交加本节；每项的改动、排查和实际运行结果见链接）：
  - 审查 Codex 的 10.3 子步 6 至 10.4e 提交并修正，见 [审查记录](verification-log/2026-11.md#审查-codex-的-103-子步-6-至-104e-提交2026-11-05)；10 月日志里错放的 11 月记录已移回。
  - 10.4e 工具栏完成：[编辑与写回](verification-log/2026-11.md#quick-工具栏编辑与布局写回2026-11-05)、[区域菜单与选项菜单](verification-log/2026-11.md#quick-工具栏区域菜单与选项菜单2026-11-05)、[手柄拖动](verification-log/2026-11.md#quick-工具栏手柄拖动2026-11-05)、[悬停提示](verification-log/2026-11.md#quick-悬停提示2026-11-05)。
  - 10.4f [状态栏](verification-log/2026-11.md#quick-状态栏2026-11-05)；10.4g [命令面板与窗口选择器](verification-log/2026-11.md#quick-命令面板与窗口选择器2026-11-05)、[拖动中按 Tab 选目标](verification-log/2026-11.md#quick-拖动中按-tab-选目标窗口2026-11-05)。
  - 10.4h 中 Claude 的部分：[设置页与表单控件](verification-log/2026-11.md#quick-设置页与表单控件2026-11-06)、[标签配色编辑](verification-log/2026-11.md#quick-设置页的标签配色编辑2026-11-06)、[输入框聚焦清空的修复](verification-log/2026-11.md#quick-设置页输入框聚焦后清空2026-11-06)。
  - 10.4i [对话框](verification-log/2026-11.md#quick-对话框2026-11-06)（管理器的 `openDialog`，不阻塞调用方）和 [选择菜单](verification-log/2026-11.md#quick-下的选择菜单2026-11-06)（Quick 下不再弹 Widgets 菜单，选择沿用原命令上下文）。
  - Muxora [文件页的地址栏和信息栏](verification-log/2026-11.md#muxora-文件页的地址栏和信息栏2026-11-06)。
  - 按用户指出的标签问题逐像素对齐 Widgets：[窗口先排好再显示](verification-log/2026-11.md#quick-窗口启动时标签先分离再接合2026-11-06)、[顶部边框与菜单项高度](verification-log/2026-11.md#quick-窗口顶部边框和菜单项高度对齐-widgets2026-11-06)、[工具栏按钮与菜单栏选项按钮](verification-log/2026-11.md#quick-工具栏按钮宽度和菜单栏选项按钮对齐-widgets2026-11-06)、[菜单标题被截断](verification-log/2026-11.md#quick-菜单栏标题被截断2026-11-06)、[访问键下划线](verification-log/2026-11.md#quick-菜单的访问键下划线照-windows-规则显示2026-11-06)。
  - `quiet-desktop.ps1` 恢复窗口时仍按原顺序恢复，但不再检查顺序（用户决定，见 [记录](verification-log/2026-11.md#桌面隔离恢复时不再检查窗口顺序2026-11-05)）。
  - 验证范围：每步只跑相关套件，最后一次全部 21 个 Quick 套件、`external-presentation` 和应用的 `localization` 在 `ec66ca3` 之前的 `5d33232` 上通过，`ec66ca3` 之后跑了菜单相关的 9 个套件；翻译检查和文档检查通过。没有跑全量，没有在应用里手动试用。
- **进行中**：无。10.4h 剩下的部分属于执行者，见下一步。
- **下一步**（给执行者，按顺序；每项的要求写在 [阶段 10](../roadmap/implementation/phase-10-qt-quick.md#104-工具栏侧栏菜单与设置) 对应条目里）：
  1. 先读 [助手协作经验](lessons/collaboration.md)、[Qt Quick 经验](lessons/qt-quick.md) 和阶段 10 的 10.4 状态段，特别是“和 Widgets 版对照的标准”。
  2. **10.4h 其余分类**：把 `settings/settings_view_options.cpp`（“显示”分类 `window/display`）、`settings/settings_tab_appearance.cpp` 里标签外观的其余设置（放进已有的 `window/tabs` 分类、排在标签配色前面）和“置顶标签”分类（`window/tabs/sticky`）改写成 `settings/setting_description.cpp` 里的描述，Quick 设置页自动显示。栏显隐只作用于设置页所在的窗口：给 `describeSettings` 加窗口参数，`quick::createSettingsContent` 从窗格所在窗口传入。控件类型按 [设置设计 §3](../design/settings-and-commands-design.md#3-全局设置与编辑) 选，已有类型够用；遇到现有类型表达不了的控件就停下，写进交接记录。用例加在 `quick-settings`：每个分类挑一两个设置，检查改动写回管理器、别处的改动回到控件、不可编辑时控件禁用（例如标题裁切关闭时长度控件）。
  3. **10.4h 快捷键页**：照 `settings/shortcuts_page.cpp` 做 Quick 页面，用已有的 `SettingTable`（命令列表、绑定列表）、`SettingKeySequenceField`（录键，录键时不触发快捷键已经做好）、`SettingButton`、`SettingTextArea`（参数 JSON）、`SettingTextField`（搜索）和 `SettingsStyle`。先把冲突与前缀提示、参数校验、`runCommands` 校验这些规则从 `ShortcutsPage` 移到不依赖控件的代码里，两边共用（Widgets 页改为调用它，跑一次 `shortcut-settings` 确认行为不变）。内容按 `window_system.shortcuts` 类型由 `quick::createSettingsContent` 创建（现在这个类型在 Quick 下是空白标签）。用例加在 `quick-settings`：搜索、按键搜索、录入并应用一条绑定、冲突时不能应用、删除绑定。
  4. **10.4k 菜单栏单按 Alt 的键盘导航**：要求和用例写在计划的 10.4k 条目里。
  5. **10.4j 无障碍**：按计划条目。
  6. 每项完成：更新计划状态、验证日志、变更记录和预览说明；只跑相关套件，桌面套件整批经 `quiet-desktop.ps1 -Elevate`。不要删 Widgets 代码：10.6 单元 2 已并入单元 3，等 Claude 拆好步骤。
  7. 碰到新界面组件或新外观就停下写进交接记录；复用现有控件和样式的接入自己做。
- **Claude 下次接手**：审查执行者的提交；拆 10.6 单元 3（管理器去掉 Widgets 呈现）为可单独提交的步骤；根据 10.5 的测量定优化方案。
- **待用户决定**：无新问题。之前就有的：8.27 菜单与浮动面板重做的具体要求；Quick 版整体试用（拖动、布局、视图模式、工具栏编辑、设置页、对话框）。Release 预览（`out/package/muxora-preview/`）仍是 11-04 的版本，没有更新，未打包。
- **请下一位回答**：无。
- **本轮新决定**（用户 2026-11-06，写在 [阶段 10 · 10.4](../roadmap/implementation/phase-10-qt-quick.md#104-工具栏侧栏菜单与设置)）：
  - Quick 的窗口框架、标签、菜单、工具栏等要和 Widgets 参考版一比一一致，以同一进程里的 Widgets 窗口为准。
  - Muxora 的占位内容页以后会重写，不要求还原；文字纵向位置只要合理，不要求逐像素一致。
  - 要有菜单栏单按 Alt 的键盘导航（10.4k）。
  - 10.6 单元 2 并入单元 3（Claude 根据现状改的计划，理由写在表里）。
- **环境与注意事项**：
  - **待清理（Codex，2026-11-06）**：系统临时目录下 `muxora-quick-frame-measure-20261106`，仅含本轮完整 Quick 帧采样与函数归因的源码、脚本和构建目录。已只读核实 63 个文件、无重解析点、无运行中的程序；递归删除被 `blocked by policy` 拦截，未给具体原因。按仓库删除受阻规则补充此项，Claude 下次接手时先核对目录、内容和用途，再删除；测量结论已写入 [验证日志](verification-log/2026-11.md#quick-完整窗口帧成本与阴影归因2026-11-06)。
  - 用 `Start-Process` 在 `quiet-desktop.ps1` 会话里启动应用截图或冒烟后，有两次桌面没有恢复，按提示单独跑 `quiet-desktop.ps1 -Elevate -Restore` 就好。
  - 应用的 `--screenshot` 截完不退出，要关窗口；它在启动后 700 毫秒拍，适合检查第一帧。和 Widgets 参考版对比时两边都用 `--fresh --state-dir <临时目录>`，不要碰参考包自带的 `data/`。
  - 测试进程崩溃时，`-o 文件` 的输出不完整，去掉 `-o`、把标准输出重定向到文件才能看到调用栈，见 [调试经验](lessons/debugging.md#测试进程崩溃时看标准输出里的调用栈)。
  - `out/diagnostics/quick-migration/` 和 `quick-tab-strip/` 仍是 10.5 的基准证据，保留；本轮的对比截图目录已删除。
  - 没有运行中的预览或测试。

## 2026-11-05 · Codex：Quick 菜单与统一工具栏布局，继续 10.4e

- **最后提交**：实现截至 `2baa71c`，本节随后的记录提交一起推送到 `main`；没有其他分支推送。用户要求立即交接，未再运行验证。
- **本轮完成**：
  - 10.4b–d：Quick 共用菜单、右键入口、标签列表、文本编辑、浮窗标题、动态状态、增量树、菜单栏及窄窗口收纳已接入；沿用原外观和交互，不是新增菜单设计。相关修订和各次实际结果见 [11 月验证日志](verification-log/2026-11.md)。
  - 10.4e：共用工具栏布局和 JSON 合并规则；工作区与宿主／Lua 命令栏；菜单、标题、窗口按钮统一注册表、上下区域、保存布局读取和内容显隐已接入。标题和空白区域共用拖窗输入，caption 保留独立视觉归属和原生命中。当前位置与范围见 [10.4e](../roadmap/implementation/phase-10-qt-quick.md#104-工具栏侧栏菜单与设置)。
  - 标题全文测量已移出 resize，缓存随标题、字体、字体数据库及 DPR 失效，见 [测量记录](verification-log/2026-11.md#quick-标题测量移出尺寸变化2026-11-05)。局部取样不能证明整窗帧率。
- **进行中**：10.4e 未完成。统一布局目前读取保存状态；管理器／Lua 的工具栏状态编辑仍主要走 Widgets，Quick 的实际编辑与保存写回、窗口种类的布局归属、手柄拖动、完整选项菜单和提示尚未接通。`keptStrips`／`keptInactiveStrips` 仍在，不能直接删除或宣称迁移完成。最终目标是彻底移除 Widgets 呈现，不保留薄适配作为最终方案。
- **下一步**：
  1. 按角色执行接手流程；Claude 接手时先审查本轮提交。先读工程、测试规范及 [Qt Quick 经验](lessons/qt-quick.md)，复用现有阶段计划。
  2. 继续 10.4e，接通当前注册表的状态查询、编辑、保存和按 WindowKind 区分的当前／非活动布局。入口看 `custom_chrome.cpp`、`chrome/toolbar_api.cpp`、`commands/toolbar_move_command.cpp`、`layout_persistence.cpp`、`WindowPresenter`；`WindowManager::initialToolbarLayout` 目前优先取按 WindowId 原样保留的布局，种类切换归属尚未接好。
  3. 使用 `strip_layout_state` 的合并规则保留迟加载提供者及多窗口编辑；不能让普通同步覆盖未提交的拖动预览，也不能让旧窗口覆盖新默认。再接手柄拖动、边界调整与取消，复用 `strip_layout` 的空位回收、弹性与优先权规则。
  4. 完整选项菜单接入时，合并菜单自己的 More 与 strip options，保留一个入口；当前菜单内容不额外创建 options tail。内容开关与菜单项排除的区别见 [统一记录](verification-log/2026-11.md#quick-窗口内容并入工具栏注册表2026-11-05)。完成条件满足后按 10.6 删除相应 Widgets 代码，后续仍有状态栏、面板、设置、对话框、无障碍和完整性能调优。
- **待用户决定**：无新的设计问题。真实外观／交互试用与完整应用高刷新验收仍未完成。Release 预览未更新，未打包。用户要求不要再做多余验证、立即交接；不准备对比测试或额外测试框架。
- **请下一位回答**：无独立问题，按接手审查结果继续。
- **本轮新决定**：现有菜单与工具栏按既有设计迁移，继续执行，不以新组件为由停工；未修改 AGENTS.md。全量／大回归仍须用户单独授权，不做静态外观测试或无必要哈希校验。
- **环境与注意事项**：
  - 最新四套相关检查同批通过：内容 12、菜单 38、工具栏 8、原生拖动 3；菜单内容开关修复后，仅定向复核工具栏 8 与菜单函数 8，均无失败或跳过。各次时间、首报失败、恢复经过及验证范围见 [统一记录](verification-log/2026-11.md#quick-窗口内容并入工具栏注册表2026-11-05)。未跑全量、大回归或外观检查。
  - `quick-toolbar` 已承接工具栏行为场景，`quick-popup-menu` 保留菜单场景；只是按职责拆开，未改超时或覆盖要求。字体／窗口控件函数也已定向通过。桌面曾首次恢复失败，单独管理员恢复成功，最近会话恢复成功；没有正在运行的检查。桌面检查仍须整批经 `quiet-desktop.ps1 -Elevate`；不得沿用用户之前清除旧快照的授权来清除新快照。
  - 不在桌面隔离会话内编译；只运行已准备好的目标，避免编译服务留在 runner 后代树中。
  - **待清理，禁止 Codex 绕过策略**：系统临时目录下 `Muxora-toolbar-measure-bb214121144a4599883aa3886ace6300`（此前工具栏取样）与 `muxora-title-cost-54652cf54f054cdfb7f6545378a43dcd`（本轮标题取样）递归删除被 `blocked by policy` 拦截，均未给具体原因。本轮目录已只读核实仅有 `title_cost.cpp`、`title_cost.obj`、`title_cost.exe`，无子目录，已给用户绝对路径及无需管理员权限的手动命令。Claude 删除前再核对实际目录与内容，确认无用户数据；前一目录按原记录核查，不盲删。没有将临时测量源码加入项目。

## 2026-11-04 · Claude：Quick 迁移推进，Muxora 改为只用 Quick 启动

- **最后提交**：`main` 已推送，与 `origin/main` 一致；工作区干净。另推送了标签 `widgets-reference`，指向切换前最后一个提交。
- **本轮完成**（10-30 交接之后约 75 个提交，每项的改动和验证见链接）：
  - 10.3 子步 1–5：Quick 标签组、停靠表面（中央、固定区域、自动隐藏侧栏和展开面板）、整窗原生框架与标题行、管理器 External 模式，见 [工作区](verification-log/2026-10.md#quick-工作区按模型建窗口2026-10-31)、[固定区域](verification-log/2026-10.md#quick-停靠区的固定区域2026-10-31)、[侧栏](verification-log/2026-10.md#quick-停靠区的自动隐藏侧栏2026-10-31)、[标题栏](verification-log/2026-10.md#quick-窗口的标题栏和原生框架2026-10-31)。
  - 拖动全部对齐 Widgets（只差按 Tab 选目标，挪到 10.4）：[栏内与跨窗口](verification-log/2026-11.md#quick-标签在栏内组间和窗口间拖动2026-11-01)、[拖出浮窗](verification-log/2026-11.md#quick-标签拖出成浮窗2026-11-01)、[系统移动与拖标题停靠](verification-log/2026-11.md#quick-拖动交给系统移动浮窗拖标题可停靠2026-11-03)、[目标栏画载体](verification-log/2026-11.md#quick-标签栏上方由目标栏画载体2026-11-03)、[标签夹](verification-log/2026-11.md#quick-标签夹拖出成浮窗2026-11-03)、[侧栏](verification-log/2026-11.md#quick-侧栏的拖入和拖出2026-11-03)、[落位动画](verification-log/2026-11.md#quick-标签落下时滑到位2026-11-04)、[侧栏载体](verification-log/2026-11.md#quick-侧栏上方画出侧栏按钮2026-11-04)、[标签夹停靠](verification-log/2026-11.md#quick-拆出的标签夹放到引导和侧栏上2026-11-04)、[唤起目标](verification-log/2026-11.md#quick-拖动中唤起目标窗口2026-11-04)、[滚轮](verification-log/2026-11.md#quick-拖动中滚轮滚动指针下的标签栏2026-11-04)、[内容预览](verification-log/2026-11.md#quick-拖到标签栏上时显示内容画面2026-11-04)。
  - 窗口层面：[面板手柄与标题拖动](verification-log/2026-11.md#quick-展开面板的尺寸手柄和标题拖动2026-11-04)、[显示与激活（`WindowPresenter`）](verification-log/2026-11.md#quick-窗口接收管理器的显示和激活2026-11-04)、[视图模式与栏显隐](verification-log/2026-11.md#quick-窗口按视图状态显示2026-11-04)、[标题与窗口族](verification-log/2026-11.md#quick-窗口标题跟随活动标签2026-11-04)、[布局保存与恢复](verification-log/2026-11.md#quick-恢复和保存布局2026-11-04)。
  - 修好此前一直失败的套件：[Debug 下失败的套件](verification-log/2026-11.md#debug-下失败的三个套件2026-11-03)、[`menu-structure`](verification-log/2026-11.md#menu-structure-跟上-10-月-26-日的标签行设置2026-11-04)。
  - [切换](verification-log/2026-11.md#muxora-切换为只用-quick-启动2026-11-04)：Muxora 只用 Quick 启动；原 Widgets 版的 Release 包放在 `out/package/muxora-widgets-reference/` 供对比；日常 Release 预览已按 `d966723` 重新部署。
  - [后续计划](../roadmap/implementation/phase-10-qt-quick.md#剩余工作与分工2026-11-04-起)：剩余工作、顺序、负责人和每一项的完成标准都写在阶段 10，10.6 改成分块删除 Widgets 的五个单元。
  - 验证范围：每步只跑相关套件，Quick 16 个套件最后一次全部通过；没有跑全量，没有在应用里试用。
- **进行中**：无。10.3 子步 6 尚未开始。
- **下一步**（给执行者，按 [剩余工作与分工](../roadmap/implementation/phase-10-qt-quick.md#剩余工作与分工2026-11-04-起) 表里 1–5 的顺序；每项的范围、完成标准和要跑的套件写在对应子步里）：
  1. 先读 [助手协作经验](lessons/collaboration.md) 和阶段 10 的“剩余工作与分工”。
  2. 删除 Widgets 占位内容（10.6 单元 1），`localization` 测试改为检查文字函数和 QML 占位页。
  3. 10.3 子步 6：标签和窗格的上下栏接口、Quick 内容的焦点与输入法作用域。Muxora 文件标签的两条栏要新外观，留给 Claude。
  4. 10.4a 命令菜单数据模型；10.4e 前半工具栏排布规则抽离。这两项改到 Widgets 代码，行为不变的依据是对应的 Widgets 套件，这几个要跑。
  5. 10.5 的工具项：`quiet-desktop.ps1` 运行期间保持屏幕亮着。
  6. 碰到新界面组件或新外观（菜单、工具栏、状态栏、对话框、设置控件的样子）就停下，写进交接记录，由 Claude 做 10.4b–i。
- **待用户决定**：无新问题。之前就有的：8.27 菜单与浮动面板重做的具体要求仍等用户补充；Quick 版的拖动、布局和视图模式等用户试用。
- **请下一位回答**：无。
- **本轮新决定**：
  - 用户 2026-11-04：Muxora 现在就切到 Quick；先保留一份能启动原版的 Release 包用来对比，之后不再做 Widgets 版。记在 [阶段 10 · 10.3](../roadmap/implementation/phase-10-qt-quick.md#103-quick-窗口内容与原生交互) 的切换决定和“剩余工作与分工”。
  - 由此定下：Widgets 套件不再运行；每补齐一块 Quick 功能，就删掉对应的 Widgets 代码和用例（10.6 的删除单元）。
  - 用户 2026-10-30 定的“整个界面必须完全迁移到 Quick”仍然有效。
- **环境与注意事项**：
  - **屏幕休眠**：休眠时 Qt Quick 不出新帧，依赖动画结束或帧数的断言会一直失败，换成一个空的 `QQuickWindow` 也一样，不是代码问题。先唤醒屏幕并保持亮着再跑，见 [Qt Quick 经验](lessons/qt-quick.md#测量帧率前先确认屏幕处于工作状态)；把这一步做进 `quiet-desktop.ps1` 是上面的第 5 项。
  - **桌面恢复**：`quiet-desktop.ps1` 偶尔报“部分窗口没有恢复”，按提示再跑一次 `-Elevate -Restore` 就好。
  - **偶发失败**：`quick-native-drag` 在一次整批运行里超时过一次，单独重复都通过，见 10.5 的偶发问题。
  - **对比包**：`out/package/muxora-widgets-reference/` 自带 `data/`，布局独立保存。日常 Release 部署不要覆盖或删除它。
  - **程序位置**：Debug 版程序在 `out/build/dev-debug/bin/muxora.exe`，不带 Qt 运行库，要经 `tools/build.ps1 -Run` 或先载入工具链再启动。
  - **换行符**：在 Windows 上用 Python `write_text` 改文件会写成 CRLF，git 提交时会改回 LF；用 `write_bytes` 或指定 `newline` 可以避免。
  - **诊断目录**：`out/diagnostics/quick-migration/` 和 `quick-tab-strip/` 是 10.5 要对照的基准证据，暂时保留。
  - **运行状态**：没有运行中的预览或测试。
