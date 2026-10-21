# 阶段 6：工程（入口拆分、测试运行器、格式与检查、诊断构建、CI）

**目的**：降低构建和测试成本，让问题更早暴露，为业务接入准备结构。方案背景见 [工程方案](../review-2026-10/engineering-plan.md)。

**阶段约束**：

- 纯重构步骤（6.1、6.7）不改变任何行为，验证以现有测试和部署夹具为准；提交说明写明“无行为变化”。
- 不改变 Release 默认编译选项。新增的告警、检查和诊断构建都通过默认关闭的选项或单独的预设启用。
- 格式化只作用于本次修改的行，不做全仓库重排。

---

## 6.1 应用入口拆分

**目标**：`src/app/main.cpp` 只负责创建对象、解析参数和启动；布局读写与命令注册各自成文件。

**涉及文件**：`src/app/main.cpp`；新建 `src/app/layout_store.{hpp,cpp}`、`src/app/app_commands.{hpp,cpp}`、`src/app/startup_options.{hpp,cpp}`；`src/app/CMakeLists.txt`。

**实现**：

1. `startup_options`：把 `main()` 开头手工预扫描 `--state-dir` 的循环和 `QCommandLineParser` 的选项定义移入：

   ```cpp
   struct StartupOptions { QString stateDirectory; bool fresh = false; bool smoke = false;
                           QString screenshot; QString command; };
   QString prescanStateDirectory(const QStringList& arguments); // 原循环，原样搬移
   void addStartupOptions(QCommandLineParser& parser);          // 翻译文字不变
   StartupOptions readStartupOptions(const QCommandLineParser& parser);
   ```

2. `layout_store`：`save` lambda 与恢复块（包括“旧版本保存了 WindowSystem 标题时改为应用名”的修正）搬为

   ```cpp
   class LayoutStore {
   public:
       LayoutStore(window_system::WindowManager& windows, QString stateRoot);
       bool save();                              // 原 save lambda
       bool restore(const QString& applicationName); // 原恢复块，返回是否恢复成功
       [[nodiscard]] QString path() const;
   };
   ```

   4 MiB 读取上限、`QSaveFile` 原子写入、空窗口时不保存，全部保持。阶段 1.9 以后在这里加异步保存。

3. `app_commands`：`host.newTab`、三个预览类型命令、`layout.save`、`application.exit`、`layout.reset`、`help.about` 的注册搬入 `void registerApplicationCommands(WindowManager&, LayoutStore&, int& tabCounter)`。命令 ID、标题、类别、菜单路径、顺序值、默认快捷键全部不变。
4. 测试程序 `muxora-localization-test` 等目前直接编译 `main` 之外的源文件；检查 `src/app/CMakeLists.txt` 中各测试目标的源文件列表，必要时加入新文件。

**验证**：应用全部测试；`-Package` 后运行 `application-exit-test.ps1`、`application-view-test.ps1`、`lua-deployment-test.ps1`（三者覆盖启动参数、保存、恢复和退出）；`muxora.exe --smoke`、`--fresh` 手工各启动一次确认退出码。

**完成标准**：`git diff` 中 `main.cpp` 只剩组装代码（约 60 行以内）；所有夹具通过。

**状态**：未开始。

---

## 6.2 测试运行器合并

**目标**：约 50 个窗口模块测试程序合并为三个运行器（与 0.2 的三层对应），每次增量构建只链接三次；CTest 中的套件名称和结果文件名不变。

**前置**：0.2。

**涉及文件**：新建 `modules/window-system/tests/support/test_main.hpp`、`support/test_runner_main.cpp`；全部 `*_test.cpp` 的最后一行宏；`modules/window-system/tests/CMakeLists.txt`。

**实现**：

1. 现状：51 个文件用 `QTEST_MAIN`，6 个用 `QTEST_GUILESS_MAIN`，2 个用 `QTEST_APPLESS_MAIN`（全仓库统计，含 Lua 与应用测试）。
2. `test_main.hpp`：

   ```cpp
   #ifdef WINDOW_SYSTEM_COMBINED_TESTS
   namespace window_system_tests {
   enum class AppKind { Gui, Core, None };
   bool registerSuite(const char* name, AppKind kind, QObject* (*factory)());
   }
   #define WINDOW_SYSTEM_TEST_MAIN(Class, Name, Kind) \
       static const bool Class##_registered = window_system_tests::registerSuite( \
           Name, window_system_tests::AppKind::Kind, [] () -> QObject* { return new Class; });
   #else
   #define WINDOW_SYSTEM_TEST_MAIN(Class, Name, Kind) WINDOW_SYSTEM_TEST_MAIN_##Kind(Class)
   #define WINDOW_SYSTEM_TEST_MAIN_Gui(Class) QTEST_MAIN(Class)
   #define WINDOW_SYSTEM_TEST_MAIN_Core(Class) QTEST_GUILESS_MAIN(Class)
   #define WINDOW_SYSTEM_TEST_MAIN_None(Class) QTEST_APPLESS_MAIN(Class)
   #endif
   ```

   `Name` 与 CTest 套件名一致（例如 `"sidebar-presentation"`）。

3. `test_runner_main.cpp`：读取第一个参数 `--suite <name>`，找到注册项，按 `AppKind` 创建 `QApplication`／`QCoreApplication`／不创建，复制 `QTEST_MAIN` 展开后的初始化（`QTEST_SET_MAIN_SOURCE_PATH`、`app.setAttribute(Qt::AA_Use96Dpi, true)` 等——以 Qt 6.12 的 `qtest.h` 实际展开为准，逐条照抄），然后 `QTest::qExec(object, 剩余参数)`。未知套件返回非零并列出可用名称。
4. 每个测试文件把 `QTEST_MAIN(X)` 改为 `WINDOW_SYSTEM_TEST_MAIN(X, "<套件名>", Gui)`，其余不变；`#include "<文件>.moc"` 保留。
5. `CMakeLists.txt`：三个运行器目标 `window-system-model-tests`、`window-system-widget-tests`、`window-system-desktop-tests`，各自 `target_compile_definitions(... PRIVATE WINDOW_SYSTEM_COMBINED_TESTS)` 并包含该层的全部测试源文件；`add_test(NAME window-system.<suite> COMMAND <runner> --suite <suite> -o <原结果文件>,txt)`，属性（`LABELS`、`TIMEOUT`、`RUN_SERIAL`）与之前相同。
6. 同一运行器中的测试文件可能有同名的匿名命名空间辅助函数或同名的 `struct Fixture`——它们在各自的编译单元中，不冲突；但同名的非静态全局函数会在链接时冲突。逐个处理链接错误：改成 `static` 或放进匿名命名空间，不改函数内容。
7. 外部宿主夹具和点击穿透夹具（`fixtures/`）保持独立程序。
8. 保留一个开关 `WINDOW_SYSTEM_SEPARATE_TEST_EXECUTABLES`（默认 OFF）恢复逐文件程序，便于定位单个套件的构建问题。

**测试与验证**：

- `ctest -N` 列出的套件名与合并前完全相同（把合并前的列表保存为 `out/test-results/suite-names-before.txt` 比较）。
- 全量运行一次，结果行数与合并前的最近一次全量一致（0.3 汇总脚本比较）。
- 记录合并前后：一次修改 `window_manager.cpp` 后的增量构建时间、`out/build/dev-release` 目录大小。

**完成标准**：套件名、结果文件和行数一致；增量构建时间明显下降（写出数字）。

**风险**：`QTEST_MAIN` 在不同 Qt 版本中的展开不同；运行器中的初始化必须与当前 Qt 版本一致。把 Qt 版本号写进运行器的注释，并在升级 Qt 的检查清单中加入这一项。

**状态**：未开始。

---

## 6.3 格式与静态检查配置

**涉及文件**：新建 `.clang-format`、`.clang-tidy`；`docs/development/engineering-design.md`（说明用法）；可选 `tools/format-changed.ps1`。

**实现**：

1. `.clang-format`（贴近现有代码；先在几个文件上试运行，调整到改动最少）：

   ```yaml
   BasedOnStyle: LLVM
   IndentWidth: 4
   ColumnLimit: 120
   AllowShortFunctionsOnASingleLine: All
   AllowShortIfStatementsOnASingleLine: WithoutElse
   AllowShortLoopsOnASingleLine: true
   BreakBeforeBraces: Custom
   BraceWrapping: { AfterFunction: true, AfterClass: false, AfterNamespace: false }
   PointerAlignment: Left
   NamespaceIndentation: None
   SortIncludes: Never
   ```

   `modules/lua-runtime` 现有代码使用不同风格（指针右对齐、`  public:` 缩进），在该目录放一个单独的 `.clang-format` 保持其风格。

2. 使用方式：只格式化改动行，`git clang-format --diff origin/main`；`tools/format-changed.ps1` 包装这条命令。CI 中检查新增改动是否已格式化，不检查历史代码。
3. `.clang-tidy`：

   ```yaml
   Checks: >
     -*,bugprone-*,-bugprone-easily-swappable-parameters,
     performance-*,modernize-use-override,modernize-use-nullptr,modernize-loop-convert,
     readability-container-size-empty
   WarningsAsErrors: ''
   HeaderFilterRegex: '(modules|src)/.*'
   ```

   运行需要 `compile_commands.json`（预设已开启 `CMAKE_EXPORT_COMPILE_COMMANDS`）和一个能读 MinGW 头文件的 clang-tidy（llvm-mingw 自带）。工具不存在时脚本给出说明并退出，不算失败也不算通过。
4. 首次运行的结果保存到 `out/diagnostics/clang-tidy-baseline.txt`，按类别统计数量，写入本步骤状态；修复放到后续单独的提交中，每类一个提交。

**状态**：未开始。

---

## 6.4 诊断构建（ASan／UBSan）

**目标**：有一个可以检测越界、释放后使用和未定义行为的构建，用于 `model` 与 `widget` 层测试。

**实现**（2026-10-20 修订：Release 已改用 MSVC（[ADR 0009](../../adr/0009-msvc-toolchain.md)），诊断构建直接走 MSVC，不再需要另一套 Qt）：

- 新增用户预设 `diag-msvc-asan`，与 Release 共用 `msvc2022_64` 套件和 `tools/toolchain.ps1` 载入的编译环境：`CMAKE_BUILD_TYPE=RelWithDebInfo`、对本项目目标加 `/fsanitize=address /Zi`（目标级选项，不改全局）。Qt 本身不带 ASan 插桩，越界检查覆盖本项目的代码。MSVC 的 ASan 不包含泄漏检测，泄漏仍靠现有的对象与句柄回落测试。
- UBSan：MSVC 没有对应功能。可选用 clang-cl（使用 MSVC 的 ABI 和标准库，能链接同一套 Qt）加 `-fsanitize=undefined`，作为第二条诊断构建；不可用时记录“环境阻塞”。

`tools/diagnose.ps1`：选择可用路线、配置到 `out/build/diag-*`、构建、运行 `ctest -L "model|widget"`，结果写入 `out/test-results/diag-*.xml`。工具链不存在时明确报告“环境阻塞”，不能写成通过。

**完成标准**：MSVC ASan 构建可用并完成一次运行；发现的问题逐个登记到审查记录，修复各自单独提交并附回归测试。

**状态**：未开始。

---

## 6.5 应用目标的严格告警（可选开关）

**实现**：`src/app/CMakeLists.txt` 增加 `option(MUXORA_STRICT_WARNINGS "Enable strict warnings for application targets" OFF)`；开启时对 `muxora` 及应用测试目标加 `-Wall -Wextra -Wpedantic`（MSVC 为 `/W4 /permissive-`）。默认关闭，不改变 Release 默认构建。开启后修复告警，修复不改变行为。CI 中开启。

**状态**：未开始。

---

## 6.6 持续集成

**目标**：每个 PR 自动构建并运行 `model`、`widget` 层；每日运行 `desktop` 层和部署夹具。

**涉及文件**：新建 `.github/workflows/ci.yml`、`.github/workflows/nightly.yml`。

**实现**：

1. Runner：`windows-2025`（或当时最新的 Windows 镜像）。
2. Qt 获取：先验证可行性——[工程设计 §6.1](../../development/engineering-design.md) 记录了 aqtinstall 在 Qt 6.11 之后的仓库布局问题。依次尝试：(1) 新版 `install-qt-action`／aqtinstall 是否已支持 6.12 MinGW；(2) 官方在线安装器的无人值守模式（需要 Qt 账号，凭据放在仓库机密中）。两者都不可行时，CI 只构建不依赖 Qt 的部分是没有意义的——记录为“环境阻塞”，不要用其他 Qt 版本代替。
3. 缓存：Qt kit（按版本和编译器做键）、Lua 源码包（`FetchContent` 下载目录）。
4. PR 工作流：配置 → 构建 → `ctest -L "model|widget" --output-junit` → 上传 `out/test-results/` 和失败时的 `out/diagnostics/`。
5. 每日工作流：全量 `ctest`、`tools/verify.ps1` 的独立模块与外部宿主部分、`-Package` 加部署夹具。桌面层在托管 runner 上可能因没有交互桌面而失败；如果失败原因是环境，结果单独标注，不与代码失败混在一起。
6. CI 结果与本地结果分开记录；本地验证仍按 [README](README.md#验证) 执行。

**状态**：未开始。

---

## 6.7 大文件拆分

**目标**：在不改变行为的前提下，把超过 1000 行的文件拆成按职责划分的文件。

**实现**（逐个文件、各自提交）：

1. `model/layout_model.cpp`（约 1700 行）→ `layout_model.cpp`（查询与核心操作）、`layout_json.cpp`（`toJson`、`fromJson` 及 JSON 辅助）、`layout_validation.cpp`（`validate()` 及其辅助）、`layout_edges.cpp`（边栏与固定相关操作）。匿名命名空间中的共享辅助函数移到内部头文件 `model/layout_model_p.hpp`。
2. `chrome/command_strip.cpp`（约 1500 行）→ 把纯几何计算（行分配、弹性宽度、压缩）提取为不依赖控件的函数，放到 `chrome/strip_layout.{hpp,cpp}`，并为它们补单元测试（输入条带尺寸，输出几何）。控件部分留在原文件。
3. `window_manager.cpp`（约 1080 行）→ 已经有 `tab_group_api.cpp`、`hierarchy_api.cpp` 等拆分先例；把 `eventFilter` 及快捷键分发移到 `commands/shortcut_dispatch.cpp`，把 `refresh()` 及视图调和移到 `view_reconcile.cpp`（与阶段 1 的修改协调，阶段 1 完成后再做）。

**验证**：每次拆分后全部相关套件通过；`toJson()` 输出不变；外部宿主夹具通过。

**状态**：未开始。

---

## 6.8 `WindowManager` 领域门面（后续）

**目标**：减少 `WindowManager` 的公开方法数量，按领域分组。

**实现要点**：增加轻量门面对象（值类型，内部只保存 `WindowManager*`），例如 `manager.layout().moveTab(...)`、`manager.chrome().registerToolbar(...)`、`manager.view().setZenMode(...)`；旧方法转发到门面，标注 `[[deprecated]]` 一个版本周期后删除。门面必须在外部宿主夹具中使用。这一步改变公开 API，需要先写设计说明并经过评审，再拆分实现。

**状态**：未开始（需要设计评审）。
