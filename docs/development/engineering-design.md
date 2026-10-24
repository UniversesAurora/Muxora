# Muxora 工程、资源与依赖设计

关联：V0 设计文档第 2、13、16、17、18 节，以及自动化测试规范。本文规定工程要求；首个窗口系统预览的实际构建与验证情况见 [预览说明](window-system-preview.md)。

## 1. Windows 首发与平台边界

V0 默认只构建、打包和验收 Windows x64。最低 Windows 版本在选定 Qt、工具链及必要系统 API 后明确；当前 VS 风格与未来 Aero 主题都不自动表示支持 Windows 7。

macOS、Linux 是确定的后续目标，但 V0 不要求编译它们，也不把设计上的可移植性写成已经通过跨平台验证。

保持可移植的部分包括：会话目录、SSH 服务接口、终端适配接口、文件端点、传输调度、工作区逻辑布局、命令和配置结构。允许使用 Qt Core 的跨平台能力，不为了“纯 C++”重新实现字符串、事件和文件监听。

通用窗框、窗口命中与壳的 DPI 差异集中在 `modules/window-system/src/platform/windows`。Muxora 的凭据、agent、进程与其他业务平台差异留在 `src/platform/windows`。上层使用小型能力接口，不直接包含 Windows 头文件；平台类型不进入会话或文件公共接口。

未来分别在窗口模块与宿主增加对应平台实现。现在只定义确有 Windows 使用方的边界，不为未知平台创建大量空类。远程路径与本地路径分别建模，不能用 Windows 路径规则处理服务器路径。

## 2. C++ 与 Qt 编码规范

采用 C++20、Qt Widgets 和传统头文件／源文件。工程模块与 CMake target 按职责拆分；V0 不要求使用 C++20 Modules。

窗口系统是 `modules/window-system/` 中的独立公开 API 模块，可独立配置并用于其他宿主；`src/workspace_host/` 只承担 Muxora 接入。V0 采用自有窗口、停靠与拖拽实现，复用合适的 Qt 基础组件，不引入 KDDockWidgets（[ADR 0001](../adr/0001-own-docking-implementation.md)）；具体 Qt 控件不属于强制依赖。

### 2.1 可读性与依赖

- 一个类或模块有明确职责；窗口类不同时承担配置解析、SSH 认证、传输与终端状态。
- 接口使用清楚的 ID、状态、命令和结果，避免长串布尔参数、无语义字符串与隐式状态切换。
- 代码里的标识（命令 ID、分类、菜单路径、配置键、存储的名称）一律用英文，不拿界面文字当标识；要显示时映射到可翻译的文字（用户 2026-10-24 决定，翻译规则见 [源文本与覆盖](../design/settings-and-commands-design.md#83-翻译源文本与覆盖)）。
- 优先组合与直接依赖；少量存在实际替换需求的接口才使用多态。不为了扩展创建多层工厂或通用插件框架。
- 高层依赖接口，第三方适配实现接口；libssh、Ghostty、停靠库类型不暴露到不相关模块。
- 用返回值或明确的错误对象表达失败，包含足以诊断的上下文；不以日志替代错误处理。
- 注释解释理由、约束与第三方行为，不逐行复述代码。
- 新增公开操作同时提供统一命令入口、必要的键盘方式和有意义的自动化验证。
- Qt 布局使用逻辑像素；平台命中与坐标转换集中在平台适配层，不按主屏 DPR 处理所有屏幕（[High DPI](../design/window-system-design.md#10-high-dpi-与清晰度)）。
- 统一格式、命名和静态分析规则。头文件自包含，模块只公开需要的头文件。

采用 target 级别的 include、编译选项和 PRIVATE／PUBLIC 依赖，不向整个工程开放所有内部目录。共享构建配置放在 `CMakePresets.json`，个人路径放在不提交的 `CMakeUserPresets.json`。[CMake 构建系统](https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html)、[CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)

### 2.2 所有权与生命周期

普通 C++ 对象优先值语义和 RAII；独占资源使用 `unique_ptr`，确有共享生命周期时才使用 `shared_ptr`，观察共享对象使用弱引用。不能用 `shared_ptr` 掩盖所有权，也不能保留引用环。[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource)

Qt 对象采用一套明确释放责任：

- parent 拥有的 QObject 由 parent 生命周期管理，不再添加独立的智能指针释放者。
- 无 parent 的对象有明确所有者和销毁位置。
- 非拥有 QObject 引用必要时使用 `QPointer`；它不能替代跨线程同步。
- 将信号连接绑定到合适的 QObject context，减少页面销毁后的回调；异步任务仍需要自己的取消和结果失效机制。

QObject 的 parent 所有权及线程规则遵循 Qt 文档。[QObject](https://doc.qt.io/qt-6/qobject.html)、[QPointer](https://doc.qt.io/qt-6/qpointer.html)

SSH session、channel、SFTP handle、Ghostty terminal、render state、文件、socket 和系统句柄分别封装为不可复制的 RAII 资源。谁创建谁使用对应释放接口；跨 DLL 不混用 `new/delete`、`malloc/free` 或不同分配器。

关闭实例按明确顺序进行：禁止新请求、取消任务、让旧回调失效、清理通道与状态、释放视图及最后不再被引用的连接。跨窗口移动仅转移归属，不能走“关闭旧实例再打开新实例”的路径。

两种窗口共用 DockSurface；WindowFamily 是逻辑归属，不能交给 Qt parent 或原生 owner 级联销毁。完整根关闭先汇总全族关闭许可，再提交资源关闭；升级／降级、单标签及整窗移动走可回滚布局事务，成功后才清理源空壳。轻量窗口不创建隐藏的完整工具条控件，不复制配置服务或终端缓存。[双窗口与拖拽契约](../design/floating-window-design.md)

### 2.3 线程与异步

GUI 线程负责交互与必要绘制。网络、目录读取、大量传输、长搜索与配置扫描在受控执行器中进行；不能调用阻塞等待、认证或完整目录扫描卡住 GUI。

每个第三方连接或引擎实例有明确执行线程／串行访问规则。多个标签或窗格可以共享执行器，不要求每个窗格永久创建一个线程。任务池、待执行请求和消息队列有上限。

QObject 在所属线程使用和销毁；worker 停止、对象清理与事件循环退出有明确顺序。不能先停事件循环，再期待普通 `deleteLater()` 队列自动完成释放。[Threads and QObjects](https://doc.qt.io/qt-6/threads-qobject.html)、[deleteLater](https://doc.qt.io/qt-6/qobject.html#deleteLater)

异步结果携带实例 ID、请求代数或取消状态。标签关闭、导航更新或连接更换后，旧结果丢弃；不能通过裸 QWidget 指针回写。关闭和退出的等待有边界，不使用强杀线程来完成普通资源清理。

### 2.4 当前配置格式

当前开发阶段只维护现行布局、设置和工具状态格式，不保留旧版本的兼容读取、字段转换、数据迁移或专用兼容 API。格式调整时同步更新读写双方、调用方、测试和文档；后续计划也不预留旧格式转发或双写周期。

当前格式声明为可选的字段仍使用各自默认值；损坏数据的类型、范围、引用和原子恢复校验必须保留。运行期间添加新控件、恢复本次拖动或取消固定的位置，以及多个现存窗口合并较新的设置，都属于当前行为，不是版本迁移。第三方协议、操作系统和工具链的兼容要求不受此约束影响。

## 3. 内存节约是实现约束

必须防止泄漏，也必须限制仍被引用却已无使用价值的数据。可重建缓存不应无限保留，关闭的页面不应因订阅或全局注册表继续存活。

### 3.1 数据只保存必要副本

- 一个终端核心持有一份权威历史。Qt 显示层不再保存完整第二份终端正文或历史。
- render state 以可见区域为主，必要时仅有少量预取；按 dirty rows 更新，不每帧复制全部历史。
- 宿主只保留有界的待显示状态，快速输出时合并绘制通知。不能无限排队 queued signal 或帧快照。
- 字形整形与图片缓存按字节预算淘汰，不按每个历史行 ID 永久累计。
- 文件列表采用 model/view、批量更新和按需加载，避免为每个文件创建一个 QWidget；多个呈现共享可共享的数据，不重复持有完整目录树。

Ghostty 的借用引用只在规定生命周期内使用，不能把更新后失效的 cell／row 指针投递给 UI。显示状态更新按上游的两阶段接口缩短锁占用。[Ghostty render API](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt/render.h)

Qt 的增量目录模型模式可作为文件列表基础。[Fetch More 示例](https://doc.qt.io/qt-6/qtwidgets-itemviews-fetchmore-example.html)

### 3.2 分项预算与全局预算

| 资源 | 必须采用的约束 |
| --- | --- |
| 终端历史 | 每实例的行数与字节限额，加所有终端的全局预算；按库能力淘汰最旧历史。 |
| 内嵌图片 | 输入字节、解码尺寸、单图和总缓存分别限制；解码前检查预算。 |
| 字形与行缓存 | 有界缓存、淘汰策略和字体／DPI 变化失效规则。 |
| 目录数据 | 增量读取、缓存上限、旧查询取消；不能一次缓存所有子目录。 |
| SSH 输入输出 | 有界缓冲及背压；协议字节不可像显示通知一样随意丢弃。 |
| 传输 | 固定大小流水线缓冲、并发限额；内存不随文件总大小增长。 |
| UI 消息 | 合并无效重绘、替换迟到目录结果；队列容量受控。 |
| 日志 | 大小与轮转限制；高频字节日志默认关闭。 |
| 辅助进程 | 按需启动与结束，记录其内存及句柄；避免闲置进程常驻。 |

数值上限在首个 Windows 原型中根据实测制定并保存默认方案，不能把“无限制”作为临时默认。分项设置可调整，但整体仍有预算策略；多个窗格不应简单获得无限叠加的缓存。

Ghostty 当前历史限制按 page 裁剪，属于近似限额，不等于整个进程的硬内存上限。内存统计可能遍历 page，不在每次网络读入后扫描；可用的历史压缩在空闲时分步处理。[历史与内存接口](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt/terminal.h)

### 3.3 隐藏、收纳与关闭

自动隐藏、非活动标签和被最大化窗格遮住的内容停止无效绘制，降低快照生成与动画频率。不能仅因视图隐藏而停止协议处理；资源达到高水位时允许施加背压，保留必要控制与回复通路，低水位后恢复，不丢弃协议字节。

回收可重建的绘制与目录缓存，保留有价值且在预算内的近期缓存，平衡切换速度与占用。未使用的功能标签按需创建视图。关闭后移除订阅、计时器、注册表引用和缓存，连接引用归零时关闭对应资源。

### 3.4 不用的组件不加载

功能组件在第一次真正需要时才加载，没用到就不占内存（决定见 [ADR 0006](../adr/0006-load-components-on-demand.md)）：

- **默认按需**：Lua 运行时（没有脚本时不创建线程和解释器）、内置编辑器（Scintilla 编译为插件 DLL，第一次打开编辑器时加载）、SFTP 辅助程序、X11 显示服务、语言服务进程、结构描述（JSON Schema）等派生数据，都在第一次使用时创建。
- **可以常驻的例外**：使用非常频繁、加载代价会影响交互的部分，例如窗口系统本身、终端核心与渲染（打开终端就要用）、命令注册表。
- **实现方式**：较大的可选组件编译为单独的 DLL，用 `QPluginLoader` 在首次使用时加载，应用只依赖一个很小的接口头文件；较小的组件用延迟构造（第一次调用时创建对象）。
- **用完就销毁**：界面控件、对话框、编辑器、面板、缓存、线程、辅助进程等一切能销毁的对象，在关闭或不再使用时销毁并释放内存，不为“下次可能还用”而保留（有明确收益、并在预算内的缓存除外）。
- **卸载**：只有插件 DLL 本身例外：含有 Qt 类型或静态数据的 DLL 加载后不在运行期间卸载（卸载后被访问会崩溃），但它创建的所有对象仍按上一条销毁，留在进程里的只有 DLL 的代码。能安全释放的资源（例如 Lua 解释器、语言服务进程、辅助程序）在空闲一段时间且没有使用者时释放。
- **验证**：每个按需组件都有一条测试，确认普通启动和常用操作之后它没有被加载（进程模块列表、线程名或对象计数），以及首次使用的加载耗时记录在预览说明中。

## 4. 响应速度与性能验收

重绘合并到显示节奏，批量更新数据。拖拽预览只调整预览几何；移动鼠标时不重新认证、不重建 PaneTree 或字体缓存。窗口 resize 合并高频尺寸请求，结束后的最终尺寸可靠发送。

拖拽唤起目标使用同进程窗口注册表和单个待定候选，按需进行命中与计时，不常驻扫描桌面。原生移动循环、捕获、层叠及 DPI 转换集中于 WindowSystem 平台适配；抬前目标不能抢终端焦点，取消后释放计时器、预览和订阅。整窗拖拽不复制历史或保留无限全尺寸截图。

大量输出采用受控处理批次，给输入、取消和其他连接留出执行机会。文件传输使用背压与公平调度；如果共享 transport 影响交互，通过实测决定独立传输连接。

长任务有可取消状态，超过正常处理时间时立即呈现忙碌状态。热路径避免全量日志、重复文本整形、同步磁盘访问和跨线程阻塞调用。

首个 Release 原型建立以下测量：

| 场景 | 记录内容 |
| --- | --- |
| 冷启动、再次启动 | 到窗口可交互的耗时、峰值、加载的组件。 |
| 标签切换、组与窗格聚焦 | 从本地输入事件到焦点／画面完成的延迟，p50、p95 与最差值。 |
| 拖拽、调整分隔条 | 帧时间、掉帧与事件积压。 |
| 单终端与多终端持续输出 | 输入处理延迟、CPU、快照／队列占用。 |
| 大目录、传输并行 | 首批可用结果、取消响应、内存峰值及终端交互。 |
| 长期与反复开关 | native heap、句柄、线程、对象与各缓存的趋势。 |

本地操作初始目标为 p95 不超过 50 ms，普通拖动按 60 Hz 交互评估；这是原型调优目标，需记录参考机器、数据量与测量方法。远端输出到达后的本地处理延迟与 SSH 网络往返分别统计，不把服务器或网络等待归成 UI 绘制性能。

内存以 private bytes、native heap、组件预算和辅助进程总量为主要证据，工作集作为补充。系统缓存、分配器保留或 ASan 开销不能直接认定泄漏；已关闭的资源持续增长、分配未释放或句柄累积必须定位。

## 5. Windows 检查与发布条件

自有 C++ 代码启用严格告警、格式检查和选定的静态分析规则；可控代码中的告警处理后再放行。第三方告警单独管理，不能为其噪声关闭整个项目的检查。

建立普通 Debug、Release 与 ASan 诊断构建。MSVC ASan 主要检测越界、use-after-free、double-free 等问题，不能代替完整泄漏检测，也不保证覆盖未经相应构建的第三方内部。[MSVC AddressSanitizer](https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170)

使用 Visual Studio Memory Usage 快照、适用范围内的 CRT debug heap、资源计数和循环场景共同检查。性能与生产内存数据使用 Release，诊断版本单独记录。[Memory Usage](https://learn.microsoft.com/en-us/visualstudio/profiling/memory-usage?view=vs-2022)、[CRT 泄漏检查](https://learn.microsoft.com/en-us/cpp/c-runtime-library/find-memory-leaks-using-the-crt-library?view=msvc-170)

重复连接／关闭、分割／关闭窗格、边栏展开／固定、跨窗口移动、传输取消、配置刷新与退出，检查资源回落及长期趋势。测试重点是生命周期、真正的协议往返和错误场景，不为每个 getter 写重复测试。

**已确认的内存泄漏、悬空访问、持续无界增长，以及普通操作明显阻塞 GUI，均为发布阻断项。** 需要有可复核的测量与检查结果，不以“采用 RAII”代替验证。

### 5.1 完整自动化测试与失败处理

必须从初始原型建立完整链路：需求与断言、构建／静态检查、单元及模型、独立 WindowSystem API、组件适配、真实协议和 Windows 桌面、DPI、诊断／压力、Release 性能，以及干净环境下最终发行包验收。使用 CTest 与 Qt Test 组织，本机与 CI 共用入口，随功能增加对应有效用例。

**测试失败必须定位并修复问题，不得自行删除／跳过用例、修改预期、放宽阈值、缩减负载、更新失败截图基线或关闭诊断来获得通过。** 确证用例本身有错时，保留依据与审阅记录再修正；涉及标准降低必须由用户明确确认。未执行、跳过、环境阻塞和不稳定分别报告，不算通过。

缺陷修复保留复现用例。依赖升级通过受影响完整回归、性能／内存对比与打包检查，不能只以成功编译放行。用例矩阵、CI 分层、隔离环境、结果证据和发布条件由 [自动化测试与质量要求](testing-design.md) 统一规定。

## 6. 源码、二进制与链接方式

依赖从源码取得还是使用预编译产物，与最终静态还是动态链接，是两个独立选择。

| 组件 | 来源及构建建议 | Windows V0 运行形式 |
| --- | --- | --- |
| Qt | 官方、匹配编译器与架构的预编译 SDK；只有实际定制需要才自己编译。 | 动态库及需要的平台插件。 |
| libssh 与加密依赖 | 固定稳定版本，经 vcpkg manifest 与 baseline 构建，复用可信缓存。 | 优先动态库，实际依赖与 CRT 一致验收。 |
| WindowSystem | 本仓库独立模块，自有布局及停靠实现，MIT 方向。 | 内部库或按实际复用需求分发；V0 不依赖 KDDockWidgets。 |
| libghostty-vt | 固定官方源码 commit、所需 Zig 版本和选项，利用官方 CMake wrapper 构建。 | V0 优先 DLL 与匹配的头文件／import library。 |
| OpenSSH | 验收过的官方 Windows 构建或选定成熟发行组合；固定版本与校验。 | 独立原生程序，由后端管理。 |
| 可选 SFTP 辅助程序 | 如启用，以固定开源库和工具链构建自己的小型产物。 | 按需启动的程序，不要求用户安装构建工具。 |
| X server | 首先检测已安装的验收组合；可选组件包另行维护。 | 独立组件。 |

Qt SDK、所有 Qt 插件及选用的 Qt 第三方库必须使用同一套兼容 Qt。通过依赖管理器增加 Qt 相关组件时核对传递依赖，避免意外再编译或混入第二套 Qt。

libssh 选定版本必须存在于实际使用的 registry／baseline。需要安全修复而 registry 未包含时，采用有记录的小型 overlay port；不能假设写出版本号就能下载正确源码。[vcpkg 版本管理](https://learn.microsoft.com/en-us/vcpkg/users/versioning)

Ghostty 官方 CMake wrapper 调用 Zig，并支持 Windows DLL／import library 和静态目标。Windows 静态链接还需处理其 SIMD 等依赖，V0 先验证 DLL 路线，不为集成方便默认关闭性能功能。Zig 是开发工具，运行软件的用户不需要安装。[Ghostty CMake](https://github.com/ghostty-org/ghostty/blob/main/CMakeLists.txt)

源码首次编译后的产物可以二进制缓存复用。缓存键包含源码版本、构建选项、编译器／工具链、架构、CRT、Debug／Release 和有关依赖；产物附带出处与校验。这样保留可复现源码基础，同时避免每个开发者反复完整编译。[vcpkg 二进制缓存](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching)

动态库便于明确构建和分发边界，但不自动意味着省内存或可任意替换；资源占用仍以实际测量为准。

### 6.1 Qt SDK 的安装、版本绑定与升级

开发机使用官方 Qt Online Installer 安装 Qt 6.12 的 **MSVC 2022 64-bit** 套件（`msvc2022_64`），并按需安装 Qt 基础模块和测试组件；编译器来自本机 Visual Studio 2022（C++ 桌面开发工作负载），Qt 不提供 MSVC 编译器。CMake 与 Ninja 取自 Qt 安装目录下的 `Tools`。Release 预览和全部自动化测试都用这套工具链，选择原因与约束见 [ADR 0009](../adr/0009-msvc-toolchain.md)。Qt Creator 为可选开发工具。官方安装器及 Maintenance Tool 当前需要 Qt 账号，开源许可组件可以免费取得。

Qt SDK 放在项目外的版本化目录，例如仓库旁边的 `..\sdk\Qt\<版本>\msvc2022_64`，多个项目共用；不把 SDK 提交进仓库。初版无需从源码编译 Qt。

`tools/toolchain.ps1` 统一处理套件与编译器，`build.ps1`、`verify.ps1`、`bench.ps1` 共用：按顺序查找 Qt 套件（`-QtRoot` 参数、环境变量 `QT_ROOT`、仓库旁边 `..\sdk\Qt` 中最新版本的 MSVC 套件，最后是不提交的 `.local/sdk.json`），从套件的 `mkspecs/qconfig.pri` 确认它是 MSVC 套件，其他套件直接报错。脚本通过 `vswhere` 找到装有 x64 C++ 工具的最新 Visual Studio，载入 `vcvars64` 环境，并要求 `cl` 的版本不低于套件记录的 `QT_MSVC_*_VERSION`。构建目录的 CMake 缓存记录的编译器与本次不同，或记录的头文件依赖前缀与当前 `cl` 的输出不同时，脚本先清空该构建目录；配置和构建期间控制台输入和输出代码页固定为 UTF-8（见 [构建经验](lessons/build-toolchain.md#本地化的-cl-让头文件依赖随控制台代码页丢失)）。开发入口就是 `tools/` 下的脚本，不另设任务运行器；本机绝对路径放在不提交的 `.local/sdk.json`、环境变量或 `CMakeUserPresets.json`。不依赖机器全局 PATH 恰好找到某个版本。[Qt CMake 路径](https://doc.qt.io/qt-6/cmake-making-qt-available.html)

所有目标在 MSVC 下加 `/utf-8`：源码是 UTF-8，MSVC 默认按系统代码页读源文件，中文字符串会悄悄变成乱码而不一定报警告。各目录的 CMakeLists 末尾为本目录内实际编译源码的目标统一添加，新增目标自动覆盖。

官方 Maintenance Tool 提供添加／删除组件、检查更新、更新已安装包以及安装新 Qt 版本的图形和命令行入口。新 Qt 版本优先并行安装，使用独立构建目录验证，全部相关测试通过后再切换项目绑定；保留旧版本用于回退。升级 SDK 不代表项目自动改用新版。[Qt 安装与维护](https://doc.qt.io/qt-6/get-and-install-qt.html)、[命令行管理](https://doc.qt.io/qt-6/get-and-install-qt-cli.html)

社区 aqtinstall 可以下载官方预编译 SDK，但截至本稿日期，稳定版对 Windows Qt 6.11 之后仓库布局有兼容问题，因此不作为当前默认路线。以后评估替换安装工具时，同样固定工具版本并验证实际 Qt 包，不跟踪未经验证的开发分支。[兼容问题记录](https://github.com/miurahr/aqtinstall/issues/1007)

### 6.2 自有代码与第三方许可

自有 Muxora 和 WindowSystem 采用 MIT 方向。Qt 选择 LGPL 可用模块，libssh、Ghostty 等按各自许可维护声明、对应源码和发行条件；Windows 优先动态链接 LGPL 库并满足替换／重新链接要求。可收费发行，不把依赖许可改标为 MIT。[Qt LGPL 义务](https://www.qt.io/development/open-source-lgpl-obligations)

V0 不链接 GPL 版 KDDockWidgets 或其他 GPL-only 库。X server 等独立可选程序与其组件包另行核对许可和源码义务。详细决定见 V0 文档第 16 节。

## 7. 可升级的第三方边界

固定版本用于保证每次发行可复现，后续主动升级维护中的组件。升级按小步执行，避免一次同时改变 Qt、停靠、终端和 SSH，导致问题难以定位。

- 终端供应方类型与回调集中在 `terminal/ghostty`，向产品层提供稳定的项目接口。
- libssh 与 OpenSSH 差异集中在各自提供者，文件层使用统一端点。
- 窗口系统公共接口独立版本化，自有停靠实现在 `modules/window-system/src/docking`；未来复用其他组件时也将供应方类型集中在此，不进入公开 API。
- 原始上游源码保持可追溯；必要补丁独立保存来源、原因、测试与移除条件。
- 构建选项、许可证与组件版本进入发行清单，可定位实际使用的实现。

Ghostty 对外 API 尚未稳定，每次升级都按以下流程处理：[API 状态](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt.h)

```text
核对上游变更与安全修复
→ 更新固定提交、工具链和构建选项
→ 重建供应方库与适配层
→ 检查输入、查询回复、字体、TUI、历史及生命周期
→ 比较资源与响应基线
→ 完整 Windows 打包与启动验收
→ 随应用发行，必要时回退整个已验收版本
```

“能更新”指可重复执行这一流程，不承诺运行中热换 DLL 或让用户任意换一个版本。上游 API／ABI 改变时重建应用，避免活动终端句柄跨不同库版本使用。

应用配置和工作区布局使用自身的当前格式，遵守 [配置格式约束](#24-当前配置格式)。供应方序列化格式不作为应用公共 API；适配数据必须与当前逻辑结构一致。

JSONC 用户配置、独立会话文件、快捷键规则和机器状态采用分别维护的 schema 与服务，保留用户注释和外部编辑；运行状态原子写入且合并高频变化。命令及按键解析属于通用窗口能力，业务设置服务由宿主注入；本地 Normal／Visual 仅协调成熟终端核心的选择与跟踪能力。具体契约见 [设置与命令](../design/settings-and-commands-design.md) 和 [终端模式](../design/terminal-mark-mode-design.md)。

## 8. 推荐目录组织

以下是完整产品的目录规划。当前按窗口系统与 Muxora 应用预览所需建立目录，远程业务和第三方集成目录随实际实现创建，不预置空模块：

```text
Muxora/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── vcpkg-configuration.json
├── cmake/
│   ├── DependencyVersions.cmake
│   ├── dependencies/          # 固定依赖的 CMake 集成
│   ├── policies/              # 编译、告警与诊断配置
│   └── deployment/            # 平台部署规则
├── modules/
│   └── window-system/         # 通用窗口系统，独立公开 API 与构建
│       ├── CMakeLists.txt
│       ├── include/window_system/
│       ├── src/               # 模型、命令、壳、窗格、样式与后端
│       ├── themes/
│       ├── tests/
│       │   └── fixtures/external-host/ # 独立复用验证夹具
│       ├── cmake/
│       └── docs/
├── src/
│   ├── app/                   # 启动与依赖组装
│   ├── core/                  # 少量 ID、结果、取消和预算基础
│   ├── settings/              # schema、JSONC、用户配置与独立状态存储
│   ├── sessions/              # 来源、目录与别名附加设置
│   ├── ssh/                   # 连接服务及后端接口
│   │   ├── libssh/
│   │   └── openssh/
│   ├── terminal/
│   │   ├── ghostty/           # 外部 API、句柄与回调适配
│   │   ├── interaction/       # Normal／Visual、浏览、选择与搜索协调
│   │   └── view/              # Qt 显示、字体与输入法
│   ├── files/                 # 端点、浏览、传输与对应视图
│   ├── workspace_host/        # 业务内容、命令、状态与恢复回调
│   └── platform/
│       ├── api/               # 确有使用方的平台边界
│       └── windows/           # 当前唯一平台实现
├── resources/                 # 图标、配色、默认键位
├── tests/                     # 单元、集成、真实端到端、fixtures 与测试辅助
├── benchmarks/                # 可重复的资源与响应测量
├── ci/                        # 检查编排及可复现测试环境说明
├── docs/
│   ├── README.md              # 文档入口
│   ├── design/                # 产品、界面与接口设计，每个主题一个文件
│   ├── development/           # 开发规范与实际操作说明
│   │   ├── handoff.md         # 助手之间的交接记录
│   │   ├── documentation.md   # 文档维护规范：信息的权威位置与更新对照
│   │   ├── window-system-preview.md   # 当前状态与最近一次验证
│   │   ├── verification-log/  # 按月的修订与验证记录
│   │   └── lessons/           # 开发经验
│   ├── roadmap/
│   │   ├── implementation/    # 分阶段实现计划与推荐执行顺序
│   │   └── review-2026-10/    # 整体审查快照，不再更新
│   └── adr/                   # 有长期影响的决定及理由
├── third_party/
│   ├── overlays/              # 必要 vcpkg overlay
│   └── patches/               # 必要补丁及说明
├── packaging/windows/
├── tools/                     # 开发／验证脚本，Windows 优先 pwsh
└── out/                       # 忽略提交的构建、下载与缓存产物
```

按功能组织代码，功能内区分模型、适配与视图；不会把所有界面代码堆进一个巨大窗口，也不会建立堆放任意内容的 `utils` 目录。

窗口模块的目录不是只做分类：它必须有可独立构建的目标与外部宿主测试夹具，通过公开 API 使用，不能依赖根工程的 SSH／Ghostty 依赖和 Muxora 配置。该边界详见 [通用窗口系统设计](../design/window-system-design.md)。

顶层 CMake 只负责配置和组装。模块 target 表达实际依赖，`core` 不依赖 UI，供应方 include 尽量 PRIVATE。模块边界不要求每个 target 都成为 DLL，可以使用普通内部库；部署 DLL 数量由需要决定。

WindowSystem 的公共 target 独立于 Muxora 的业务 core；宿主单向依赖它。测试夹具与安装选项在模块内管理，嵌入根工程时不更改全局设置。用户试用的是 `src/app/` 中的 Muxora 应用，后续产品继续使用该入口与 `src/workspace_host/` 接入代码。

第三方完整源码放在构建缓存或可校验的来源中，不将 SDK 与大仓库直接复制进项目。需要离线构建时提供明确的源码／缓存包，仍保留出处和许可。

## 9. Windows 分发

使用 Qt 的部署工具收集需要的 Qt 运行文件；额外 SSH、Ghostty、加密与辅助程序由项目清单补齐，不能假设工具会收集所有第三方依赖。[Qt Windows 部署](https://doc.qt.io/qt-6/windows-deployment.html)

发行包包含匹配的组件、平台插件、必要运行库、许可证和版本清单，不包含编译器、Zig、Go、头文件或无用的 Debug 运行库。调试符号另行保留以便定位问题。

在干净 Windows 环境中启动验收，防止开发机器 PATH 或已安装 SDK 掩盖漏打包。X11 可选包独立验证，普通 SSH／文件管理不依赖用户安装完整开发环境。

测试最终生成的发行文件而不是开发目录中的可执行文件，保存产物校验与测试证据。打包成功不代表功能通过；完整发布检查未通过时不得发行。
