# 工程结构与测试

## 1. 目录与模块

**现状**：

```
modules/window-system/       独立窗口模块（Model 与 Widgets 两个静态库，可安装导出）
modules/lua-runtime/         与 Qt Widgets 无关的 Lua 运行时
modules/window-system-lua/   窗口 API 的 Lua 适配
src/app/                     应用入口、用户偏好、脚本接入
src/workspace_host/          占位内容
third_party/lua/             固定版本 Lua 源码的获取与构建
tools/                       构建、部署、打包、验证脚本
```

模块边界清楚：窗口模块不依赖 Lua 或业务，Lua 运行时不依赖 Widgets，外部宿主夹具验证了安装导出。这个结构适合继续扩展。

**业务接入时建议的目录**（只在有实际代码时创建，不提前建空目录）：

```
src/editor/          Lua 编辑器与控制台（见 Lua 方案）
src/terminal/        终端内容：会话视图、输入映射、选择与复制
modules/terminal-view/  GPU 终端渲染（QRhi、字形图集）；不依赖 SSH，可独立测试
src/ssh/             连接、认证、通道、代理链、端口转发
src/sftp/            文件端点、传输队列、同步
src/sessions/        会话目录、导入、凭据引用
src/x11/             X server 发现与启动、DISPLAY 与认证
src/platform/windows/  凭据管理器、agent、ConPTY 等业务平台代码
tools/bench/         规模与性能基准（不进默认 CTest）
docs/adr/            架构决定记录
```

**应用入口**：`src/app/main.cpp` 的 `main()` 约 160 行，命令注册、布局读写和启动参数都写在 lambda 里。建议拆成：

- `layout_store.{hpp,cpp}`：布局文件读写（`QSaveFile`、大小上限、旧标题修正）；
- `app_commands.{hpp,cpp}`：`host.*`、`layout.*`、`application.exit`、`help.about` 的注册；
- `main.cpp` 只负责创建对象、解析参数和启动。

拆分时行为不变，由现有部署夹具（`tools/tests/*.ps1`）和应用测试验证。

**ADR**：`docs/README.md` 已约定 `adr/` 目录但还没有内容。建议补记已经做出的几项决定：自研停靠（不用 KDDockWidgets）、Lua 以 C++ 模式编译并修改 GC 钩子、Lua 沙箱范围、MinGW 与 MSVC 工具链的选择（见 §4）。

## 2. 代码组织

| 问题 | 位置 | 建议 |
| --- | --- | --- |
| `WindowManager` 公开约 150 个方法 | `window_manager.hpp` | 按领域分出轻量门面：`layout()`（移动、合并、转换）、`chrome()`（工具条、状态项）、`view()`（显隐、全屏、禅模式）。旧方法保留一个版本周期，标注弃用后再删 |
| 超过 1000 行的源文件 | `layout_model.cpp`（约 1700）、`command_strip.cpp`（约 1500）、`window_manager.cpp`（约 1080） | 模型拆成核心操作、边栏与固定、JSON、校验四个文件；工具条拆成布局计算（纯函数，可单测）与控件 |
| 一行写多条语句较常见 | 多处 | 增加 `.clang-format`，只格式化本次修改的代码，避免一次性的大面积改动 |
| `TabContent` 的栏用 `delete`，窗格栏用 `deleteLater` | 见审查记录 | 统一规则并写注释 |

**静态检查**：增加 `.clang-tidy`，先启用 `bugprone-*`、`performance-*`，以及 `modernize-*` 的一部分；Qt 的 parent 所有权会让 `cppcoreguidelines-owning-memory` 大量误报，不启用。应用目标 `muxora` 目前没有开启 `-Wall -Wextra`（模块目标已开启）。按本轮要求不改 Release 默认编译选项，以后单独评估。

## 3. 测试

**现状**：59 个 CTest 套件；窗口模块约 50 个测试程序，各自静态链接 WindowSystem；大部分套件设为 `RUN_SERIAL`、超时 60 秒。

**冗余与成本**：

- 每个测试程序都要链接一次完整的静态库，增量构建时链接占大头。建议按领域合并成几个运行器（一个可执行文件内含多个 `QObject` 测试类，自写 `main` 逐个调用 `QTest::qExec`），CTest 仍按测试类分别登记（通过参数选择测试类），保持现有的结果粒度。
- 不少界面测试检查精确像素和几何（2 像素间隙、12 像素宽的下拉按钮等）。这些是有意写下的设计契约，不应删除；但修改视觉规格时成本高。建议把这类断言集中到少数“视觉契约”套件，其他交互测试只检查行为。这是组织调整，不降低任何断言。

**分层**：用 CTest `LABELS` 标出三层，入口按需选择：

| 标签 | 内容 | 并行 |
| --- | --- | --- |
| `model` | 纯模型、命令注册、Lua 运行时 | 可以并行 |
| `widget` | 可以在 `offscreen` 平台运行的控件测试 | 可以并行 |
| `desktop` | 需要真实桌面、原生窗口、鼠标和焦点的测试 | 串行 |

现在全部串行，完整运行偏慢。分层后日常可以只跑 `model` 和 `widget`，提交前再跑完整矩阵。

**不稳定用例**：原生悬停（`chrome-interaction`）和窗口曝光（本轮一次 `floating-panel`，见预览说明）偶发失败。按测试规范不放宽断言、不跳过；建议给 `desktop` 层的失败自动保存事件日志，并在报告里与通过分开统计。

**缺口**：

| 缺口 | 建议 |
| --- | --- |
| 没有规模测试 | 见 [性能 §6](performance-plan.md#6-测量与预算) |
| 布局 JSON 和快捷键文件缺少随机输入测试 | 固定种子的变异测试：从合法快照出发随机删改字段，要求 `fromJson()` 要么成功且 `validate()` 通过，要么失败且模型不变 |
| 没有 ASan/UBSan 构建 | 见 §4 |
| 主题只测了一套 | 深色主题加入后参数化现有像素测试 |

## 4. 工具链与构建

**文档与实际不一致**：[工程设计 §6.1](../development/engineering-design.md) 写的是首选 MSVC 2022 预编译 Qt，实际构建和部署使用 Qt 6.12 MinGW（GCC 15.1）。两者都可行，需要用 ADR 明确：

- 继续 MinGW：保持现状；ASan 需要改用 llvm-mingw 或单独的诊断构建。
- 增加 MSVC 诊断构建：使用 MSVC 版 Qt 和 `/fsanitize=address`，只用于诊断，不影响 Release 预览。

建议先补一个 MSVC（或 clang）诊断预设跑 `model` 和 `widget` 层，Release 预览仍然用现在的 MinGW 构建。

**构建速度**：测试目标启用 `target_precompile_headers`（Qt 头文件）和 `UNITY_BUILD`；配合 §3 的合并运行器，链接次数会大幅下降。只作用于测试目标，不改变 Release 应用的编译选项。

**CI**：Windows runner 缓存固定版本的 Qt SDK。PR 跑构建、`model`、`widget`；每日跑 `desktop` 层和部署夹具。CI 结果与本地结果分开记录。

## 5. 文档

`docs/development/window-system-preview.md` 约 75 KB，既写当前状态，又按日期累积了每轮的验证记录，查当前状态时很难找。建议：

- 预览说明只保留“当前可试用功能”“快捷键”“最近一次验证结果”；
- 历史验证记录按月份移到 `docs/development/verification-log/2026-10.md`，或并入 `CHANGELOG.md`；
- 路线类内容统一放在 `docs/roadmap/`（本组文档）。
