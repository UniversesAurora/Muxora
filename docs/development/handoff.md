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

---
## 2026-10-21 · Claude 交给 Codex：审查 9.8，整理仓库，测试改为管理员权限

- **最后提交**：`main` 已全部推送，与 `origin/main` 一致；工作区干净。
- **本轮完成**：
  - 接手核对：Release 构建通过；在提交 `0966631` 上经 `quiet-desktop.ps1 -Elevate` 重跑 Release 全量，结果见 [验证日志](verification-log/2026-10.md#claude-接手审查2026-10-21)。
  - 审查上一轮的全部提交。9.8 需要返工两处（R1 常驻调用钩子、R2 可让出的 `require`），要求、测试和完成条件写在 [9.8 · 返工](../roadmap/implementation/phase-9-lua-extension-api.md#返工)。审查发现的协作问题写进 [助手协作经验](lessons/collaboration.md)，对应规则写进 AGENTS.md。
  - 整理仓库：2026-10 审查文档移到 `docs/roadmap/review-2026-10/` 作为快照，路线文档直接列在文档入口；变更记录分成“未发布”和 `0.1.0-preview.1`；工程设计 §8 的目录规划改为只列目录；预览说明“最近一次验证”只保留最新一次。
  - 按完整文件清单再整理一轮（[经验](lessons/collaboration.md#仓库整理)）：删除 MinGW 构建分支和 `muxora.rc`、无人使用的 `mise.toml`；`verify.ps1`／`bench.ps1` 改用与 `build.ps1` 相同的 Qt 查找，`verify.ps1` 增加 Lua 外部宿主和 Lua 部署检查；删除无调用者的 `ScriptHost::affectedCommands`；`FloatingPanel` 的实现移到模块 `src/` 根目录，`authored_mouse_input.hpp` 移出 `tests/fixtures/`；更正 Lua 预算、`debug` 库等过时说法和预览说明里的本机路径；清理 `out/` 下的排查残留。
  - 改造 `tools/tests/quiet-desktop.ps1`（用户要求由 Claude 直接改）：自己解析参数，选项按全名匹配且只在被测命令之前，被测命令里的 `-Test` 等原样传递；同名程序取第一个；整批以管理员权限运行（`-Elevate` 用一次 UAC 启动管理员会话并转回输出和退出码），删除只控制窗口的代理进程。自测相应补充，结果见验证日志。
  - “窗口交互修订”整理为正式的 [窗口交互细则](../design/window-interaction-design.md)：去掉修订标注和历史说明（移到验证日志），所有引用改为“交互细则”，规则本身没有改。
  - 文档按多助手项目的通行做法重新整理（用户要求）：新增 [文档维护规范](documentation.md)（每类信息的权威位置、索引规则、改动时同步更新的文档、提交前和接手时的检查）和 `tools/check-docs.ps1`；AGENTS.md 精简为共用规则、常用命令和链接，原来只写在其中的规则移到测试要求、工程设计和 CONTRIBUTING；删除重复的路线入口页、过时的版本号和日期、正文里的修订标注，模块说明里重复的数值和行为改为链接。
- **进行中**：无。
- **下一步**（按顺序）：
  1. **9.8 返工 R1、R2**，见上面的链接。R1 先在改动前测一次基准，改完再测，数字写进验证日志。
  2. 重跑 Release 全量（`pwsh tools/tests/quiet-desktop.ps1 -Elevate pwsh tools/build.ps1 -Configuration Release -Test`），刷新固定预览目录，更新 9.8 状态、实现计划当前位置、预览说明和本节。
  3. **排查 MSVC Debug 全量**（用户 2026-10-21 指定交给 Codex）。改用 MSVC 后 Debug 第一次完整跑全量（管理员权限，排除了前台窗口的干扰）：61 个套件 51 个通过，9 个超时，`peek-size` 失败。数据见 [验证日志](verification-log/2026-10.md#claude-接手审查2026-10-21)。
     - **超时**：同一批套件 Debug 比 Release 慢约 5～8 倍（中位数 5 倍），Release 下超过约 8 秒的桌面套件在 Debug 下就超过 CTest 的 60 秒上限。先逐个单独运行这 9 个套件（`ctest --test-dir out/build/dev-debug -R <名字> --timeout 600`，经桌面隔离），确认它们只是慢、最终能通过，并记录用时；如果有卡住的，按缺陷处理。
     - 确认只是慢之后，再提出按构建类型设置超时的方案（例如 Debug 用更长的上限），写明依据，交给 Claude 评审后再改；不能直接调大或删掉超时。
     - **`peek-size` 失败**：`shortcutOpensAKeyboardEditableSizeDialog` 在 Debug 下 `height->hasFocus()` 为 false，随后等待超时（`peek_size_test.cpp:518`、`:530`），Release 下通过。按真实缺陷排查（焦点时序或等待条件），修复后保留回归。
     - 结果写进验证日志和预览说明“已知问题”；完成后 `verify.ps1` 应能完整跑通。
  4. 之后按推荐顺序进行 1.7。
- **待用户决定**：8.10 尺寸规格统一。
- **本轮新决定**：
  - 桌面测试和全量测试一律带 `-Elevate` 运行，不先用普通权限试一次，并且整批以管理员权限运行：权限更高的前台窗口会让普通权限的测试无法移动鼠标（AGENTS.md、[测试要求](testing-design.md)、[桌面测试经验](lessons/desktop-testing.md)）。
  - 9.8 中被 `require` 的模块必须能在顶层调用 `muxora.*`，与 Neovim 一致（用户此前“按 Neovim 的方式处理”的要求，写在 9.8 返工 R2）。
  - 改动热路径要附前后耗时；达不到设计或 ADR 要求时交给用户决定，不写成“用法”；交出前按文档的写法跑一遍改过的入口脚本；验证结论写清每个数字对应的运行和提交（AGENTS.md、[助手协作经验](lessons/collaboration.md)）。
  - 项目只支持 MSVC 构建（ADR 0009 补记）；开发入口只有 `tools/` 下的脚本，不再有 `mise.toml`。
  - 排查用的临时文件放系统临时目录或 `out/diagnostics/<主题>/`，用完删除（AGENTS.md“必须遵守 · 临时文件”）。
  - 文档：每类信息只写在 [文档维护规范](documentation.md) 规定的位置，别处只链接；每次提交同步更新受影响的文档并运行 `pwsh tools/check-docs.ps1`；接手时按该规范审查上一位的提交（AGENTS.md、文档维护规范）。
  - AGENTS.md 只放所有助手必须遵守的规则、常用命令和链接，细则写在它链接的文档里。
  - 发布标签 `v0.1.0-preview.1` 移到 main 上内容相同的 `dfb5c14`（用户同意；原先指向 10-20 改写前的 `d3aeb53`，两者文件完全相同）。GitHub 预发布及其下载文件不变。
  - “默认用 pwsh”“提权优先用 gsudo”“git 只用 rebase 或快进”属于这台电脑的约定，已同步写进两位助手各自的全局配置，不在仓库里。
- **环境与注意事项**：
  - 已知偶发问题见 [预览说明](window-system-preview.md#待完成与验证范围)。

---
