# 实现计划：总览与执行规则

日期：2026-10-18（2026-10-19 修订：主题阶段改为颜色注册表方案，新增阶段 8 界面质量，增加界面标准）\
上游：[审查结论与后续路线](../README.md)。路线文档回答“做什么、为什么”；本目录把每一项拆成可以独立交给一个 Agent 的步骤，写清楚改哪些文件、怎么改、加什么测试、怎么验证、做到什么程度算完成。

## 阶段一览

| 阶段 | 文档 | 内容 | 前置 |
| --- | --- | --- | --- |
| 0 | [基线与度量](phase-0-baseline.md) | 规模基准程序、测试分层标签、测试结果汇总脚本、ADR、预览文档拆分 | 无 |
| 1 | [布局性能](phase-1-layout-performance.md) | 模型索引、去掉序列化比较、分隔条快速路径、只重建变化的视图、视图按需创建、延迟恢复 | 0.1、0.2 |
| 2 | [主题系统](phase-2-themes.md) | 按 VS Code 的颜色注册表与主题机制重做：颜色 ID、主题定义与继承、Muxora Dark／Black、对比度测试与组件画廊、`workbench.colorTheme` 等设置与切换命令、颜色覆盖、高对比 | 0.2 |
| 3 | [快捷键](phase-3-keybindings.md) | 上下文键、`when` 表达式、分发与菜单接入、终端冲突默认值、JSONC、快捷键编辑器 | 0.2 |
| 4 | [设置](phase-4-settings.md) | 设置 schema、生成式设置页、行为设置迁出布局、JSONC 写回、文件监听 | 3.6（共用 JSONC） |
| 5 | [Lua 工具](phase-5-lua-tools.md) | 控制台、编辑器、补全、API 单一来源、多脚本、脚本管理 | 5.1 无前置；5.2 起需要 3.5（编辑器的 `Ctrl+S` 等按键优先） |
| 6 | [工程](phase-6-engineering.md) | 应用入口拆分、测试运行器合并、格式与静态检查、诊断构建、CI | 0.2 |
| 7 | [终端与会话接入](phase-7-terminal-and-sessions.md) | 终端视图模块；本地终端（Windows PowerShell、PowerShell 7、cmd、WSL、Git Bash，ConPTY）；会话目录与 SSH config；SSH、SFTP、X11；启动时恢复会话 | 1.4–1.6、3.1–3.5 |
| 8 | [界面质量](phase-8-ui-quality.md) | 窗口外侧缩放边框、临时面板八向调整、浮层基类、快速输入（替换命令面板等全部选择器）、行内重命名、统一对话框、Windows 系统通知、空工作区、菜单与文案整理、尺寸统一 | 8.1／8.2 无前置；8.3 起建议在 2.4 之后 |
| 9 | [Lua 扩展 API](phase-9-lua-extension-api.md) | 运行时多任务挂起与交互调用；Lua 对话框、输入、选择、系统通知；自定义界面（声明式控件树、面板标签、对话框）；存储、剪贴板、文件、网络、外部程序；终端读写与订阅；会话与 SFTP 接口 | 9.1 无前置；9.2 需 8.4／8.6／8.7；9.6、9.7 需阶段 7 |

### 依赖与并行

```
0.1 ─┬─> 1.x（性能改动前后都要跑基准）
0.2 ─┼─> 2.x ──────────────┐
     ├─> 3.1 → 3.2 → 3.3 → 3.4 → 3.5
     │              └─> 3.6 → 4.x
     ├─> 6.x
     └─> 5.1 ──────────┐
3.5 ───────────────────┴─> 5.2 → 5.3 → 5.4 → 5.5 → 5.6
1.4 → 1.5 → 1.6 → 1.7 ─┐
3.1–3.5 ───────────────┴─> 7.x
8.1 → 8.2
9.1 ─> 9.2、9.3 → 9.4、9.5；阶段 7 ─> 9.6、9.7
2.4 ─> 8.3 → 8.4 → 8.5；8.3 → 8.6；8.7 无前置；8.4 ─> 2.6 的主题选择列表
```

可以同时进行、互不改同一文件的组合（每组各开一个会话，各用自己的 worktree）：

| 组合 | 说明 |
| --- | --- |
| 1.1 + 2.1 + 6.1 | 模型、样式、应用入口，文件不重叠 |
| 3.1 + 5.1 | 窗口模块的上下文键、应用侧 Lua 控制台（5.1 会改 `script_host.cpp`，3.x 不改） |
| 2.2–2.5 + 3.2 | 主题目录与表达式解析器不重叠；两者都改 `window_manager.hpp` 时要先合并一方再开始另一方 |
| 8.1 + 2.1 + 1.1 | 原生帧、颜色注册表、模型索引，文件不重叠 |

**容易冲突的文件**（同一时间只允许一个步骤修改）：`window_manager.hpp`、`window_manager.cpp`、`window_manager_p.hpp`、`window_commands.cpp`、`command_menus.cpp`、`settings_page.cpp`、`modules/window-system/CMakeLists.txt`、`modules/window-system/tests/CMakeLists.txt`、两个 `.ts` 翻译文件、`docs/development/window-system-preview.md`。

## 每个步骤的执行规则

执行任何步骤的 Agent 必须遵守下面的流程。仓库规则以 [AGENTS.md](../../../AGENTS.md) 为准，这里是落到每一步的具体做法。

### 开始前

1. 读 `AGENTS.md`、本文件、所在阶段文档的“阶段约束”和目标步骤全文。
2. 读步骤“涉及文件”里列出的全部源文件，确认文档中的行号、函数名仍然存在。若代码已经变化导致步骤描述不成立，先在阶段文档里修订该步骤，再实现；不要按过时描述硬改。
3. 确认前置步骤已经合入当前分支（看 `git log` 和阶段文档中的“状态”）。
4. 从 `main` 开新分支或 worktree，分支名 `<阶段>.<步骤>-<短名>`，例如 `1.4-tab-view-reuse`。

### 实现中

- 一个步骤一个提交（必要时可以拆成“重构无行为变化”和“行为变化”两个提交）。不要顺手修改步骤范围外的代码；发现的问题写进阶段文档的“发现”小节或审查记录。
- 公开 API 的改动必须同步：头文件注释、`modules/window-system/README.md` 或相应模块 README、外部宿主夹具（`modules/window-system/tests/fixtures/external-host/`）中至少一处真实调用。
- 新增用户可见文字使用 `QCoreApplication::translate("WindowSystem", ...)`（模块）或 `"Muxora"`（应用），并运行 lupdate 目标（`window_system_lupdate`、`muxora_lupdate`）后补全 `zh_CN` 与 `en` 两个 `.ts` 文件。
- 新增公开操作要同时提供：命令注册表中的命令（菜单、命令面板、快捷键共用）、必要的键盘方式、Lua API（如果该领域已有 Lua 分组）、自动化测试。
- 不改 Release 默认编译选项和固定部署目录 `out/package/muxora-preview/`。新增的可选构建开关默认关闭。

### 界面标准

凡是涉及界面的步骤（新增或修改控件、弹窗、页面、菜单、颜色、尺寸、交互），都按下面的标准执行，不能做成“能用就行”的原型：

- 外观与交互以 Visual Studio、VS Code 为参照，遵守 [界面设计规范](../../design/ui-design-system.md)；颜色只使用 [主题系统](../../design/theme-system-design.md) 的颜色 ID（阶段 2.3 之前使用 `Theme` 令牌），不写具体颜色值。
- 同一类界面只用一种组件：选择与单值输入用快速输入（8.4），确认用统一对话框（8.6），通知用 Windows 系统通知（8.7，不做通知中心和程序内提示）。不新建 `QDialog`、`QInputDialog`、`QMessageBox`，不调用 `exec()`。
- 每个界面步骤完成时：
  1. 在浅色、深色、黑色三个主题下截图（阶段 2.5 之后用组件画廊导出），放到 `out/diagnostics/ui-review/<步骤>/`；
  2. 对照设计规范逐条检查尺寸、间距、颜色 ID、各状态、键盘路径、焦点返回、`Esc`、文案，把检查结论写进步骤状态；
  3. 自动化测试覆盖键盘路径、焦点返回和关键几何。
- 截图和测试不等于真实桌面试用；预览说明中如实写明试用范围。

### 验证

| 场合 | 必须执行 |
| --- | --- |
| 每个步骤 | 步骤中列出的“相关套件”；新增测试在修复前失败（缺陷修复类）或覆盖新行为 |
| 性能类步骤 | 阶段 0.1 基准在改动前后各跑一次，结果写入 `out/test-results/bench-<步骤>-before.json`、`-after.json` |
| 一组步骤集中完成后 | 一次 Release 全量：`pwsh ./tools/build.ps1 -Configuration Release -Test` |
| 改了公开 API 或安装导出 | `pwsh ./tools/verify.ps1` 中的独立模块与外部宿主部分 |
| 改了应用行为 | `-Package` 更新固定部署目录，再运行 `tools/tests/application-exit-test.ps1 -PackageDirectory <固定目录>` |

测试命令的常用写法（PowerShell 7）：

```powershell
$qt = Get-Content .local/sdk.json -Raw | ConvertFrom-Json
$tools = Split-Path (Split-Path $qt.qtRoot)
$env:PATH = "$($qt.qtRoot)\bin;$(Split-Path $qt.compiler);$tools\Tools\Ninja;$env:PATH"
& "$tools/Tools/CMake_64/bin/cmake.exe" --build out/build/dev-release --parallel 4
& "$tools/Tools/CMake_64/bin/ctest.exe" --test-dir out/build/dev-release --output-on-failure -R "shortcut|sidebar"
```

单个 Qt 测试程序可以直接运行并写文本结果，例如：

```powershell
& out/build/dev-release/modules/window-system/tests/window-system-sidebar-presentation-test.exe -o "out/diagnostics/<名字>.txt,txt"
```

**失败处理**：按 [测试要求](../../development/testing-design.md)：不删用例、不跳过、不改预期、不放宽阈值。先判断是否与本步骤有关：

1. 在不含本步骤修改的基线上用同样的环境运行同一套件（`git stash` 或另一个 worktree）。
2. 基线也失败：属于既有问题。记录并单独修复（单独提交），或在阶段文档“发现”中登记后继续。
3. 只在本步骤失败：修复实现，直到原有断言通过。
4. 依赖真实桌面的套件（`native-frame`、`chrome-interaction`、`floating-panel` 等）失败时，先检查测试窗口是否被其他程序遮挡（`WindowFromPoint` 命中的窗口标题和进程），不能把遮挡造成的失败写成通过，也不能写成代码缺陷。

首次失败记录保存到 `out/diagnostics/<步骤>-*.txt`，不要覆盖。

### 完成后

1. 在阶段文档中把该步骤的“状态”改为“已完成（提交 `<hash>`）”，写一行实际验证结果。
2. 在 [预览说明](../../development/window-system-preview.md) 最上方的“当前可试用的功能”中加一节，写行为变化和真实验证范围；未执行的检查写明未执行。
3. 提交说明：标题一行；正文写行为变化、原因、实际验证（套件数、测试行数、失败及原因）；结尾附 `Co-Authored-By` 行（见会话中的要求）。

## 步骤文档的格式

每个步骤使用同一结构，便于不同 Agent 接手：

- **目标**：做完后可以观察到的结果。
- **前置**：必须先完成的步骤。
- **涉及文件**：要读和要改的文件。
- **实现**：编号的具体做法，包括数据结构、函数签名、算法和必须保持的不变量。
- **测试**：新增或修改的测试，写到测试函数名和关键断言。
- **验证**：要运行的套件和命令。
- **完成标准**：逐条可检查。
- **风险**：容易出错的地方和处理方式。
- **状态**：未开始／进行中／已完成（提交 hash）。
