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

## 2026-10-24 · Claude 交出：8.15b 钉住标签、审查 8.12 后的跟进、布局恢复，8.16 交给 Codex

- **最后提交**：`main` 已推送，与 `origin/main` 一致；工作区干净。8.15b 先在分支 `claude/8.15b-sticky-tabs` 上开发，Codex 推送 8.12 后变基到 `main` 并快进合入，分支和工作树已删除。
- **本轮完成**（用户要求 8.15b 四项都做，前三项照 VS Code 做成设置）：
  - `window-system/tabs: Close tabs with the middle button`：标签栏和收纳条的中键关闭（界面规范早有规定，一直没实现）。
  - `window-system/tabs: Show tooltips for the displayed tab in custom layouts`：修复多行标签栏第二行起悬停提示错误（反向验证过）。
  - `window-system/tabs: Size sticky tabs and keep them in view`：`tabs.stickySizing`（normal／compact／shrink），紧凑和收缩时单行滚动钉住标签留在开头。
  - `window-system/tabs: Show sticky tabs on a separate row`：`tabs.stickyTabsOnSeparateRow`，两行之间拖动按指针所在行决定钉住。
  - `window-system/tabs: Keep sticky tabs open on the close shortcut and middle click`：`tabs.preventStickyClose`，默认与 VS Code 相同（快捷键和中键都拦），因此 Ctrl+W 对钉住标签改为切到未钉住的标签。
  - `window-system/tabs: Mark sticky tabs in the tab list`：完整标签列表中钉住标签带钉子标记。
  - 用户试用预览后的修正：多行拖动最后一行唯一的标签时插入位置来回跳（原有问题，[记录](verification-log/2026-10.md#多行拖动最后一行唯一的标签时插入位置来回跳2026-10-24)）；关闭多行时保留滚轮展开（[记录](verification-log/2026-10.md#关闭多行时保留滚轮展开2026-10-24)）；钉住行占满时多出的滚动条（[记录](verification-log/2026-10.md#钉住行占满时单行标签栏多出滚动条2026-10-24)）；收缩宽度可调，当时新增 `tabs.shrinkStickyTabWidth`，后来改为限制标题长度（[记录](verification-log/2026-10.md#收缩的钉住标签宽度可调2026-10-24)）。
  - 每项的实现要点、新增用例和反向验证见 [验证记录](verification-log/2026-10.md#钉住标签的外观与保护815b2026-10-24)；新增桌面套件 `sticky-tabs`。
  - 变基到 8.12 之后，Release 全量 64/64 通过（经隔离桌面，325.86 秒），翻译检查和文档检查通过。
  - 审查 Codex 的六个 8.12 提交（用户要求，重点是翻译）：迁移没有功能错误。`treewide: Use sentence case and whole sentences in English UI text` 统一英文命令名大小写、改掉一处字符串拼接和一条过时的报错；`docs: Tidy the 8.12 records after review` 恢复 ADR 0010 原文、精简 Codex 的验证记录。发现、修正和留着没改的事项见 [审查记录](verification-log/2026-10.md#审查-codex-的-812-提交2026-10-24)，修正后受影响的 20 个套件通过。
  - 用户看了审查记录后决定内部标识改英文，审查里只记录的问题也一并改掉，[记录](verification-log/2026-10.md#英文内部标识整句方向命令和-luaruntime-目录2026-10-24)：
    - `window-system: Use English IDs for menus, categories and palette names`：66 个中文分类和菜单路径标识、内置配色名换成英文，豁免清单从 161 项减到 47 项。
    - `lua-runtime: Translate diagnostics from its own catalog`：LuaRuntime 的 42 条诊断移到自己的 `LuaRuntime` 上下文和目录，应用启动时加载。
    - `window-system/commands: Give each direction command a whole title`：72 条方向和上一个／下一个命令各用整句源文本，中文与原拼接结果相同。
    - `test: Run the translation coverage check as its own CTest`：覆盖检查改为 CTest 项 `translations.coverage`，C++ 测试不再调用 PowerShell。
    - 在全部改动上 Release 全量经隔离桌面 65/65 通过（320.20 秒）。
  - 随后按用户决定，[记录](verification-log/2026-10.md#跨平台检查改用-python收缩的钉住标签改为限制标题长度2026-10-24)：
    - `tools: Port the translation check to Python` 和 `tools: Port the docs check to Python`：和平台无关的三个检查脚本改写成 Python，反向用例也进了 CTest（`translations.checker`）。
    - `window-system/tabs: Limit only the title of shrunk sticky tabs`：`tabs.shrinkStickyTabWidth` 改为 `tabs.shrinkStickyTitleWidth`，只限制标题文字，默认 40 px，和普通标题裁切分开算。
    - 在全部改动上 Release 全量经隔离桌面 66/66 通过（312.96 秒）。
  - 用户问手动改坏设置会怎样，[记录](verification-log/2026-10.md#布局文件部分无效时的恢复设置与状态的文件归属2026-10-24)：
    - `app: Recover what it can from an invalid layout file`：布局里一类设置无效时只重置那一类，窗口和标签照常恢复；失败时都先留副本并在状态栏说明，修正了一处值无效就丢掉整个布局的缺陷。
    - `docs: Decide where each kind of setting and state is stored`：设置设计 §2 加上数据归属表，阶段 4 计划补上控件指定方式和遗漏的设置 ID。
    - 在全部改动上 Release 全量经隔离桌面 67/67 通过（323.55 秒）。
  - 只改文档的两个提交：`docs: Plan per-scope input method state`（用户提出的新需求，设计写进交互细则，实现步骤为 8.16）；`docs: Give a rule for where new saved data goes`（新增要保存的数据按设置设计 §2 的七条规则选文件，AGENTS.md 指向它）。文档检查通过，没有代码改动，没有另跑测试。
  - `docs: Design SSH private key storage and the Muxora agent`：私钥放 `~/.ssh`（默认）或 Muxora 保管库，外部工具通过独立的无界面代理使用保管库密钥；新设计文档和实现步骤 7.13、7.14。只改文档。
- **进行中**：无。
- **下一步**：
  1. [8.16 按层级保存输入法状态](../roadmap/implementation/phase-8-ui-quality.md#816-按层级保存输入法状态)：用户指定交给 Codex。平台接口和 Windows 实现、范围跟踪、`WindowBehavior::inputMethodScope` 及其保存、Lua 设置接口、设置页复用现有下拉框及测试均由 Codex 完成。微软拼音、日文、韩文和第三方输入法的真实效果由用户试用。
  2. 然后按原顺序 [1.10](../roadmap/implementation/phase-1-layout-performance.md#110-命令注册表-id-索引r12) → 1.7 → 1.11 → 1.12。
- **待用户决定**：8.10 尺寸规格统一；是否把竖排标签列表（上百个会话时用，[交互计划 §6](../roadmap/review-2026-10/window-system-ux-plan.md) 有提到，实现计划里没有）列入计划。
- **请下一位回答**：无。
- **本轮新决定**（均为用户 2026-10-24）：8.15b 的三个设置照 VS Code 的名称、选项和默认值；收缩的钉住标签宽度可调；关闭多行时保留滚轮展开的值；多行收起时不常显新建按钮。写在 [标签外观设置](../design/settings-and-commands-design.md#31-标签外观与颜色) 和 [交互细则 · 钉住标签](../design/window-interaction-design.md#钉住标签)。内部标识（命令分类、菜单路径、配色等）一律用英文 ID，以后新增的也一样，写在 [设置与命令设计 §8.3](../design/settings-and-commands-design.md#83-翻译源文本与覆盖) 和 [工程设计 §2.1](engineering-design.md#21-可读性与依赖)。脚本按用途选语言：构建和测试只经 CMake／CTest，和平台无关的检查用 Python，只在 Windows 上做的事用 PowerShell，写在 [工程设计 §8](engineering-design.md#8-推荐目录组织)。收缩的钉住标签只限制标题长度，和普通标题裁切分开算，写在 [标签外观设置](../design/settings-and-commands-design.md#31-标签外观与颜色)。每类数据放哪个文件、控件默认按类型生成且可单独指定、布局文件部分无效时只重置无效的那几类，写在 [设置设计 §2–4](../design/settings-and-commands-design.md#2-文件划分) 和 [阶段 4](../roadmap/implementation/phase-4-settings.md)。输入法状态可以按层级分开记：输入法和它的全部模式一起记，不只针对中英文；默认按标签；轻量浮窗是窗口下面的一层；新建时沿用来源状态，不跨重启保存，先做 Windows，写在 [交互细则 · 输入法状态](../design/window-interaction-design.md#输入法状态)，实现步骤是 [8.16](../roadmap/implementation/phase-8-ui-quality.md#816-按层级保存输入法状态)。以后新增要保存的数据按 [设置设计 §2](../design/settings-and-commands-design.md#2-文件划分) 的规则选文件，并加进归属表。SSH 私钥由 `ssh.keys.storage` 选放 `~/.ssh`（默认）或保管库，保管库只用系统账户保护、不设主口令；切换时弹窗问是否迁移、是否保留原位置；外部工具照 1Password 的做法通过独立的 `muxora-agent` 使用保管库密钥，主程序没开也能用，写在 [SSH 私钥存放设计](../design/ssh-key-storage-design.md)。
- **环境与注意事项**：
  - 检查脚本改成了 Python，旧的 `.ps1` 已删除：配置测试需要 Python 3.10 或更新版本，提交前用 `python tools/check-docs.py`，翻译检查是 `python tools/check-translations.py`（全量测试里的 `translations.coverage`、`translations.checker` 就是它和它的反向用例）。
  - 设置 `tabs.shrinkStickyTabWidth` 已改名为 `tabs.shrinkStickyTitleWidth`，含义改为标题文字的最大长度。
  - 翻译目录的格式和 `lupdate` 输出不同（缩进、闭合标签），直接运行 `window_system_lupdate` 会重排整个文件。本轮只按目录原格式插入新条目；以后可以考虑统一一次格式。
  - 只在默认主题下截图核对过外观；三主题截图、Debug 构建和用户真实试用未做。

---

## 2026-10-24 · Codex：英文源文本迁移收尾

- **最后提交**：`main` 已包含本轮全部提交，交出时已推送并与 `origin/main` 一致；工作区干净。
- **本轮完成**：
  - 重新阅读规则、交接与相关设计，审查最近提交并完成接手构建与相关套件，见[接手核对](verification-log/2026-10.md#英文源文本迁移接手核对2026-10-24)。
  - `treewide: Use English source text and check translation coverage`：迁移英文源键、双语目录、动态显示文字、错误和脚本模板；保留内部标识与用户内容。源码检查、反向夹具和运行时覆盖见[迁移记录](verification-log/2026-10.md#英文源文本与覆盖检查2026-10-24)。
  - `lua: Cancel the count-hook fixture at its ready event`：通过迁移前后对照定位到用例的随机取消点，只修正测试前提；产品代码未改，原断言和反向验证保留，见[计数钩子记录](verification-log/2026-10.md#计数钩子恢复用例的取消时机2026-10-24)。
  - `treewide: Finish local English translation verification`：补齐 Windows 系统菜单备用文字、安装后宿主双语检查及部署帮助检查。本机 Release 全量 63/63、独立模块 56/56；最后改动后的定向套件、两个外部宿主和部署检查通过，见[收尾记录](verification-log/2026-10.md#英文源文本迁移收尾2026-10-24)。
- **进行中**：无。8.12 已完成，CI 接入按用户决定留到 6.6。
- **下一步**：按原顺序做 [1.10](../roadmap/implementation/phase-1-layout-performance.md#110-命令注册表-id-索引r12) → 1.7 → 1.11 → 1.12，再按测量决定 1.9／6.1。本轮没有开始这些步骤。新增外观仍由 Claude 做。
- **待用户决定**：
  - 8.10 尺寸规格统一；8.15b 第 3 项做成设置还是固定规则，见[计划](../roadmap/implementation/phase-8-ui-quality.md#815-钉住标签)。
- **请下一位回答**：无。
- **本轮新决定**：用户确认翻译检查的 CI 接入留到 [6.6](../roadmap/implementation/phase-6-engineering.md#66-持续集成) 统一实施；8.12 按现有本机实现和验证完成。
- **环境与注意事项**：
  - 首次 `toolbar-popup` 外点关闭和首次 Release 全量 `chrome-interaction` 二次点击关闭失败，后续通过仍不能证明原因已消除；没有改动断言或阈值。历史设置页滚动与拖拽首报也没有在本轮定位。
  - 桌面验证经隔离入口，先等收起结束再测，等测试进程全部退出再恢复。没有使用 computer use 测试。
  - 未做 Debug 全量、便携包、混合 DPI、多屏或用户真实试用。

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

