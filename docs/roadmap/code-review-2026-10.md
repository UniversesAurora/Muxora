# 代码审查记录（2026-10）

审查基线：提交 `89ef8d0`。行号指该提交或本轮修改后的位置，随代码变化可能偏移。

严重程度：

- **高**：在目标负载（数百至上千个标签／会话）下会成为瓶颈，或结构决定了后续必须返工。
- **中**：可测到的浪费或不一致，局部修改即可解决。
- **低**：可维护性或一致性问题。

“状态”只写实际情况：已修复（有验证）、待处理、需测量（尚无数据，不能下结论）。

## 总体判断

窗口模型（`LayoutModel`）用不可变树和事务候选副本保证原子性，`validate()` 覆盖得很完整；重入、宿主回调销毁管理器、跨窗口移动保持内容身份这些难点都有防护和测试。Lua 运行时的隔离、预算和代次失效也做得扎实。本轮没有发现确定的内存泄漏：所有 `new` 都有 Qt parent 或明确的 `delete`/`deleteLater`，回调普遍用 `QPointer` 检查存活。已有的 `memory-lifecycle`、`lua-lifecycle` 夹具也在限定负载下检查了对象和句柄回落。

主要问题在规模：当前实现按“几十个标签”的规模写成，很多路径是“整体重建”或“全量扫描”。功能正确，但离“成百上千个会话”的目标有结构差距。这些问题集中在下表的 R1–R6。

## 发现

| ID | 级别 | 问题 | 位置 | 状态 |
| --- | --- | --- | --- | --- |
| R1 | 高 | 任何布局变化都调用 `refresh()`：所有内容控件改回存储控件并隐藏，所有 `TabContent` 删除并重建 `QSplitter`／`PaneHostFrame`，所有窗口的 `DockSurface` 整棵重建 | `window_manager.cpp` `refresh()`；`tab_content.cpp` `rebuild()`；`dock_surface.cpp` `rebuild()` | 待处理，见[性能 §1](performance-plan.md#1-布局变化改为增量调和) |
| R2 | 高 | 每个标签不论是否可见都常驻一套视图（`TabContent`、分割器、窗格框）；重设 parent 时 QSS 重新 polish | 同上；`theme.cpp` `applyTheme()` | 待处理，见[性能 §2](performance-plan.md#2-非活动标签的视图按需创建) |
| R3 | 高 | 模型查找是线性扫描：`groupForTab()` 遍历所有组的标签列表，`tabForPane()` 遍历所有标签的窗格树，`familyHasTabs()` 对每个窗口生成完整标签列表 | `layout_model.cpp` 约 286–339 行 | 待处理，见[性能 §3](performance-plan.md#3-布局模型的查找与事务成本) |
| R4 | 高 | 拖动分隔条时每个 `splitterMoved` 都执行一次模型事务：复制模型、同步固定树、`validate()` 全量校验；随后在所有标签视图里 `findChildren<QSplitter*>()` 找对应分割器 | `window_commands.cpp` `layout.setRatio`；`layout_model.cpp` `transact()` | 待处理 |
| R5 | 中 | `addTabToNewGroup()` 用两次 `LayoutModel::toJson()` 比较判断回调是否改动了模型；`requestCloseAllWindows()` 用 `saveLayout()` 比较。已有 `revision()`，整份序列化是多余成本 | `window_manager.cpp` 约 178、410 行 | 待处理 |
| R6 | 中 | 主题通过整段样式表（QSS）作用在每个顶层窗口。QSS 让每个子控件走 `QStyleSheetStyle`，内容控件跨窗口移动时重新解析，绘制也慢于原生 style | `theme.cpp` `applyTheme()` | 待处理，见[性能 §4](performance-plan.md#4-主题从样式表转向调色板与-proxystyle) |
| R7 | 中 | 键盘分发：每次 `KeyPress` 和 `ShortcutOverride` 都复制整个命令目录（含每条命令的 `std::function`）再逐条比较。终端输入时每个按键都要走两次 | `window_manager.cpp` `eventFilter()` | **已修复**（本轮） |
| R8 | 中 | 焦点进入和右键菜单时，按“遍历全部标签 × 生成窗格列表”查找窗格所属标签 | `window_manager.cpp` `eventFilter()` | **已修复**（本轮），改用 `tabForPane()`；R3 解决后进一步变成常数时间 |
| R9 | 中 | 布局保存在 GUI 线程同步写缩进 JSON，变化后 700 ms 触发。布局小时无感，标签上千时 JSON 体积和写盘时间需测量 | `src/app/main.cpp` `save` | 需测量 |
| R10 | 中 | 窗口行为设置（新标签位置、拖出形态等）随布局文件保存，而界面语言存在 `User/settings.json`。同为用户偏好却分两处，重置布局会连带丢设置 | `layout_persistence.cpp`；`user_preferences.cpp` | 待处理，见[交互 §4](window-system-ux-plan.md#4-设置界面与存储) |
| R11 | 中 | 拖拽指示、标签色板等处有硬编码颜色，深色主题会出现浅色块 | `drag_controller.cpp` 约 60–92 行；`tab_appearance.cpp` 约 60–101 行 | 待处理，见[交互 §1](window-system-ux-plan.md#1-主题深色与黑色) |
| R12 | 低 | `CommandRegistry::find()` 线性查找；`find()` 返回的指针在 `add()`/`remove()` 后失效，调用方需注意不要跨回调保存 | `command_registry.cpp` | 待处理（命令数到几千时再改为哈希索引） |
| R13 | 低 | `WindowManager` 公开 API 约 150 个方法，`window_manager.cpp`、`command_strip.cpp`、`layout_model.cpp` 都超过 1000 行，与仓库“不用巨大窗口类”的规则有距离 | 公开头文件 | 待处理，见[工程 §2](engineering-plan.md#2-代码组织) |
| R14 | 低 | 应用目标 `muxora` 和应用测试没有开启 `-Wall -Wextra`，模块目标已开启 | `src/app/CMakeLists.txt` | 待处理；本轮按要求不改 Release 默认编译选项 |
| R15 | 低 | 约 50 个测试可执行文件各自静态链接 WindowSystem，链接时间和磁盘占用随套件数线性增长 | `modules/window-system/tests/CMakeLists.txt` | 待处理，见[工程 §3](engineering-plan.md#3-测试) |
| R16 | 中 | 侧栏隐藏期间发生重建，再显示侧栏时，收纳条要等下一轮事件循环才真正显示；紧随其后的临时面板定位和固定分区尺寸计算看不到收纳条。`sidebar-presentation` 套件 8 行因此稳定失败，上一提交的定向测试没有覆盖该套件 | `dock_surface.cpp` `updateRailLayout()` | **已修复**（本轮） |

### 关于内存泄漏

没有找到确定的泄漏。需要留意的点：

- `refresh()` 对被移除的内容使用 `deleteLater()`。在没有事件循环的同步路径（例如宿主在退出前批量关闭）里，这些对象要等下一轮事件循环才释放；`WindowManager` 析构时已处理 `geometryUpdateDepth` 的情况。行为正确，但长循环里连续关闭大量标签时峰值会偏高，建议在 [性能 §6](performance-plan.md#6-测量与预算) 的循环测试中记录。
- `TabContent::setTopBar()` 用 `delete` 释放旧栏，而 `setPaneBar()` 用 `deleteLater()`。两者在各自调用上下文中都安全，但规则不统一，后续重构时应统一为一种并写明理由。
- Lua 侧的回调、计时器、订阅在重载和卸载时归零，已有 24 代重载夹具验证。

## 本轮修改

前两项是行为不变的热路径修改，第三项修复一个已有缺陷。没有改公开 API、默认编译选项或任何测试预期。

1. **快捷键候选索引（R7）**：`WindowManager::Private` 按首个按键缓存候选绑定（命令 ID、序列、参数、`enabled` 回调副本），在绑定或命令目录变化时（`bindingsRevision` 递增）惰性重建。分发时只取首键相同的候选，顺序与原来遍历“命令→绑定”一致；`enabled` 回调仍逐次调用，回调修改注册表时的中止规则不变。首键不同的绑定在 `QKeySequence::matches` 中必然是 `NoMatch`，所以过滤不改变结果。
2. **焦点与右键查找（R8）**：`FocusIn`、`ContextMenu` 处理改用 `LayoutModel::tabForPane()`。一个 Pane 只属于一个 Tab（由 `validate()` 保证），遍历顺序相同，结果一致，省去每个标签生成窗格列表的分配。

3. **侧栏重新显示（R16）**：隐藏状态下重建的收纳条留在 `DockSurface` 上，没有加入网格。重新显示时 `updateRailLayout()` 才把它加入网格，这一步会把控件改挂到 `DockSurfaceRoot`；Qt 改 parent 时会隐藏控件，只在下一轮事件循环补上显示。现在加入网格后立即显示应当可见的收纳条。原有 `SidebarPresentationTest::hidingKeepsPinnedContentAndCentralTabs` 的 8 行就是回归用例：修复前在基线提交 `89ef8d0` 和本轮修改上都稳定失败，修复后通过。

新增回归 `ShortcutDispatchTest::dispatchFollowsCatalogAndBindingChanges`：先触发一次按键建立索引，再依次新增命令、改绑定为两段序列、同首键的第二个命令、删除命令，每一步用真实按键验证分发结果，最后确认没有绑定时按键回到编辑器。

验证情况见 [预览说明](../development/window-system-preview.md) 中“审查与热路径修订”一节。
