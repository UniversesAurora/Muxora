# 阶段 0：基线与度量

**目的**：后续性能改动必须先有可以重复的测量，测试也要能按层次选择运行。本阶段只增加工具和文档，不改变应用行为。

**阶段约束**：

- 基准程序由默认关闭的 CMake 选项控制，在单独的构建目录中编译，不影响 `out/build/dev-release` 和固定部署目录。
- 基准结果是本机测量，记录机器、Qt 版本、提交和平台插件；不写成跨机器的结论。

---

## 0.1 规模基准程序

**目标**：一条命令得到 100／500／1000／2000 个标签下的创建、关闭、移动、拆分、分隔条、按键、保存与循环开关的耗时和内存数据，输出 JSON。

**前置**：无。

**涉及文件**：

- 新建 `modules/window-system/benchmarks/CMakeLists.txt`
- 新建 `modules/window-system/benchmarks/layout_scale_benchmark.cpp`
- 新建 `modules/window-system/benchmarks/benchmark_support.hpp`、`benchmark_support.cpp`
- 修改 `modules/window-system/CMakeLists.txt`（增加选项）
- 新建 `tools/bench.ps1`

**实现**：

1. 在 `modules/window-system/CMakeLists.txt` 的测试选项旁增加：

   ```cmake
   option(WINDOW_SYSTEM_BUILD_BENCHMARKS "Build WindowSystem scale benchmarks" OFF)
   if(WINDOW_SYSTEM_BUILD_BENCHMARKS)
       add_subdirectory(benchmarks)
   endif()
   ```

   不调用 `add_test`，基准不进入 CTest。

2. `benchmarks/CMakeLists.txt`：

   ```cmake
   qt_add_executable(window-system-layout-benchmark layout_scale_benchmark.cpp benchmark_support.cpp)
   target_link_libraries(window-system-layout-benchmark PRIVATE WindowSystem::Widgets Qt6::Test)
   target_include_directories(window-system-layout-benchmark PRIVATE ../src)
   if(WIN32)
       target_link_libraries(window-system-layout-benchmark PRIVATE psapi)
   endif()
   ```

   需要 `find_package(Qt6 REQUIRED COMPONENTS Test)`，因为按键场景使用 `QTest::keyClick`。

3. `benchmark_support.hpp` 提供：

   ```cpp
   struct Samples { QVector<double> ms; };
   struct Summary { double p50 = 0, p95 = 0, max = 0, mean = 0; int count = 0; };
   Summary summarize(QVector<double> values);          // 排序后取分位
   quint64 privateBytes();                             // Windows: GetProcessMemoryInfo + PROCESS_MEMORY_COUNTERS_EX::PrivateUsage；其他平台返回 0
   void drainDeferredDeletes();                        // QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete) 后 processEvents
   int liveWidgetCount();                              // QApplication::allWidgets().size()
   ```

   分位数用“最近秩”法：`index = ceil(p * n) - 1`，不插值，在文件注释中写明。

4. `layout_scale_benchmark.cpp`：

   - `QCommandLineParser` 参数：`--tabs 100,500,1000,2000`、`--repeat 30`、`--scenario all|create|close|move|split|ratio|keys|save|cycle`（可多次）、`--out <path>`、`--commit <hash>`。
   - 每个规模新建一个 `WindowManager` 和一个完整窗口，窗口尺寸 1600×1000，调用 `showWindow` 并等待曝光（使用 `QTest::qWaitForWindowExposed`）。平台插件由环境变量决定：脚本默认设置 `QT_QPA_PLATFORM=offscreen`，`-Visible` 时不设置。
   - **构造工作区**：每 25 个标签开一个新组：第一个组用 `addTab`，之后用 `addTabToNewGroup(window, content, options, previousGroup, placement)`，`placement` 在 `Right` 与 `Bottom` 间交替，使中央树有深度。内容用 `new QLabel(QString::number(i))`，`contentType = "bench"`。
   - **场景**（每项测 `--repeat` 次，记录每次耗时）：

     | 场景 | 一次操作 | 恢复方式 |
     | --- | --- | --- |
     | create | 从空窗口加到 N 个标签，记录总耗时和每 100 个标签的累计耗时 | 每个规模只做一次 |
     | close | `closeTab(中间某个标签)` | 关闭后 `addTab` 补回同一组，补回不计时 |
     | move | `moveTab(tab, otherGroup, Placement::Center)` | 交替移回原组 |
     | split | `splitPane(tab, pane, new QLabel, Placement::Right)` | `closePane` 不计时 |
     | ratio | `execute("layout.setRatio", {{"node", id}, {"ratio", r}})`，`r` 在 0.3 与 0.7 间交替；`id` 取中央树根节点 | 无 |
     | keys | 焦点放在一个内容上，`QTest::keyClick(content, Qt::Key_X)` 连续 1000 次，记录总耗时 / 1000 | 无 |
     | save | `QJsonDocument(manager.saveLayout()).toJson()`，同时记录字节数 | 无 |
     | cycle | 1000 次“`addTab` + `closeTab`”，每 100 次调用 `drainDeferredDeletes()` 后记录 `privateBytes()` 和 `liveWidgetCount()` | 无 |

   - **内存**：create 完成后先 `drainDeferredDeletes()`，记录 `privateBytes()`、`liveWidgetCount()`、每标签平均增量（减去空窗口时的值）。
   - 每个计时区间结束后调用 `QCoreApplication::processEvents()`，并把它计入耗时：用户看到的是事件循环处理完之后的结果。

5. 输出 JSON（UTF-8，缩进）：

   ```json
   {
     "benchmark": "layout-scale",
     "commit": "<hash>",
     "qt": "6.12.0",
     "platform": "offscreen",
     "machine": { "cpu": "<QSysInfo::currentCpuArchitecture()>", "logicalCores": 16 },
     "results": [ { "scenario": "close", "tabs": 1000, "samples": 30, "p50Ms": 0, "p95Ms": 0, "maxMs": 0, "meanMs": 0 } ],
     "memory": [ { "tabs": 1000, "privateBytes": 0, "widgets": 0, "bytesPerTab": 0, "widgetsPerTab": 0 } ],
     "cycle": [ { "tabs": 1000, "iteration": 100, "privateBytes": 0, "widgets": 0 } ],
     "save": [ { "tabs": 1000, "bytes": 0 } ]
   }
   ```

6. `tools/bench.ps1`：

   - 参数：`-Name <label>`（必填，用于文件名）、`-Tabs '100,500,1000,2000'`、`-Repeat 30`、`-Visible`。
   - 读取 `.local/sdk.json` 中的 `qtRoot`、`compiler`（由 `tools/build.ps1` 生成）；找不到时提示先运行 `build.ps1`。
   - 配置单独目录：`cmake -S . -B out/build/bench-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<qtRoot> -DCMAKE_CXX_COMPILER=<compiler> -DCMAKE_MAKE_PROGRAM=<ninja> -DBUILD_TESTING=OFF -DWINDOW_SYSTEM_BUILD_BENCHMARKS=ON`。
   - 只构建目标 `window-system-layout-benchmark`；运行时把 Qt `bin` 加入 PATH；`--commit` 传 `git rev-parse --short HEAD`；输出 `out/test-results/bench-<Name>.json`。

**测试**：基准本身不是测试。为 `summarize()` 写一个小的 Qt 测试即可：`window-system.benchmark-support`（放在 `modules/window-system/tests/`，只在基准选项开启时编译）。断言：`{1..100}` 的 p50 为 50、p95 为 95、max 为 100；空输入返回 `count == 0`。

**验证**：

```powershell
pwsh ./tools/bench.ps1 -Name baseline
```

检查 JSON 字段齐全；用 `-Tabs 100 -Repeat 3` 快速跑一遍确认可用。默认 `tools/build.ps1 -Configuration Release` 的构建目录中不应出现基准目标。

**完成标准**：

- [x] 默认 Release 构建产物与之前相同（不出现基准目标，`muxora.exe` 不受影响）。
- [ ] `bench-baseline.json` 生成，八个场景在四个规模下都有数据。（按用户决定只跑了 100／500／1000 三个规模，见下方状态）
- [x] 在 [性能方案 §6](../review-2026-10/performance-plan.md#6-测量与预算) 末尾追加“基线数据”表，写明机器、平台插件、提交。

**风险**：offscreen 平台不走原生窗口和 DWM，绘制成本偏低。表格中注明平台；需要原生数据时用 `-Visible` 另跑一份。

**状态**：已完成（提交标题 “Add opt-in WindowSystem layout scale benchmark”），2000 个标签这一档未运行。基线结果见 [性能方案 §6 基线数据](../review-2026-10/performance-plan.md#基线数据2026-10-19)。

实现与上面的差别：

- 每次计时在 `processEvents()` 之后还要执行 `deleteLater()`（`settleEventLoop()`：先 `processEvents()`，再 `drainDeferredDeletes()`），创建循环和场景准备也这样处理。原因：基准不进入 `exec()`，此时 `processEvents()` 不会执行 DeferredDelete，被重建替换掉的视图会一直留着。第一次正式运行就是这样：500 个标签的创建跑了 30 多分钟，进程私有提交涨到 1.4 GB，运行被停止。日志保留在 `out/diagnostics/0.1-bench-*-attempt1-no-deferred-delete.*`，这些数据没有使用。真实的事件循环每一轮都会执行延迟删除，所以计入耗时。
- 每跑完一个规模就重写一次输出文件，JSON 增加 `completedTabs` 和 `complete`，中途停止时已完成的规模仍然保留。第二次运行是在 1000 档的 cycle 中途按用户决定停止的，那时结果只在最后写出，所以没有留下数据（日志 `0.1-bench-baseline-attempt2-stopped-at-1000-cycle.txt`）。
- JSON 另有 `tabs`（请求的规模）、`repeat`、`machine.os` 和 `createProgress`（每 100 个标签的累计创建耗时）；`cycle` 多记一个第 0 次的起点。`--commit` 在工作区有未提交的已跟踪改动时加 `-dirty` 后缀。`--tabs` 的每个值必须大于 25，保证 move 至少有两个组。
- 运行时间：三档合计 3 小时 24 分钟，四档估计要 8 小时以上。按用户决定，`bench.ps1 -Tabs` 和程序 `--tabs` 的默认值改为 `100,500,1000`，2000 用 `-Tabs 2000` 单独补测。

验证：

- 基线：`pwsh ./tools/bench.ps1 -Name baseline -Tabs '100,500,1000'`，`complete: true`，三个规模的八个场景都有数据，另有 `memory`、`cycle`、`save` 各三组。提交标记为 `dae64b0-dirty`：窗口模块库代码与 `01df918` 相同，未提交的只有本步骤的基准文件。
- 冒烟：修正后 `-Tabs 100 -Repeat 3` 一次通过（`out/test-results/bench-smoke.json`）。
- `window-system.benchmark-support`：在临时目录（`BUILD_TESTING=ON`、`WINDOW_SYSTEM_BUILD_BENCHMARKS=ON`）中构建运行，2 行通过，带 `model` 标签；开启基准选项时 CTest 共 60 个，临时目录已删除。
- 默认构建：`tools/build.ps1 -Configuration Release` 重新配置后 `WINDOW_SYSTEM_BUILD_BENCHMARKS=OFF`，`build.ninja` 中没有基准目标，`muxora.exe` 没有重新链接；CTest 仍为 59 个。

---

## 0.2 测试分层标签

**目标**：`ctest -L model`、`-L widget`、`-L desktop` 可以分别运行三层；不显示窗口的套件可以并行。

**前置**：无。

**涉及文件**：`modules/window-system/tests/CMakeLists.txt`、`modules/lua-runtime/tests/CMakeLists.txt`、`modules/window-system-lua/tests/CMakeLists.txt`、`src/app/CMakeLists.txt`、`tools/build.ps1`、`docs/development/testing-design.md`。

**实现**：

1. 分类规则（按测试源码判断，写进 `testing-design.md`）：
   - `desktop`：调用 `showWindow`、`show()`、`qWaitForWindowExposed`、`QTest::mouse*`、`QCursor::setPos` 或 Win32 API 的套件；
   - `widget`：创建 `QWidget` 但不显示窗口；
   - `model`：只链接 `WindowSystem::Model`、`CommandRegistry` 或 Lua 运行时，不创建控件。
2. 用下面的命令初步分类，再逐个打开源码确认：

   ```bash
   cd modules/window-system/tests
   grep -lE "showWindow|\.show\(\)|qWaitForWindowExposed|QTest::mouse|QCursor::setPos|windows\.h" *_test.cpp
   ```

3. 在 `modules/window-system/tests/CMakeLists.txt` 中，`foreach(suite ...)` 改为三个列表 `window_system_model_suites`、`window_system_widget_suites`、`window_system_desktop_suites`，循环体相同，只是 `set_tests_properties` 增加 `LABELS`。`desktop` 保留 `RUN_SERIAL TRUE`；`model` 和 `widget` 去掉 `RUN_SERIAL`。单独写出的套件（model、widgets、extension-api、drag-interaction、custom-chrome、tab-ordering-model 等）逐个加 `LABELS`。
4. 其他三个 CMakeLists 同样加标签：`lua-runtime.runtime` 为 `model`；`window-system.lua`、`window-system.lua-lifecycle` 按源码判断；应用的 `muxora.state-paths`、`muxora.user-preferences` 为 `model`，`muxora.localization`、`muxora.scripting` 按源码判断。
5. `tools/build.ps1` 增加 `[string]$TestLabel`，非空时给 ctest 传 `-L $TestLabel`；junit 文件名加上标签后缀。

**测试**：无新增 Qt 测试。用 CTest 本身验证：

```powershell
ctest --test-dir out/build/dev-release -N -L model   # 只列出，不运行
ctest --test-dir out/build/dev-release -N            # 总数仍为 59
```

**验证**：分别运行三层各一次；再并行运行 `ctest -L "model|widget" -j 8`，与串行结果比较，失败数必须一致（都为 0，或失败都能在基线复现）。

**完成标准**：

- [x] 三层套件数之和等于总套件数，没有套件漏标签。
- [x] 去掉 `RUN_SERIAL` 的套件在 `-j 8` 下连续三次通过。若某个套件在并行时失败而串行通过，说明分类错误，改回 `desktop`，不要修改测试本身。
- [x] `testing-design.md` 写明分类规则与命令。

**风险**：有些套件在构造函数里创建窗口但不显示，可能仍依赖焦点。以并行实测为准。

**状态**：已完成（提交 `530eede`）。实际分类：

| 标签 | 数量 | 套件 |
| --- | --- | --- |
| `model` | 8 | `window-system.model`、`tab-ordering-model`、`movement-model`、`pinned-layout`、`pinned-positions`、`lua-runtime.runtime`、`muxora.state-paths`、`muxora.user-preferences` |
| `widget` | 10 | `window-system.extension-api`、`custom-chrome`、`hierarchy-conversion`、`visibility-profile`、`tab-appearance`、`shortcut-settings`、`localization`、`window-system.lua`、`muxora.localization`、`muxora.scripting` |
| `desktop` | 41 | 其余 window-system 套件（含 `widgets`、`drag-interaction`、`native-frame`、`view-state`）和 `window-system.lua-lifecycle` |

验证（Release，`out/build/dev-release`）：`ctest -N` 共 59 个，三层 8 + 10 + 41 = 59，`-LE` 排除三层后为 0；`RUN_SERIAL` 只出现在 41 个 `desktop` 套件上。`-L "model|widget"` 串行一次、`-j 8` 连续三次，18 个套件均通过，`test-summary.ps1` 统计每次 732 行通过、0 行失败。`-L desktop` 串行一次，41 个套件通过（287 s）。三层合计 2189 行通过、0 行失败（汇总 `out/test-results/0.2-after-layers-summary.json`）。`tools/build.ps1 -Configuration Release -Test -TestLabel "model|widget"` 运行 18 个套件，报告为 `out/test-results/dev-release-model-widget.xml`。

分类说明：`view-state` 不在 grep 结果中，但通过 `restorePlacement()` 最大化和全屏真实窗口，归入 `desktop`。`window-system.lua-lifecycle` 只为读取进程内存计数包含 `windows.h`，按规则归入 `desktop`。`window-system.widgets` 原来没有 `RUN_SERIAL`，但它调用 `showWindow`，现归入 `desktop` 并改为串行。`window-system.lua`、`muxora.scripting`、`extension-api`、`custom-chrome` 原来带 `RUN_SERIAL`，现在归入 `widget`，以上三次并行运行均通过。

---

## 0.3 测试结果汇总脚本

**目标**：统一统计“实际测试行”（不含 `initTestCase`／`cleanupTestCase`），生成 JSON，取代手工统计。

**涉及文件**：新建 `tools/test-summary.ps1`。

**实现**：

1. 参数：`-BuildDirectory`（默认 `out/build/dev-release`）、`-Output`（必填）。
2. 递归读取 `*-results.txt`（这些文件由各 `add_test(... -o <name>-results.txt,txt)` 生成）。
3. 逐行匹配 `^(PASS|FAIL!|SKIP|BLACKLIST|XFAIL|XPASS)\s*:\s*(\S+)::(\w+)(\((.*)\))?`。按“套件文件 + 函数名 + 数据标签”去重：同一行出现多条 `FAIL!` 只算一个失败行；同一行既有 `FAIL!` 又有 `PASS` 的情况按失败算。
4. 输出：每个套件的通过／失败／跳过行数、失败行列表；以及总计。文件不存在或为空的套件单独列出“无结果”。

**验证**：对当前构建目录运行，结果应与审查记录中的数字一致（最近一次全量为 2168 个通过行；失败行按去重规则计算）。

**状态**：已完成（提交 `dae64b0`）。实现与上面的差别：

- 数据标签改为懒惰匹配，取到后面跟空白或行尾的 `)` 为止（`\((.*?)\)(?=\s|$)`）。按原写法的贪婪匹配，`FAIL!  : T::f(row) 'x' returned FALSE. ()` 会把失败消息算进数据标签，同一行的 `PASS` 与 `FAIL!` 就无法合并。
- `XPASS` 与 `FAIL!` 一样算失败，`XFAIL` 算通过，`BLACKLIST` 算跳过。
- 套件清单取自构建目录中的 `CTestTestfile.cmake`：已注册但没有结果文件、或结果文件中没有测试行的套件列入 `noResult`；没有对应注册套件的结果文件也按路径列出。`initTestCase`／`cleanupTestCase` 不计入测试行，但它们的失败列入该套件的 `fixtureFailures`。每个套件记录结果文件的修改时间，便于发现旧结果。

验证：对审查时留下的 Release 全量结果运行（59 个结果文件，写入于 2026-10-18 22:34 前后），得到 59 个套件、2168 行通过、21 行失败（`native-frame` 20、`floating-panel` 1）、0 行跳过，与审查记录一致（`out/test-results/test-summary-review-baseline.json`）。0.2 的三层运行之后再次运行，得到 2189 行通过、0 行失败。

---

## 0.4 架构决定记录

**目标**：`docs/adr/` 中记录已经做出、但只散落在设计文档里的决定。

**实现**：新建以下文件，每个包含“背景、决定、后果、替代方案、日期”五节，内容取自现有设计文档，不新增决定：

| 文件 | 内容来源 |
| --- | --- |
| `docs/adr/0001-own-docking-implementation.md` | `engineering-design.md` §2：自研停靠，不用 KDDockWidgets |
| `docs/adr/0002-lua-as-cxx-with-gc-hook.md` | `third_party/lua/CMakeLists.txt` 注释、`lua-scripting-design.md` |
| `docs/adr/0003-lua-sandbox-scope.md` | `script_worker.cpp` `initialize()` 去掉的库与函数 |
| `docs/adr/0004-windows-toolchain.md` | 实际使用 MinGW GCC 15.1 构建 Release；MSVC 只用于诊断构建（见阶段 6.4）。同时修订 `engineering-design.md` §6.1 中“首选 MSVC”的表述，使两者一致 |

`docs/README.md` 的表格中加入 ADR 目录入口。

**状态**：已完成（提交标题 “docs: Record architecture decisions and split the preview log”）。实现与上面的差别：

- 除计划的 0001–0004 外，另记三项在本计划写成之后才定下、只散落在设计文档里的决定：0005 标题按钮命中（只有最大化／还原走原生命中）、0006 组件按需加载、0007 改用 Windows 系统通知。仍然不新增决定。
- 0003 按当前代码记录：没有打开 `coroutine` 与 `package`，因此也没有 `require`；设计文档中的受限 `require` 尚未实现，ADR 中写明。
- 0004 与 `engineering-design.md` §6.1 一起按实际改正：Qt MinGW 套件及其配套编译器、`build.ps1` 查找 Qt 的顺序、`mise.toml` 只提供任务入口。另在 `docs/adr/README.md` 写明编号、五节结构和“被取代”时的处理。
- 窗口系统设计、Lua 设计、界面规范、工程设计在对应位置链接到 ADR。

---

## 0.5 预览说明拆分

**目标**：`docs/development/window-system-preview.md` 只写当前状态。

**实现**：

1. 新建 `docs/development/verification-log/2026-10.md`，把预览说明中带日期的“修订”与“验证记录”小节原样移入（保持标题与正文不变，按日期倒序）。
2. 预览说明保留：开发环境、当前可试用功能（功能表，不带日期叙述）、快捷键表、试用步骤、“最近一次验证”一节（只写最新一次），以及指向日志的链接。
3. `docs/README.md` 增加日志入口。

**完成标准**：移动前后用 `git diff --stat` 确认总行数基本守恒；所有原有链接仍然有效（用 `grep -rn "window-system-preview.md#" docs` 检查锚点引用）。

**状态**：已完成（提交标题 “docs: Record architecture decisions and split the preview log”）。实际结果：

- 原文 587 行中两段带日期的连续区域（原第 71–294 行、第 330–572 行，共 467 行）原样移入 `verification-log/2026-10.md`，它们原本就按日期倒序排列；只把相对链接改为从新目录出发的路径。预览说明剩 156 行。
- 原“当前可试用的功能”下面实际是 2026-10-18 至 10-20 的修订叙述，现状只体现在 10-18 那一节里的表格和段落中。预览说明改写为一张按当前代码更新的功能表，不带日期；“最近一次验证”写 2026-10-20 的全量结果；“待完成与验证范围”增加仍未查明的三个问题。
- 没有文档用锚点链接预览说明；`code-review-2026-10.md` 中按标题文字引用的一处改指日志。实现计划 README 的“完成后”第 2 条改为写日志并更新预览说明的表格和最近一次验证。
- 用脚本检查了改动过的文档、ADR、经验文档和日志中的全部相对链接与锚点，均有效。
