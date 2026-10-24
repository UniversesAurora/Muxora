# 交接记录

Muxora 由几个助手（目前是 Claude 和 Codex）按额度轮流开发，彼此看不到对方的对话和私有记忆。这份文件是接手时的第一入口：**接手先读最上面一节，工作过程中随时更新它**。规则见 [AGENTS.md · 轮流开发与交接](../../AGENTS.md#轮流开发与交接)。

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

## 2026-10-24 · Claude 交给 Codex：钉住标签、标签条滚动、测试前台修正；Codex 接 8.12 与 M1 内部步骤

- **最后提交**：`main` 已推送，与 `origin/main` 一致；工作区干净。
- **本轮完成**（用户 2026-10-24 的要求，详见各验证记录）：
  - `window-system/tabs: Keep the viewed tab in place when labels resize`：调整标题长度等外观时，活动标签在标签条上的位置不变、碰边才推动；收纳条按标签 ID 放回。拆出、排序等结构变化仍保留原偏移（[记录](verification-log/2026-10.md#标签长度变化时的条带滚动2026-10-24)）。用户报告的“Codex 版本里调长度时设置页滚到中间”在 `d31eaa8` 和当前版本都没有自动复现，原因未找到，保留了设置页滚动位置的检查。
  - `window-system/tabs: Relabel built strips when only appearance changes`：外观变化原地更新标签组和收纳条，不走整体刷新；240 个标签时每次约 21 ms → 5.7 ms（[记录](verification-log/2026-10.md#标签外观的原地更新2026-10-24)）。
  - `docs/dev: Keep test evidence in the log and diagnostics per topic`：证据写进验证日志正文；`out/test-results/` 只放统一入口的输出，`out/diagnostics/<主题>/` 在主题结束时删除（[规则](testing-design.md#测试结果与排查文件)）。已清理 827 个历史文件。
  - `window-system/persistence: Define the saved window fields once`：窗口记录字段清单只定义一处。
  - `test: Give caption-strip the MSVC Debug suite guard`：Debug 实测 57.8 秒贴着 60 秒上限，按已评审的做法给 300 秒（[记录](verification-log/2026-10.md#caption-strip-的-debug-总时限2026-10-24)）。
  - `test: Expect a cleared tab bar to be deleted from the event loop`：修正我上一轮 `ac6d0c8` 漏跑 widgets 套件留下的失败用例。
  - `test: Move the real cursor away before the compact scrollbar checks`：`compact-ui` 滚动条用例的偶发失败查明是真实光标残留，修正后连续 20 次通过。
  - `window-system/tabs: Stick tabs to the front of their group`：钉住标签 8.15a（[交互细则](../design/window-interaction-design.md#钉住标签)、[计划](../roadmap/implementation/phase-8-ui-quality.md#815-钉住标签)）。
  - `window-system/chrome: Fail toolbar reset when its window closes`：第一次完整验证中查出的竞态，关闭窗口时工具条大小重置命令误报成功；新增确定构造该顺序的用例（[记录](verification-log/2026-10.md#工具条大小重置命令在关闭窗口时的结果2026-10-24)）。
  - `test: Name the window that refuses native input` 和 `test: Keep the foreground handoff inside the quiet desktop runner`：全量中偶发的 `toolbar-popup` 失败查明是测试窗口刚激活就被 Windows 外壳窗口夺走前台；现在 `quiet-desktop.ps1` 用一个透明小窗口接住交出的前台（[记录](verification-log/2026-10.md#全量中测试窗口的前台被外壳窗口夺走2026-10-24)）。
  - `tools: Clean up the deployment check output`：部署检查的日志移到 `test-results/`，临时状态在检查通过后删除。
  - 完整 `verify.ps1` 在 `f5fffa6` 和 `9a4b98d` 上各通过一次：Debug 63/63、Release 63/63、独立模块 56/56、两个外部宿主、应用退出、视图状态和 Lua 部署检查；固定 Release 预览已刷新。
- **进行中**：无。
- **下一步**（Codex，按顺序；分工规则见 [AGENTS.md](../../AGENTS.md#轮流开发与交接)，这些步骤都不需要新外观）：
  1. [8.12 英文源文本与全量翻译核查](../roadmap/implementation/phase-8-ui-quality.md#812-英文源文本与全量翻译核查)：独占进行，期间不做其他会新增文字的步骤。中文界面显示的文字保持现状；英文源文本由代码含义决定，拿不准的条目列进验证日志交 Claude 复核；不改菜单归属和持久化标识。
  2. [1.10 命令注册表 ID 索引](../roadmap/implementation/phase-1-layout-performance.md#110-命令注册表-id-索引r12)。
  3. [1.7 非活动标签按需创建视图](../roadmap/implementation/phase-1-layout-performance.md#17-非活动标签按需创建视图) → [1.11 结构变化与同步批量关闭的成本](../roadmap/implementation/phase-1-layout-performance.md#111-结构变化与同步批量关闭的成本先测量)（先测量，结果交 Claude 决定是否改实现）→ [1.12 窗格分割器布局期间的重入](../roadmap/implementation/phase-1-layout-performance.md#112-窗格分割器布局期间的重入)。
  4. [1.9 布局保存移出 GUI 线程](../roadmap/implementation/phase-1-layout-performance.md#19-布局保存移出-gui-线程应用侧)（先看基准数据决定做多少）、[6.1 应用入口拆分](../roadmap/implementation/phase-6-engineering.md#61-应用入口拆分)。
  - 留给 Claude：8.1 → 8.2（原生框架和面板调整，要靠真实试用把握交互手感）；M2 的主题、颜色和界面组件（2.1–2.7、8.3–8.6、8.8、8.11）；3.8 快捷键编辑器、4.3 设置页；M6 的 8.10、8.15b；以及任何新增设置项。用户试用钉住标签和标签条滚动后的反馈也由 Claude 处理。
  - Codex 以后还可以接的、不涉及外观的步骤：3.1–3.5 上下文键与按键优先级、3.6–3.7 JSONC、4.1、4.2、4.4、4.5 设置注册表与持久化、6.2–6.6 工程、8.7 系统通知。按推荐顺序，到时由交接记录指定。
- **待用户决定**：8.10 尺寸规格统一；8.15b 钉住标签的尺寸模式、单独一行和按键保护要不要做、先做哪项。
- **请下一位回答**：无。
- **本轮新决定**：新外观的工作由 Claude 做，Codex 做不涉及外观的步骤（用户 2026-10-24，写在 [AGENTS.md](../../AGENTS.md#轮流开发与交接)）；标签长度变化时的条带滚动规则（[交互细则](../design/window-interaction-design.md#条带尺寸变化)，用户确认“活动标签不动、碰边再推”）；钉住标签的行为与拖动规则（用户 2026-10-24）；测试证据写进验证日志、排查文件按主题删除（[测试要求](testing-design.md#测试结果与排查文件)）。
- **环境与注意事项**：
  - 修改公开接口的生命周期或行为时，搜索所有调用它的测试，不能只跑“相关”套件；本轮因此漏了一个用例并推送了失败。
  - 未做三主题截图审查、混合 DPI、多屏和真实试用；`chrome-interaction` 菜单首报仍未查明。
  - 用户报告 Codex 版本中调标题长度时设置页会自动滚动，在 `d31eaa8` 和当前版本都没有复现，原因未找到；`PaneSettingsTest::titleWidthUpdatesKeepScrollPositions` 守着当前行为。
  - `tab-strip-layout` 的 `detachedMultilineTabDropsIntoAnotherGroupAtItsVisibleRow` 在 10-22 和本轮前两次完整 Debug 运行中整组失败、单独运行通过，修正前台交接后的完整运行通过，但原因没有单独确认；再失败时先看拖放目标点上的窗口（[记录](verification-log/2026-10.md#全量中测试窗口的前台被外壳窗口夺走2026-10-24)）。

---

## 2026-10-23 · Claude 交出：接手审查、构建依赖修复、标签排布与 Q3

- **最后提交**：`main` 已推送，与 `origin/main` 一致；工作区干净。
- **本轮完成**：
  - 审查 Codex 自上次交接以来的全部 36 个提交（Lua 停止钩子与模块加载、Debug 时限与 `peek-size`、标题裁切与字形缓存、6.9 当前格式清理、滑条组件与焦点修复、计划文档）。实现总体符合规则和设计；发现的问题已在下列提交修复，未改动的观察见“环境与注意事项”。
  - `build: Keep MSVC header dependencies across console code pages`：查明两次“增量构建不重编依赖头文件的对象”的原因是 `cl` 中文输出随控制台代码页变化，构建脚本现固定代码页并清空前缀不一致的构建目录（[记录](verification-log/2026-10.md#构建目录的头文件依赖与控制台代码页2026-10-23)、[经验](lessons/build-toolchain.md#本地化的-cl-让头文件依赖随控制台代码页丢失)）。
  - `lua: Count user hook intervals before restoring runtime checks`：被取消任务留下的协程恢复时用户 count 钩子会提前触发，已修复并有反向验证（[记录](verification-log/2026-10.md#被取消任务留下的协程的用户计数钩子2026-10-23)）。
  - `window-system/tabs: Share label geometry and show close feedback`（用户要求）：标题长度调小或为 0 时收纳条两端不再变宽，图标与关闭按钮之间只留一个间隙，收纳条两端留白 12 → 8，关闭按钮有悬停和按下反馈；三处重复的尺寸公式合并为一处（[记录](verification-log/2026-10.md#标签内部排布与关闭按钮反馈2026-10-23)）。
  - `window-system/views: Defer deleting replaced tab bars`：标签栏替换改为延迟删除，宿主在旧栏自己的按钮里替换栏不再删掉信号发送者（[记录](verification-log/2026-10.md#标签栏替换的删除时机2026-10-23)）。
  - `docs/roadmap: Plan the uncovered items of the frozen review`：答复 Q3，新增 1.11、1.12、8.14，1.7 增加内容控件挂接契约（[A3](verification-log/2026-10.md#冻结审查尾项的计划覆盖a32026-10-23)）。
  - Release 全量在 `15635b9` 加标签改动上 63/63 通过；固定预览已刷新，应用退出检查通过。删除了 Codex 删不掉的 3 个仓库外临时目录和两个遗留工作树（`claude/*` 分支保留）。
- **进行中**：无。
- **下一步**：[8.12 英文源文本与全量翻译核查](../roadmap/implementation/phase-8-ui-quality.md#812-英文源文本与全量翻译核查)（约 550 条翻译、510 处翻译调用，按步骤先盘点再迁移），之后 1.7 → 1.11 → 1.12。
- **待用户决定**：8.10 尺寸规格统一（标签内部排布已按用户要求单独定下，不再列入）；交互计划中的浏览器式“钉住标签”是否加入计划（见 A3）；新的标签排布和关闭按钮反馈待用户试用。
- **请下一位回答**：无。
- **本轮新决定**：标签内部排布与关闭按钮反馈（用户 2026-10-23），规格在 [界面设计规范 §2.2](../design/ui-design-system.md#22-尺寸)；构建期间固定控制台代码页，见 [工程设计 §6.1](engineering-design.md#61-qt-sdk-的安装版本绑定与升级)。
- **环境与注意事项**：
  - 旧的构建目录如果是在另一种代码页下配置的，第一次运行 `build.ps1`／`verify.ps1`／`bench.ps1` 会整目录清空重建一次。`dev-debug` 已按新规则重建；`standalone` 已删除，由下次 `verify.ps1` 重建。
  - 审查中记录、未改动：设置页滑条每次数值变化都整体刷新外观，实测每次约 8.5 ms，可接受；`restoreLayout()` 只对窗口记录用字段白名单拒绝未知字段，`saveViewLayout()` 新增字段时必须同步白名单；Lua 中 JSON `null` 的哨兵只能从宿主数据取得，脚本不能直接构造；`out/diagnostics/` 根目录堆积了大量历史排查文件，部分被验证日志引用，本轮未清理。
  - 仍未完成：完整 `verify.ps1`；Release `chrome-interaction` 菜单首报原因未查明；`verify.ps1` smoke 状态清理缺口；未做三主题截图审查和真实试用。

---

## 2026-10-23 · Codex 交出：标题长度调节与输入焦点修复

- **最后提交**：本轮分项提交已纳入 `main`，交出时与 `origin/main` 一致，工作区干净。
- **本轮完成**：`window-system/settings: Add reusable integer slider controls` 完成两套长度的同排滑条和 Qt 数字框，单位后缀及解析独立于组合组件，连续拖动保留页面。相关套件、Lua 定向用例及部署版设置启动检查通过，原失败和前后测量见 [记录](verification-log/2026-10.md#标题长度滑条与可复用数值组件2026-10-23)。`window-system/settings: Keep focus while editing title widths` 已单独修复输入跳到搜索框，证据见 [焦点记录](verification-log/2026-10.md#标题长度连续输入的焦点修复2026-10-23)。此前 [6.9](../roadmap/implementation/phase-6-engineering.md#69-清除旧配置兼容层) 已完成，交付见 [记录](verification-log/2026-10.md#当前配置清理交付与接手状态2026-10-23)。此前实现分别提交：
  - `repo: Keep only the current configuration format`：确定工程约束并清理现行计划中的双写、旧字段导入、别名和转发周期。
  - `window-system/persistence: Preserve explicit saved titles and colors`：删除标题和内置配色升级，显式值恢复回归通过。
  - `test: Drop recovery of obsolete desktop snapshots`：删除旧句柄快照恢复，自身无窗口／原生模式均通过。
  - `window-system/settings: Keep the view command fixture current`：补齐既有标题设置的预期集合，没有改产品行为。
  - `lua: Preserve explicit null values across host calls`：修复严格结构回归发现的 Lua 空值丢失，运行时及应用相关检查通过，记录同机前后样本。
  - `window-system/model: Remove obsolete snapshot reconstruction`：删除固定树／顺序补建和旧跨方向尺寸 API，相关模型及界面回归、安装宿主通过。
  - `window-system/persistence: Remove obsolete chrome layout conversion`：删除工具条混合转换、默认补建、临时全屏字段读取和假空行，相关回归通过。
- **进行中**：无未提交或中断的实现。此前 8.13 与 9.8 的真实试用范围、既有菜单／悬停及多行拖拽首报继续保留；本轮未宣称完整入口全绿，未执行无关全量或 Debug。
- **下一步**：Claude 接手后先答复下方保留的 Q3，按结论补齐现行计划引用或缺项；随后按 [推荐顺序](../roadmap/implementation/README.md#推荐执行顺序) 做 [8.12 英文源文本与全量翻译核查](../roadmap/implementation/phase-8-ui-quality.md#812-英文源文本与全量翻译核查)，完成后再进入 1.7。开始前完整阅读步骤及引用；不因文档或构建类型变化重复已完成测试。
- **待用户决定**：8.10 尺寸规格统一。
- **请下一位回答**：Q3（冻结审查尾项是否被计划覆盖）已由 Claude 答复，结论见 [验证日志 · A3](verification-log/2026-10.md#冻结审查尾项的计划覆盖a32026-10-23)。
- **本轮新决定**：用户确定滑条与 Qt 数字框同排，保留上下微调；单位显示 `px`，后缀被改动后按数字前缀读取并恢复显示，不弹窗；支持零值和大数值。组合组件不处理单位或标签业务，说明只描述设置效果。权威要求在 [标签外观设置](../design/settings-and-commands-design.md#31-标签外观与颜色) 和 [输入组件](../design/ui-design-system.md#47-输入控件)。
- **环境与注意事项**：固定预览已覆盖，测试和部署进程已退出，无待恢复快照；本轮临时状态与辅助脚本已删除。此前基准的临时构建产物删除被工具自动审批拒绝，残留在仓库外，见当前配置清理交付记录。首报均保留；本轮确认部分 Ninja 对象的头文件依赖为空，旧目标混用不同范围常量，清理重建后相关检查通过，但依赖采集失效的原因仍待排查，见 [经验](lessons/build-toolchain.md#本地化的-cl-让头文件依赖随控制台代码页丢失)。`verify.ps1` smoke 状态清理的既有收尾缺口未混入本次。历史交互问题的逐项重复见 [记录](verification-log/2026-10.md#先前失败用例逐项重复2026-10-22)，不能据此认定已修复。
