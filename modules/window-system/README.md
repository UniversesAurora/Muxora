# WindowSystem

可独立构建的 Qt Widgets 工作区模块。Muxora 通过公开 API 使用它；其他项目也可通过 CMake package 接入，模块不依赖 SSH、终端、文件业务或 Muxora 配置。

当前提供两种窗口壳、逻辑窗口族、中央标签组、标签内窗格、四边自动隐藏／固定、单标签与整窗移动、命令面板、快捷键入口和布局快照。首轮功能与待完善项见 [预览说明](../../docs/development/window-system-preview.md)。

## 构建边界

| Target | 职责 |
| --- | --- |
| `WindowSystem::Model` | 布局模型、稳定 ID、窗口族、事务和严格 JSON 校验；只依赖 Qt Core。 |
| `WindowSystem::Widgets` | 原生窗口、停靠视图、窗格、命令、主题与平台适配；依赖 Model 和 Qt Widgets。 |

可通过 `add_subdirectory()` 嵌入，也可安装后使用 `find_package(WindowSystem CONFIG REQUIRED)`。独立模块构建不探测 Muxora 或远程业务依赖；安装后接入由 `tests/fixtures/external-host/` 验证，不额外交付示例产品。

## 公开 API

入口为 [WindowManager](include/window_system/window_manager.hpp)，独立模型见 [LayoutModel](include/window_system/layout_model.hpp)。实际宿主组装见 [Muxora 启动入口](../../src/app/main.cpp) 与 [内容接入](../../src/workspace_host/placeholder_content.cpp)。

成功添加内容后模块接管 QWidget parent；添加失败保持调用者所有权。观察内容用稳定 ID 或 QPointer。移动、拆出及窗口形态转换保留原内容实例；关闭许可由宿主回调控制，完整根关闭先检查整个窗口族。

`setPaneTopBar()` / `setPaneBottomBar()` 为 Pane 设置专属附加栏，焦点变化时显示在 Tab 的共用上下位置；`setTabTopBar()` / `setTabBottomBar()` 作为默认栏。`pinTab()` 只处理当前标签，`pinEdge()` 明确处理该侧全部标签。标签图标可通过 `TabOptions::showIcon` 或 `setTabIconVisible()` 控制。

拖拽使用真实轻量窗口，达到阈值即移走内容并清理空组；Esc 通过模型快照恢复，保留控件实例。标签条按释放位置插入，内容区不隐式合并。详见 [交互修订](../../docs/design/window-interaction-design.md)。

菜单、命令面板和快捷键共用 [CommandRegistry](include/window_system/command_registry.hpp)。宿主可以注册自己的操作；主题使用 [Theme](include/window_system/theme.hpp) 令牌，默认不添加应用 logo。布局快照由宿主选择保存位置，模块不写 Muxora 用户文件。

当前 API 为 0.1 初版，尚在交互迭代阶段，不承诺稳定二进制 ABI。完整 JSONC 用户键位、条件表达式和后续扩展仍按设计逐步接入。

## 规范与验证

- [窗口系统设计](../../docs/design/window-system-design.md)
- [工作区与导航](../../docs/design/workspace-design.md)
- [双窗口与拖拽](../../docs/design/floating-window-design.md)
- [测试要求](../../docs/development/testing-design.md)

运行 `pwsh tools/verify.ps1` 验证 Muxora、独立模块、安装后的外部接入及部署启动。测试失效须修复实现；不自行降低标准。
