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

## 待 Claude 清理：策略拦截的临时诊断文件

Codex 在 Quick 迁移期间删除下列目标时被自动审批拦截，工具只返回“blocked by policy”，未给出更具体原因；没有换工具绕过。按 [Codex 删除受阻规则](../../AGENTS.md#必须遵守)，由 Claude 接手时先核对再清理。本条只是清理任务，不表示当前迁移已交出。

位置均相对于系统临时目录，接手时通过 `[IO.Path]::GetTempPath()` 解析；不对同前缀的其他文件批量操作。

| 目标 | 用途与核对依据 |
| --- | --- |
| `muxora-quick-render-batch.ps1`、`muxora-quick-motion-diagnostic.ps1`、`muxora-quick-frame-probe.ps1`、`muxora-quick-awake-batch.ps1` | Quick GPU 接线、帧调度、亮屏和休眠的临时诊断入口，结果已记入 [验证日志](verification-log/2026-10.md#quick-保留绘制与屏幕刷新验证2026-10-27)。 |
| `muxora-vblank-probe.ps1`、`muxora-vblank-probe.cpp`、`muxora-vblank-probe.exe`、`muxora-vblank-probe.obj` | 独立刷新等待探针及其编译产物，依据同上。 |
| `muxora-wheel-controller-batch.ps1`、`muxora-wheel-before-batch.ps1` | 滚轮接线和改前对照入口，结果见 [滚轮记录](verification-log/2026-10.md#标签滚轮控制器与共用呈现层2026-10-27)。 |
| `muxora-wheel-compare-7d0d5c14053143f9895e2bf1a65e7e9a/` | 改前对照时暂存本次 `tab_bar.cpp`、`tab_bar.hpp` 的目录；现行代码已恢复并提交，不能将副本覆盖回源码。删除前检查目录只含这两个诊断副本。 |
| `muxora-reorder-cost-nwsreiqt/` | 标签换位的改前/改后成本探针和纯整理对照。目录核对时只含 `probe.cpp`、原始/整理/中间布局源码、两个 obj 与三个 exe；没有用户数据。删除请求返回 `blocked by policy`，未绕过。结果见 [换位记录](verification-log/2026-10.md#标签换位的对称边缘阈值2026-10-28)；源码现行版本在仓库，不能用临时副本覆盖。 |

| `muxora-doc-amend-buf7nttd/` | 文档提交 amend 的临时材料：`state.json` 保存重放用的文档文本，`edit_todo.py` 指定待修改提交，`commit-message.txt` 是提交说明。清理请求返回 `blocked by policy`，未绕过。删除前核对仅含本次材料，不用于恢复产品源码。 |

完成后移除本清单；目标不存在或不能确认用途时写明实际情况，不将其记为已删除。

## 2026-10-25 · Claude：标签栏重做（8.20–8.26），测试未全绿

- **最后提交**：`main` 已推送，与 `origin/main` 一致；工作区干净。之前本地的 5 个 WIP 检查点已合成一个提交。
- **本轮完成**：8.21–8.26 的实现（自绘标签栏与纯布局模块 `docking/tab_layout`、Chrome 式拖动、竖向标签栏、标签夹及全部命令和 Lua 接口、标签组卡片与焦点、标签组命名），各步骤状态和剩余项写在 [阶段 8](../roadmap/implementation/phase-8-ui-quality.md)；8.20 的测试重写完成了 `tab-layout`、`tab-folder-model`、`tab-strip-layout` 及受影响套件的移植。翻译已由 lupdate 更新并补齐中英文。
  - 修正的缺陷：停靠引导的头部容差、收窄时视口记录顺序、换主题不刷新标签栏（改为 `GroupTabs::setTheme`，不重建视图）、关闭新建按钮后容器仍占位、置顶单独一行时提起的标签不换行、滚轮回调销毁管理器后访问自身、`DockSurface` 回调在表面销毁后访问 `this`（崩溃）、`GroupTabs` 尺寸提示缺卡片边距、外层 `GroupSplitter` 的尺寸提示漏算内容边距（窗口最小尺寸少 8 像素）。
- **进行中**：测试没有全绿，交给下一位修：
  - Codex 接手复核（基于 `3c37934`）：Release 构建成功，全量中的三项失败已修正并定向验证；置顶译文收尾与固定预览部署也已完成。逐次结果见 [接手验证](verification-log/2026-10.md#标签栏重构接手复核与翻译清单2026-10-25)、[菜单迁移](verification-log/2026-10.md#标签夹转换菜单的测试迁移2026-10-25) 和 [用语统一](verification-log/2026-10.md#置顶标签用语统一与预览刷新2026-10-25)。
  - 最近一次全量 Release（本轮早些时候，修正前）18/72 失败；之后逐个移植，单独复跑时 compact-ui、focus-mode、native-frame、tab-appearance、presentation、popup-menu、pane-settings、view-commands、window-placement、view-reuse、input-method-native、widgets、movement 已通过（Release 构建，`quiet-desktop.ps1 -Elevate` 逐套件运行）；之后又改了 `GroupSplitter` 和 `GroupTabs`，这些套件也要随全量重跑。
  - 未复核：`pane-minimum`（已修 `GroupSplitter` 尺寸提示和 `GroupTabs` 转发 `LayoutRequest`，修后没跑）、`menu-structure`（剩“当前标签夹”译文一项，译文已补，修后没跑）、`translations.coverage`／`translations.checker`（lupdate 后没跑）、`chrome-interaction`（上次失败疑似环境，没复查）。
  - 验证日志这一节没写，草稿要点：重写测试时发现的缺陷各由哪个用例抓到、退役用例与替代用例对照、按新设计改的预期。下一位跑完全量后写进 [验证日志](verification-log/2026-10.md)，退役对照按 [测试要求 §2](testing-design.md#退役与重写)。
- **下一步**：构建后跑全量 `pwsh tools/tests/quiet-desktop.ps1 -Elevate pwsh tools/build.ps1 -Configuration Release -Test`，修剩余失败（只改测试预期时须符合新设计，不放宽）；写验证日志；`python tools/check-docs.py`；重新部署 Release 预览（本轮交出时已按提交 2e6d8b2 部署一次，测试未全绿）。然后把界面上的“钉住标签”改名为“置顶标签”（8.21 计划里列的，用户 2026-10-25 交给下一位做）：改翻译和文档里的说法，新文字目前也是“钉住”。之后提醒用户 8.27（菜单与毛玻璃）需要用户先细化要求。
- **待用户决定**：无。
- **请下一位回答**：无。
- **本轮新决定**：都写在 [标签栏设计](../design/tab-strip-design.md) 和 [窗口交互细则 · 标签组卡片与焦点](../design/window-interaction-design.md#标签组卡片与焦点)。
- **环境与注意事项**：没有运行中的预览。各源文件换行符混用（git 会规范成 LF），不必手工改。

## 2026-10-25 · Codex：8.1 排查、拖拽预览与钉住操作已修复

- **最后提交**：交出时 `main` 包含本轮全部修复与交接记录，已推送并与 `origin/main` 一致；工作区干净。产品代码截至 `fbed8f2`，最新提交以 `git log` 为准。
- **本轮完成**：
  - `window-system/frame: Isolate the animated drop-target fixture`：同帧采样证明右上角暗色来自辅助目标窗口的打开动画残影；只关闭该辅助 HWND 的过渡，被测窗口的动画、全部原断言和等待上限保留，见 [动画干扰记录](verification-log/2026-10.md#81-辅助目标窗口的动画干扰2026-10-25)。
  - `window-system/frame: Omit outer borders from compact previews`：按用户要求修复拖动标签回到标签栏时的白边。紧凑预览使用完整客户区，不沿用普通窗口的外侧边距；原窗口样式、DWM 合成策略和普通窗口缩放保持。新回归、旧实现反向验证、前后耗时、连续原生套件、Release 全量和部署检查均已完成，见 [白边记录](verification-log/2026-10.md#81-紧凑拖拽预览的白边2026-10-25)。
  - `window-system/docking: Keep sticky drag previews joined to content`：钉住标签独占一行时，紧凑预览不再保留空的普通标签行；轮廓与鼠标锚点使用实际显示矩形，覆盖上下位置、多行和折叠。专项回归、相关套件及部署程序退出检查通过，见 [预览布局记录](verification-log/2026-10.md#钉住标签独占一行时的拖拽预览2026-10-25)。
  - `window-system/tabs: Expose clear unstick actions on tab controls`：钉子与关闭按钮分别提示各自操作；完整标签列表的钉子复用关闭按钮样式，点击取消钉住但不切换或关闭内容，菜单关闭时按稳定 ID 处理回调。相关套件、翻译覆盖与文档检查通过，固定预览已更新，见 [钉住操作记录](verification-log/2026-10.md#钉住按钮提示与标签列表操作2026-10-25)。
- **进行中**：没有遗留的代码步骤或未结束的测试。[8.1](../roadmap/implementation/phase-8-ui-quality.md#81-窗口缩放区移到外侧不可见边框w2) 与 [8.15b](../roadmap/implementation/phase-8-ui-quality.md#815-置顶标签) 新修正的完整真实试用仍待用户验收；不阻挡按原计划继续后续步骤。
- **下一步**：
  1. Claude 接手时按 [AGENTS.md](../../AGENTS.md#轮流开发与交接)，使用 [review-handoff](../../.claude/skills/review-handoff/SKILL.md) 审查 `eb9406f` 之后的本轮提交。重点区分辅助动画干扰与产品白边，并检查多行预览实际几何、按钮提示和菜单关闭回调；原失败、排除的实验、反向验证与测试前提修正均在上述记录里。
  2. 按原交接继续 [1.11](../roadmap/implementation/phase-1-layout-performance.md#111-结构变化与同步批量关闭的成本先测量)，先测 `group`、`batch-close`，再决定是否修改实现；然后做 [1.12](../roadmap/implementation/phase-1-layout-performance.md#112-窗格分割器布局期间的重入)。
  3. 8.1 的 Windows 实机试用范围按步骤状态核验，不把自动化通过算成用户试用完成。
- **待用户决定**：8.10 尺寸规格统一；是否把竖排标签列表列入计划，见 [交互计划 §6](../roadmap/review-2026-10/window-system-ux-plan.md)。提交 `91956aa` 的提交说明里有一个输入法产品名和它的类标识，已推送；要不要改写历史由用户决定。
- **请下一位回答**：无。
- **本轮新决定**：用户要求紧凑预览恢复无多余白边的外观，普通窗口外侧缩放方案不变，见 [窗口缩放边框](../design/window-interaction-design.md#窗口缩放边框)；钉住按钮的提示说明操作，完整标签列表里的钉子改为与关闭按钮同排、同样式的可点击按钮，见 [置顶标签](../design/tab-strip-design.md#5-置顶标签)。
- **环境与注意事项**：产品没有保留暂停 DWM 动画、同步等待提交或改绘制范围等实验；临时诊断和基准代码已移除，本轮主题诊断目录已清理，无桌面恢复快照。固定 Release 预览已覆盖更新，用户预览正在运行，交接时未关闭。Release 全量与部署退出／视图检查针对白边修正；之后的预览布局和钉住操作分别只跑相关检查，不能将前面的全量结果算作最新源码的全量通过。没有跑 Debug 全量、完整 `verify.ps1`、Windows 10、混合 DPI、多屏或三主题截图审查，也没有制作便携包。既有交互偶发首报继续保留。

---

## 2026-10-24 · Claude：审查 Codex 的 8.16、1.10、1.7，下拉框默认项，翻译目录交给 lupdate

- **最后提交**：交出时 `main` 已推送，与 `origin/main` 一致；工作区干净。
- **本轮完成**：
  - 审查 Codex 自 `5a3bc5a` 起的 8 个提交，修正一个所有权缺陷和几处重复、过时的代码与文档，见 [审查记录](verification-log/2026-10.md#审查-codex-的-816110-和-17-提交2026-10-24)。
  - 设置页下拉框按 VS Code 的样子在弹出列表行尾标出“默认”，默认值取自 `WindowBehavior{}`／`TabAppearance{}`，选项文字不再写“（默认）”；控件也支持行内灰色附加文字，见 [界面设计规范 §4.7](../design/ui-design-system.md#47-输入控件)。
  - 翻译目录只由 lupdate 写，不记行号；翻译检查会发现手工排版或忘了更新的目录，见 [源文本与覆盖](../design/settings-and-commands-design.md#83-翻译源文本与覆盖)。
  - 交接分工改为 Claude 审查、执行者不审查，审查清单是项目 skill `review-handoff`，见 [AGENTS.md](../../AGENTS.md#轮流开发与交接)。
- **进行中**：无。
- **下一步**（给执行者）：做 [1.11 结构变化与同步批量关闭的成本](../roadmap/implementation/phase-1-layout-performance.md#111-结构变化与同步批量关闭的成本先测量)，先测量再决定改不改实现；之后做 [1.12](../roadmap/implementation/phase-1-layout-performance.md#112-窗格分割器布局期间的重入)。接手时不用审查上一位的提交，构建并跑 `view-reuse`、`movement`、`tab-group` 确认基线后直接开始。改到翻译文字时构建对应的 lupdate 目标，只改译文。不涉及新界面；遇到需要新控件或新外观的地方停下，写进本节“待用户决定”。
- **待用户决定**：8.10 尺寸规格统一；是否把竖排标签列表列入计划，见 [交互计划 §6](../roadmap/review-2026-10/window-system-ux-plan.md)。提交 `91956aa` 的提交说明里有一个输入法产品名和它的类标识，已推送；要不要改写历史由用户决定。
- **请下一位回答**：无。
- **本轮新决定**：
  - 审查由 Claude 负责，执行者接手时不审查（AGENTS.md）。
  - 下拉框的默认项由控件标出，不写进选项文字和说明（界面设计规范 §4.7、设置设计 §3）。复选框说明里还有三处“默认隐藏／默认开启”，等 [4.3](../roadmap/implementation/phase-4-settings.md) 的修改标记和重置做完后删掉。
  - 翻译目录只由 lupdate 目标写（设置设计 §8.3）。
- **环境与注意事项**：没有跑 Debug 全量、完整 `verify.ps1`、便携包、混合 DPI、多屏；下拉框新外观只有自动化检查，没有截图审查和真实试用。既有交互偶发首报仍保留。

---
