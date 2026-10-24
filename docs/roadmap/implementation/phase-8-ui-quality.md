# 阶段 8：界面质量

**目的**：让所有临时界面、页面和窗口交互达到 [界面设计规范](../../design/ui-design-system.md) 的标准，解决 [界面审查](../review-2026-10/ui-audit.md) 中的问题，并实现侧栏临时面板的八向调整和窗口外侧缩放边框（[窗口交互细则](../../design/window-interaction-design.md#窗口缩放边框)）。

**阶段约束**：

- 每个步骤完成时，按规范 §6 导出浅色、深色、黑色截图到 `out/diagnostics/ui-review/<步骤>/` 并对照规范检查；阶段 2.5 的组件画廊完成前，用真实窗口截图代替。
- 统一组件替换旧实现后，旧实现删除，不保留两套。
- 现有测试按对象名查找控件的，新组件保留原对象名（例如 `CommandPalette`、`CommandPaletteSearch`）。依赖旧控件类型（如 `QTreeWidget` 接口）的断言，按测试规范提交独立证据说明新组件中如何检查同一行为，经评审后修订，并保留等效覆盖。
- **改变现有尺寸规格的步骤**（8.10）会修改已有测试中写明的尺寸，按测试规范必须先得到用户确认。

**步骤与依赖**：

| 步骤 | 内容 | 前置 |
| --- | --- | --- |
| 8.1 | 窗口缩放区移到外侧不可见边框 | 无 |
| 8.2 | 临时面板八向调整 | 8.1（贴窗口边缘的一侧依赖它） |
| 8.3 | 浮层窗口基类 | 2.4（颜色 ID），可先用现有令牌 |
| 8.4 | 快速输入组件并替换命令面板等 | 8.3 |
| 8.5 | 标签行内重命名 | 8.4（侧栏标签用输入框模式） |
| 8.6 | 统一对话框 | 8.3 |
| 8.7 | 改用 Windows 系统通知 | 无 |
| 8.8 | 空工作区 | 2.4 |
| 8.9 | 菜单结构、右键菜单顺序与文案 | 3.4（菜单 `when`）为佳，非硬性 |
| 8.10 | 尺寸规格统一 | 用户确认 |
| 8.11 | 界面字体渲染核查 | 2.5（组件画廊） |
| 8.12 | 英文源文本与全量翻译核查 | 无；推荐在 M1 下一步执行 |
| 8.13 | 选项卡长标题裁切 | 无；用户指定先实现 |
| 8.14 | 可访问性核查 | 2.9 |
| 8.15 | 钉住标签 | 无 |
| 8.16 | 按层级保存输入法状态 | 无；用户要求 |

设置页与快捷键页的界面按规范 §4.9、§4.10 分别在 [4.3](phase-4-settings.md#43-设置页由声明生成) 和 [3.8](phase-3-keybindings.md#38-快捷键编辑器重做) 中实现，本阶段不重复。

---

## 8.1 窗口缩放区移到外侧不可见边框（W2）

**目标**：非最大化窗口的左、右、下缩放区位于可见边框之外（与普通 Win32 窗口一致）；客户区边缘的像素全部归内容使用。外观不变。

**涉及文件**：`src/platform/windows/window_frame.cpp`（`WM_NCCALCSIZE` 约 492 行、`WM_NCHITTEST` 约 512 行、`resizeBorder()` 约 158 行、正常矩形与最大化对齐相关代码）、`src/platform/window_placement.hpp`、`tests/native_frame_test.cpp`、`tests/window_placement_test.cpp`。

**实现**：

1. 先阅读 `window_frame.cpp` 全文，列出所有依赖“客户区等于窗口矩形”的地方：正常矩形记录、最大化时扣除边框、全屏、拖拽预览窗口（`windowSystemDragPresentation`）、Snap、DPI 变化。把清单写进提交说明。
2. `WM_NCCALCSIZE`（非最大化、非全屏、非拖拽预览）：

   ```cpp
   const int border = resizeBorder(msg->hwnd, window);  // 系统缩放边框宽度（物理像素）
   bounds->left += border;
   bounds->right -= border;
   bounds->bottom -= border;
   // top 不变：自绘标题行一直到窗口顶部
   ```

   最大化分支保持现有逻辑。返回值仍为 0。
3. `WM_NCHITTEST`：
   - 先保留“标题按钮优先”的现有规则；
   - 点在客户区之外（左、右、下的不可见边框中）时返回对应的 `HTLEFT`／`HTRIGHT`／`HTBOTTOM`／`HTBOTTOMLEFT`／`HTBOTTOMRIGHT`；
   - 顶部：客户区最上方 `border` 像素内仍返回 `HTTOP`（左右两端返回 `HTTOPLEFT`／`HTTOPRIGHT`，范围扩展到不可见边框）；
   - 客户区内其余点返回 `HTCLIENT`，不再返回缩放命中。
4. 可见边框：现在 1 像素可见边框由 QSS 的 `QWidget#WorkspaceWindow { border: 1px }` 绘制在客户区内；保持不变。DWM 在不可见边框区域不绘制任何东西（Windows 10 起的标准行为），需要在 Windows 10 和 11 上分别确认没有出现可见的系统边框；若某版本出现，在提交说明中记录并改用备选方案（设计文档中的窄带方案）。
5. 窗口几何：`QWidget::geometry()` 是客户区，`frameGeometry()` 包含不可见边框。逐个检查保存／恢复正常矩形、`fitInitialPlacement()`、拖出窗口定位（“先放到鼠标位置再显示”）、最大化对齐，确保用户看到的位置与之前一致（可见边框位置不变）。
6. 现有注释中说明“客户区包含缩放边缘”的地方同步修改。

**测试**：

- `native_frame_test` 全部现有行通过。新增：
  - `resizeBorderLiesOutsideClient`：非最大化时 `GetClientRect` 换算的屏幕矩形比 `GetWindowRect` 左右下各小 `resizeBorder`；客户区左边缘内 1 像素处 `WM_NCHITTEST` 返回 `HTCLIENT`，客户区外 1 像素处返回 `HTLEFT`（直接 `SendMessage(WM_NCHITTEST)` 检查）；
  - `visibleBoundsUnchanged`：同一保存布局在修改前后恢复，可见窗口矩形（`DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`）相同；
  - 最大化与全屏的现有对齐测试保持通过。
- `window_placement_test` 全部通过。
- 真实桌面试用：从左、右、下、四角拖动缩放；Snap 布局弹窗；拖到屏幕边缘的系统贴靠；多显示器不同 DPI 之间移动。结果写入预览说明，未试用的项写明未试用。

**完成标准**：测试全部通过；Windows 11 上试用通过；Windows 10 若无法试用，在状态中写明“未在 Windows 10 验证”。

**风险**：这是原生帧的核心改动，影响面大。必须单独提交，提交前运行 `native-frame`、`window-placement`、`chrome-interaction`、`caption-strip`、`drag-*`、`focus-mode` 和应用部署夹具。

**状态**：未开始。

---

## 8.2 临时面板八向调整（W1）

**目标**：临时展开面板的四条边、四个角都能调整大小；位置仍按现有对齐规则；面板贴窗口边缘时也能从外侧调整。

**前置**：8.1。

**涉及文件**：`src/docking/peek_resize_handle.hpp`、`src/docking/dock_surface.{hpp,cpp}`（`showEdgeGroup` 中创建手柄的部分约 655–690 行，`placePeek()` 约 855–925 行）、`tests/peek_size_test.cpp`。

**实现**：

1. `DockSurface` 中三个手柄成员（`peekResizeHandle_`、`peekSecondaryResizeHandle_`、`peekCornerResizeHandle_`）替换为 `std::array<QPointer<QWidget>, 8> peekHandles_`，下标对应 `Qt::Edges` 的八种组合（上、下、左、右、左上、右上、左下、右下）。保留原有三个对象名（`PeekResizeHandle`、`PeekHeightResizeHandle`／`PeekWidthResizeHandle`、`PeekCornerResizeHandle`）给原来对应的那三个手柄，现有测试不受影响；新增的五个使用 `PeekResizeHandle.<方向>` 形式的对象名。
2. 尺寸计算改为“方向符号”：

   ```cpp
   // 向远离面板中心的方向拖动为放大。
   const int sx = edges & Qt::RightEdge ? +1 : edges & Qt::LeftEdge ? -1 : 0;
   const int sy = edges & Qt::BottomEdge ? +1 : edges & Qt::TopEdge ? -1 : 0;
   size.width  = sx ? qBound(140, initial.width()  + sx * delta.x(), QWIDGETSIZE_MAX) : initialPolicy.width;
   size.height = sy ? qBound(100, initial.height() + sy * delta.y(), QWIDGETSIZE_MAX) : initialPolicy.height;
   ```

   这与现有 `PeekResizeHandle::mouseMoveEvent` 的写法一致，只是推广到全部方向。贴边一侧（例如左侧面板的左边）同样适用：向左（远离中心）拖为放大。
3. 位置：`placePeek()` 不变（仍按触发标签对齐并限制在可用区域内），每次尺寸变化后重新放置。手柄几何按八个方向分别计算：边的命中带宽 4，长度为对应边长减去两端角区；角为 8×8；全部叠在面板边缘内侧，不增加留白（与现有设计“调整区域叠在面板边缘”的约束一致）。
4. 悬停提示：手柄悬停 300 ms 后，在面板对应边绘制 2 像素 `sash.hoverBorder`（阶段 2 之前用 `focus` 令牌）；拖动中保持；离开或松开后消失。实现为手柄自身的 `paintEvent`（手柄就在边上，直接画）。
5. 光标：上下边 `SizeVerCursor`，左右边 `SizeHorCursor`，左上／右下 `SizeFDiagCursor`，右上／左下 `SizeBDiagCursor`（现有构造函数已有此逻辑）。
6. 键盘：现有“设置临时展开大小…”命令保留（8.4 后改为输入框模式）。

**测试**（`peek_size_test.cpp` 新增，数据驱动 4 个边 × 8 个手柄）：

- 每个手柄向外拖 40 像素，对应维度增加 40（受最小值和可用区域限制），另一维度不变；向内拖则减小。
- 拖动后面板位置符合对齐规则（与 `placePeek()` 的计算一致）。
- 隐藏侧边栏、窗口非最大化、左侧面板贴窗口左边缘时：面板左边的手柄可拖动且窗口宽度不变；在窗口可见边框外 2 像素处按下拖动，窗口宽度改变而面板宽度不变（依赖 8.1；该行属于 `desktop` 层）。
- 原有三个手柄的全部断言保持通过。

**状态**：未开始。

---

## 8.3 浮层窗口基类（U7、M3）

**目标**：快速输入、菜单、下拉列表、工具提示、对话框共用同一种顶层窗口外观：圆角、系统阴影、主题颜色、深色边框、焦点返回。

**涉及文件**：新建 `src/chrome/overlay_window.{hpp,cpp}`；`src/platform/windows/window_frame.{hpp,cpp}`（新增 `applyOverlayFrame(QWidget*)`）；`src/floating_panel.cpp`（改为基于新基类）；`src/commands/popup_menu.cpp`（`DismissOnlyMenu` 显示前调用平台函数）；测试 `tests/floating_panel_test.cpp`、新 `tests/overlay_window_test.cpp`。

**实现**：

1. `OverlayWindow : QWidget`，窗口标志 `Qt::Tool | Qt::FramelessWindowHint`（菜单仍是 `QMenu` 自己的 `Qt::Popup`），提供：
   - `void showRelativeTo(QWidget* owner, OverlayAnchor anchor)`：`TopCenter`（快速输入）、`Center`（对话框）、`AtRect`（下拉、工具提示）；位置限制在所属窗口所在屏幕的可用区域内。
   - 焦点返回：显示时记录 `QApplication::focusWidget()` 的 `QPointer`，隐藏时若该控件仍存在且可见则 `setFocus()`，否则把焦点交给所属窗口的当前窗格（调用 `WindowManager::focusPane`）。
   - `Esc` 关闭；外部点击关闭的策略沿用 `FloatingPanelOptions`。
2. `applyOverlayFrame()`（Windows）：
   - `DwmSetWindowAttribute(handle, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, DWMWCP_ROUND)`；
   - 系统阴影：先尝试 `DwmExtendFrameIntoClientArea` 设 1 像素边距（无边框窗口获得 DWM 阴影的常用做法），若在实测中无效，再改用窗口类样式 `CS_DROPSHADOW`；选定的方法和实测结果写进代码注释；
   - 深色主题时设置沉浸式深色（2.7）。
   - 非 Windows 平台空实现。
3. `FloatingPanel` 改为继承 `OverlayWindow`（或组合），保留其公开 API 和“非模态、外部点击关闭且不吞掉点击”的契约；`FloatingPanelOptions` 不变。
4. 菜单：`DismissOnlyMenu` 在 `showEvent` 中调用 `applyOverlayFrame()`；QSS 中菜单的圆角、内边距、行高按规范 §4.2。

**测试**：`overlay_window_test`：各锚点的位置与屏幕边界限制；焦点返回到原控件、原控件已销毁时回到窗格内容；`Esc` 关闭。`floating_panel_test` 全部现有行通过。`desktop` 层另加一行读取 `DWMWA_WINDOW_CORNER_PREFERENCE` 确认已设置。

**状态**：未开始。

---

## 8.4 快速输入组件（U1、U2、U3、U5）

**目标**：实现规范 §4.1 的快速输入（列表选择与单值输入两种模式），并替换命令面板、窗口选择、标签组目标选择、临时展开大小输入、工具条移动等全部同类界面。

**涉及文件**：新建 `src/quickinput/quick_input.{hpp,cpp}`、`quick_input_model.{hpp,cpp}`、`quick_input_delegate.{hpp,cpp}`、`fuzzy_match.{hpp,cpp}`、`command_history.{hpp,cpp}`；改写 `src/commands/command_dialogs.cpp`（命令面板、窗口选择）、`src/commands/tab_group_commands.cpp`（`chooseTarget` 约 77 行起）、`src/commands/peek_size_command.cpp`、`src/commands/toolbar_move_command.cpp`、`src/commands/hierarchy_commands.cpp` 中的目标选择；测试 `tests/view_commands_test.cpp`、`tests/floating_panel_test.cpp`、新 `tests/quick_input_test.cpp`、`tests/fuzzy_match_test.cpp`。

**实现**：

1. **模糊匹配**（`fuzzy_match`，纯函数）：

   ```cpp
   struct FuzzyMatch { int score = 0; QVector<QPair<int, int>> ranges; }; // 命中区间，用于高亮
   std::optional<FuzzyMatch> fuzzyMatch(QStringView pattern, QStringView text);
   ```

   规则：模式中的字符按顺序出现在文本中即匹配（大小写不敏感）；得分：连续命中加分、词首（空格、`.`、`/`、大写字母、中文字符边界之后）命中加分、首字符命中加分、间隔减分。多个空格分隔的词分别匹配并合并区间。参照 VS Code `fuzzyScore` 的思路，但不追求完全相同的分数。

2. **模型与绘制**：`QuickInputModel : QAbstractListModel`，条目为

   ```cpp
   struct QuickPickItem {
       QString id; QString label; QString description; QIcon icon;
       QVector<QKeySequence> keys; QString group; bool enabled = true;
   };
   ```

   `QuickInputDelegate` 自绘一行：图标、标签（命中区间加粗并用 `list.highlightForeground`）、同行说明（`descriptionForeground`）、右侧按键方框（`keybindingLabel.*`，每个按键一个圆角 3 的小框，组合键之间用“+”，续键之间留空格）。分组标题行由模型插入为不可选的特殊行。不为每行创建控件。

3. **QuickInput 控件**（基于 8.3 `OverlayWindow`，锚点 `TopCenter`）：

   ```cpp
   class QuickInput : public OverlayWindow {
   public:
       struct Step { QString title; QString placeholder; QVector<QuickPickItem> items; };
       void showPick(Step step);                         // 列表模式
       void showInput(QString title, QString prompt, QString value,
                      std::function<QString(const QString&)> validate); // 输入框模式，返回错误文字
       std::function<void(const QuickPickItem&)> activeChanged;  // 预览（例如主题）
       std::function<void(const QuickPickItem&, Qt::KeyboardModifiers)> accepted;
       std::function<void()> cancelled;
   };
   ```

   每个窗口最多一个实例；打开新的替换旧的（旧的触发 `cancelled`）。多步流程由调用方在 `accepted` 中调用下一步 `showPick`，标题显示“（2/3）”，输入框为空时 `Backspace` 返回上一步。

4. **命令面板**：
   - 条目来自命令注册表；`menuWhen`（3.4）为假或 `enabled` 为假的命令默认不显示；设置 `commandPalette.showUnavailable` 为真时以禁用状态显示在最后。
   - 分组：“最近使用”（最多 10 条）与“其他命令”。最近使用记录在 `CommandHistory`（容量 50，按执行时间）；`WindowManager` 提供 `commandHistory()`／`setCommandHistory()` 和变化信号，宿主随布局或状态文件保存。
   - 说明列显示分类；按键显示全部绑定（最多 2 个，多余的在工具提示中）。
   - 前缀：`>` 命令（默认）、`%` 已打开的标签（跨窗口，选中即激活）、`@` 当前标签的窗格。
   - 保留对象名 `CommandPalette`、`CommandPaletteSearch`；列表对象名 `CommandPaletteCommands`。
5. **窗口选择**：条目为窗口标题与类型、所属窗口族，说明列显示活动标签；对象名 `WindowSelector` 保留。
6. **标签组目标选择**（替换三层菜单）：多步——①选择目标标签组（列出全部窗口的组，说明列为“窗口名 · n 个标签”；移动时当前组不列出）；②需要方向时选择方向（左、右、上、下，带图标）。在第①步移动选择时，在目标窗口中用 `muxora.dropTarget.*` 颜色高亮该组（复用拖拽覆盖层的绘制），关闭时清除。
7. **输入框模式的使用**：临时展开大小（“宽 × 高”，如 `320 × 480`，留空表示该轴默认；校验范围与现有对话框相同）、移动工具条的行号、命令面板中执行带参数的重命名。
8. 删除 `command_dialogs.cpp` 中旧的 `createDialog`、`QTreeWidget` 列表和按钮代码，删除 `peek_size_command.cpp` 中的 `QDialog::exec()`。

**测试**：

- `fuzzy_match_test`：匹配与不匹配、区间、得分次序（词首 > 连续 > 分散）、中英文混排、多词。
- `quick_input_test`：键盘导航（含分组行跳过）、`Enter`／`Esc`、预览回调、多步前进与返回、输入框校验阻止提交、替换旧实例、焦点返回。
- 现有 `view_commands_test`、`floating_panel_test` 中命令面板和窗口选择相关断言：按对象名的保持；依赖 `QTreeWidget` 的按约束修订。
- 目标选择：键盘完成“与指定标签组交换”“移动到指定组的右侧”；高亮在选择时出现、关闭后消失。

**状态**：未开始。

---

## 8.5 标签行内重命名（U4）

**涉及文件**：`src/docking/tab_strip.{hpp,cpp}`、`src/docking/edge_strip.cpp`、`src/custom_chrome.cpp`（`tab.rename` 约 390–405 行）、测试 `tests/tab_strip_layout_test.cpp` 或新 `tests/tab_rename_test.cpp`。

**实现**：

1. 中央与固定组的标签：`tab.rename` 无 `title` 参数时，在标签位置覆盖一个 `QLineEdit`（与标签同尺寸，选中全部文字），`Enter` 或失焦提交、`Esc` 取消；提交时调用 `renameTab`。标签条滚动或布局刷新时取消编辑。
2. 收纳条标签（竖排）和命令面板入口：使用 8.4 的输入框模式。
3. 双击标签不触发重命名（与 VS Code 一致）；右键菜单“重命名…”和 `F2`（`when: tabFocus`，阶段 3 之后）触发。
4. 删除 `QInputDialog::getText` 调用。

**测试**：行内编辑提交、取消、失焦提交、空标题处理（与现有 `renameTab` 规则一致）、编辑中关闭标签不崩溃、编辑中布局刷新取消编辑。

**状态**：未开始。

---

## 8.6 统一对话框（U8）

**涉及文件**：新建 `src/chrome/message_dialog.{hpp,cpp}`；公开到 `include/window_system/`（宿主需要用于关闭确认）；`src/app/main.cpp`（关于对话框）。

**实现**：

```cpp
struct DialogButton { QString text; QString id; bool primary = false; };
struct MessageDialogOptions {
    enum class Severity { Info, Warning, Error } severity = Severity::Info;
    QString title; QString detail; QVector<DialogButton> buttons; // 按显示顺序；主要按钮在左
    QString cancelId;                                             // Esc 返回的按钮
};
// 非阻塞：立即返回；结果通过回调。所属窗口关闭时以 cancelId 回调。
void showMessageDialog(QWidget* owner, MessageDialogOptions options, std::function<void(QString buttonId)> done);
```

外观按规范 §4.3（基于 8.3 浮层，锚点 `Center`，对所属窗口模态）。“关于 Muxora”改用此组件。阶段 5.2 的未保存确认使用它。

**测试**：按钮顺序与默认、`Enter`／`Esc`、所属窗口关闭时的回调、回调中销毁所属窗口不崩溃。

**状态**：未开始。

---

## 8.7 改用 Windows 系统通知（U9）

> 不做通知中心，也不做程序内的自绘提示（[界面规范 §4.4](../../design/ui-design-system.md#44-通知使用-windows-系统通知)）。

**涉及文件**：`src/app/script_services.cpp`（`ui.notify` 现在用非模态 `QMessageBox`，约 104–136 行）；新建 `src/platform/windows/system_notification.{hpp,cpp}`；`src/app/tests/scripting_test.cpp`。

**实现**：

1. 平台层接口：

   ```cpp
   struct SystemNotification { QString title; QString text; QString tag; }; // tag 用于点击后定位窗口或标签
   bool showSystemNotification(const SystemNotification& n, std::function<void(QString tag)> activated);
   ```

2. Windows 实现，按下面的顺序评估并在代码注释中记录选择：
   - **首选 WinRT 通知**（`Windows.UI.Notifications.ToastNotificationManager`）：未打包的桌面程序需要应用标识：启动时调用 `SetCurrentProcessExplicitAppUserModelID(L"Muxora.Muxora")`，并在 `HKCU\Software\Classes\AppUserModelId\Muxora.Muxora` 下登记显示名（应用目前没有图标，按 AGENTS.md 暂不设计 logo，先不设图标）。项目使用 MSVC（ADR 0009），可以直接用 Windows SDK 自带的 C++/WinRT 头文件；确实调用不通时用下一种。
   - **备选**：`QSystemTrayIcon::showMessage`。Windows 10／11 会把它显示为系统通知，但需要一个托盘图标；只在有通知时显示托盘图标，通知消失后隐藏。
3. 点击通知：激活 Muxora，按 `tag` 切到对应窗口或会话标签。
4. `ui.notify` 改为调用 `showSystemNotification`；参数 `title`、`text` 保持兼容，`timeout_ms` 不再有意义（系统决定显示时长），接受但忽略，并在 `lua-guide.md` 中说明。删除程序内 `QMessageBox` 通知及其数量上限。
5. 阶段 7 的“命令完成”“后台响铃”通知复用同一接口，只在 Muxora 不在前台时发出，并调用 `FlashWindowEx` 闪烁任务栏按钮；设置项 `terminal.notifications.enabled`。
6. 系统“勿扰”或用户关闭了 Muxora 的通知时，系统会自行不显示，Muxora 不另做处理。

**测试**：平台接口用可注入的实现做单元测试（参数、`tag` 回调、前后台判断）；`scripting_test` 中原来检查 `LuaNotification` 消息框的断言改为检查系统通知调用（参数、长度限制、非法参数报错保持不变）——这是用户要求的行为变化；真实系统通知的显示在桌面上试用确认，并在预览说明中如实记录。

**状态**：未开始。

---

## 8.8 空工作区（P3）

**涉及文件**：`src/docking/dock_surface.cpp`（`EmptyWorkspace` 标签，约 106 行）。

**实现**：中央显示若干常用命令（新建标签、命令面板、设置、键盘快捷方式；业务阶段加入“打开会话”），每行左侧是命令名（可点击执行）、右侧是按键方框；命令列表由宿主通过 `setEmptyWorkspaceCommands(QStringList)` 指定，默认包含模块内的三个。文字颜色 `descriptionForeground`，居中，不显示图标或插图。

**测试**：点击执行命令；按键显示随绑定变化更新；宿主自定义列表。

**状态**：未开始。

---

## 8.9 菜单结构、右键菜单顺序与文案（M1、M2、M4）

**涉及文件**：`src/commands/command_menus.cpp`、`src/app/main.cpp`（应用命令的菜单路径）、两个翻译文件、`tests/menu_structure_test.cpp`、`tests/sidebar_menu_test.cpp`。

**实现**：

1. 主菜单调整为 [交互改进 §2](../review-2026-10/window-system-ux-plan.md#2-菜单与命令结构) 的结构：文件、编辑、视图、窗口（合并原“布局”和“窗口”）、工具、帮助；“会话”“终端”菜单在阶段 7 加入。
2. “编辑”菜单的通用命令：`edit.undo`、`edit.redo`、`edit.cut`、`edit.copy`、`edit.paste`、`edit.selectAll`、`edit.find`。实现为“转发给当前焦点控件”：`QLineEdit`、`QTextEdit`、`QPlainTextEdit` 调用其同名槽；内容控件可以通过动态属性 `windowSystemEditActions` 声明支持的操作，或实现一个小接口（`EditTarget`）由模块查找。没有可用目标时命令禁用。
3. 标签右键菜单按规范 §4.2 重新分组：关闭组 → 固定与颜色 → 移动与拆分 → 转换 → 所在标签组 → 复制标题。
4. 文案检查：写一个测试遍历所有命令，检查：需要进一步输入的命令（打开快速输入或对话框的）标题以“…”结尾，其他命令不以“…”结尾；标题不以句号结尾；同一命令在菜单中显示的文字与命令标题一致。需要输入的命令在注册时用一个标志声明（`Command` 增加 `bool opensInput = false;`）。
5. 菜单结构变化是用户可见的规格变化，`menu_structure_test` 的预期按新结构修订——这属于需求变化而不是放宽测试，在提交说明中引用本文档和用户要求。

**测试**：菜单结构新预期；编辑命令对 `QLineEdit`、`QPlainTextEdit` 生效、无目标时禁用；文案检查。

**状态**：未开始。

---

## 8.10 尺寸规格统一（W3，需要用户确认）

**内容**：按规范 §2.2 统一菜单行高、输入控件高度等。现有测试中写明了部分尺寸（例如工具条、临时标题），修改它们属于规格变化。

状态栏已单独完成（2026-10-19，用户确认按 VS Code 改为 22）：`ThemeMetrics::statusHeight` 默认值改为 22 并真正生效，字体更大时仍随字体撑高。之前该度量未被使用，实际高度为字体高度 + 6（默认字体下 21）。

**做法**：先列出所有与规范不一致的尺寸及其测试位置（`grep -rn "23\b\|setFixedHeight" modules/window-system/src`，再逐个核对），整理成一张“现值 → 规范值 → 受影响测试”的表，提交给用户确认；确认后再改代码和测试预期。

**状态**：未开始（等待确认）。标签内部的内边距和间隙已按用户 2026-10-23 的要求单独调整，规格见 [界面设计规范 §2.2](../../design/ui-design-system.md#22-尺寸)，列表时不再重复提出。

---

## 8.11 界面字体渲染核查

**目标**：界面文字按 [界面设计规范 §2.4](../../design/ui-design-system.md#24-字体) 跟随系统 ClearType 设置，在常见缩放下清晰。

**前置**：2.5（组件画廊，用于逐组件检查）。

**实现**：

1. 确认程序实际使用的字体引擎是 DirectWrite（Qt 6.12 在 Windows 上的默认），记录 `QFontDatabase`／平台插件报告的引擎，写入预览说明。
2. 全仓库检查：没有 `QFont::NoSubpixelAntialias`、`QT_QPA_PLATFORM` 的 `fontengine` 参数或其他关闭子像素抗锯齿的设置；带文字的窗口、弹窗、面板没有设置 `WA_TranslucentBackground`（拖动预览等不带正文的半透明窗口除外，列出清单）。
3. 组件画廊在 100%、150%、200% 缩放下，分别在系统 ClearType 开启和关闭时截图，逐组件检查发虚、彩边和截断。

**测试**：自动测试检查带文字的主要窗口和弹窗没有半透明背景属性；在 ClearType 开启时对一个已知按钮文字截图，断言存在子像素彩色像素（说明 ClearType 生效），关闭时断言没有（测试中通过 `SystemParametersInfo` 读取当前设置，只读不改；当前设置不满足某一项时该项写明跳过原因，不改系统设置）。

**状态**：未开始。

---

## 8.12 英文源文本与全量翻译核查

**目标**：落实 [翻译源文本与覆盖要求](../../design/settings-and-commands-design.md#83-翻译源文本与覆盖)，将现有中文翻译键迁移为英文，并核查全部第一方源码，保证用户可见文本没有漏译、中英文译文正确。决定见 [ADR 0010](../../adr/0010-english-translation-source.md)。

**前置**：无。排在 1.7 之前，先完成迁移，再继续会新增文字的主题、设置、终端等步骤。

**涉及文件**：`modules/` 与 `src/` 中全部第一方源码及其文字资源；重点为 `modules/window-system/src/`、`modules/window-system-lua/src/`、`modules/lua-runtime/src/`、`src/app/`、`src/workspace_host/`；窗口模块与应用的 CMake、翻译目录、`localization_test.cpp` 及其他含旧查找键的测试；`tools/check-translations.ps1`（新增覆盖检查入口，2026-10-24 改写为 `check-translations.py`）。

**实现**：

1. 在改动前盘点文本来源和调用路径：直接翻译、`QT_TRANSLATE_NOOP` 静态表、动态菜单路径与分类、设置声明、错误传播、内嵌 Lua、资源和模板。扫描全部第一方源码中的中文及未标记英文候选，逐项判断是否面向用户；记录覆盖范围和有理由的豁免。不能只扫描 `.ts` 或只匹配已有翻译调用。
2. 按设计迁移查找键，沿用现有 `WindowSystem`、`Muxora` 上下文及资源加载方式；内部标识与显示文字分开映射。宿主错误和脚本诊断按照呈现路径接入翻译，不修改用户脚本产生的内容。
3. 运行 `window_system_lupdate`、`muxora_lupdate`，核对源文件范围包含全部需要提取的文字；有遗漏的第一方目标或资源补充提取输入。同步修改源语言声明、两个模块各自的中英文目录和测试查找键，保留目录及资源名称。英文目录保留需要的复数形式，Qt 标准控件继续加载官方目录。
4. 逐条核对中英文文案，特别检查动态菜单、错误信息、多行文本、助记符、占位符及单复数。迁移不调整菜单归属、命令行为、界面默认语言、用户标题或持久化标识。
5. 新增可独立运行的翻译覆盖检查：从源码提取的结果与受版本控制的目录比较，报告漏提取、缺失、未完成、空译文及结构不一致；候选扫描补查没有翻译标记的文字。仅检查目录中已有条目的测试保留，但不作为完整覆盖结论。新脚本用法写入测试要求，实际通过后再接入统一验证和 CI。

**测试**：

- 两个 `localization_test.cpp`：新增 `sourceLanguageIsEnglish`、`sourceAndCatalogCoverage`、`dynamicMenuTextIsTranslated`、`pluralMessagesUseCorrectForms`，检查源语言、提取覆盖、运行时动态条目以及零、单数、复数；保留语言选择、缺目录时不部分安装、Qt 标准控件和用户内容不被翻译的原覆盖。
- 覆盖检查的反向用例：新增未标记的用户界面文字、删除静态表提取标记、删除目录条目或破坏占位符时必须失败；反向用例用临时输入，不提交故意损坏的源码或目录。
- 按实际修改范围运行命令、菜单、设置、脚本诊断和宿主内容相关套件；既有中文预期保持其含义，不能通过删除断言来迁移。

**验证**：先运行源码与目录覆盖检查，再经 [统一桌面隔离入口](../../development/testing-design.md#3-工具与统一入口) 运行 `window-system.localization`、`muxora.localization` 及受影响套件；核对两种语言的命令行帮助和部署后资源加载。集中完成迁移后执行一次 Release 全量、独立模块与外部宿主检查，刷新固定预览；不因构建类型不同重复同一批检查。

**完成标准**：源码扫描候选逐项处理，全部用户可见文字有正确的中英文输出，提取与目录检查及反向用例通过；动态路径、数量文案和用户内容边界有程序断言；记录每次检查的代码版本及结果。不能仅凭“目录无 unfinished”宣布完成。

**风险**：盲目替换中文会改变内部菜单路径、存储标识或用户数据；只改现有目录会漏掉原本未标记的文字。扫描和运行时覆盖要相互核对，不能用宽泛排除规则掩盖遗漏。

**状态**：已完成（`a04fa57`、`27d0266`）。英文源文本迁移、源码覆盖、本机统一入口和验证均已完成，见 [记录](../../development/verification-log/2026-10.md#英文源文本迁移收尾2026-10-24)。用户决定将 CI 接入留到 [6.6](phase-6-engineering.md#66-持续集成) 统一实施，不影响本步骤完成。

---

## 8.13 选项卡长标题裁切

**目标**：通过设置限制选项卡标题占用的长度，超出部分直接裁切，不使用省略号。具体单位、范围和默认值只维护在 [标签外观设置](../../design/settings-and-commands-design.md#31-标签外观与颜色)。

**前置**：无；用户要求由 Codex 直接实现，先于推荐顺序中的后续工作。

**涉及文件**：`tab_appearance.hpp`、`style/tab_appearance.cpp`；`docking/tab_strip.cpp`、`edge_tab_button.*`、`dock_surface.cpp`、`drag_presentation.*`；`chrome/workspace_window.cpp`；标签外观设置、呈现命令与菜单；Lua 外观适配、翻译目录、外部宿主夹具及相关测试。

**实现**：

1. 将开关和文字最大长度接入公开外观类型、设置页、命令、Lua 和布局持久化，严格校验。
2. 普通与固定标签按受限文字长度计算宽度；四边收纳条沿文字长轴使用同一策略，移除与可配置上限冲突的旧固定总长上限。
3. 按完整字符串排版并显式裁剪文字区域，保留装饰和操作空间；拖拽标签、插入空位和真实标签几何保持一致。设置修改后更新换行及滚动范围，不改内容身份。
4. 长标题绘制优化：先记录同机定向基准，再缓存自然宽度及与文字区域相交的已排版字形。保留跨裁切边界的字形和原文语义，不以截短字符串破坏连字、组合符或双向文字；重复绘制不重新处理全文。按文本、字体、方向和实际显示区域更新，关闭标签释放缓存，不使用无界全局缓存。首次处理成本和重复绘制收益分别测量，缓存与全文参考由程序逐像素核验。
5. 普通／固定标签栏与非固定侧栏的裁切开关及长度独立配置；不添加版本升级迁移，拖拽预览和插入空位分别使用目标样式的策略。覆盖不同配置下往返拖动、即时修改及布局恢复。
6. 长度使用同排滑条和 Qt 数字框，保留上下微调，扩大可输入范围并支持零值；单位格式和具体范围只在标签外观设置中维护。组合组件不处理单位，显示后缀由数字框独立处理；纯外观变化不卸载内容，连续拖动、键入和页面滚动均有程序检查。

**测试**：外观/API/JSON/Lua 合法及非法值、缺省字段、设置双向同步；普通与固定、单行与多行、上下位置、四侧标签的几何；实际绘制与全文裁切参考的程序比较；装饰、完整提示、拖拽和插入范围。运行相关套件及独立模块、安装后外部宿主检查，不重复全量。

**状态**：实现及相关自动验证完成，基础功能见 [记录](../../development/verification-log/2026-10.md#选项卡标题裁切实现与验证2026-10-23)，绘制优化见 [缓存与对比](../../development/verification-log/2026-10.md#长标题可见字形缓存2026-10-23)，独立配置及往返拖动见 [交付记录](../../development/verification-log/2026-10.md#独立侧栏标题配置与往返拖动2026-10-23)，连续输入焦点修复见 [回归](../../development/verification-log/2026-10.md#标题长度连续输入的焦点修复2026-10-23)，滑条与数值组合、单位恢复及新范围见 [验证](../../development/verification-log/2026-10.md#标题长度滑条与可复用数值组件2026-10-23)。三主题截图审查和用户实际试用仍未执行。

---

## 8.14 可访问性核查

**目标**：自绘控件在屏幕阅读器和键盘下可用。来源：[交互计划 §5](../review-2026-10/window-system-ux-plan.md#5-其他交互问题) 中“还需要检查高对比主题和屏幕阅读器下的标签条、边栏”；高对比部分由 2.9 负责。

**前置**：2.9。

**涉及文件**：`docking/tab_strip.cpp`、`docking/edge_tab_button.cpp`、`docking/edge_strip.cpp`、设置页和快捷键页的自绘控件。

**做法**：用 Windows 讲述人逐个检查标签条、收纳条、临时面板、设置页：可访问名称和角色、选中与展开状态、焦点移动时的朗读；用 `QAccessible` 接口写自动化断言（名称、角色、状态、子项数），朗读效果的人工检查结果写进步骤状态。发现的问题在本步骤内修复。

**状态**：未开始。

---

## 8.15 钉住标签

**目标**：按 [交互细则 · 钉住标签](../../design/window-interaction-design.md#钉住标签) 实现 VS Code 式的钉住标签。用户 2026-10-24 要求加入，并确认拖动规则：落点仍在钉住段内只调整钉住顺序，落到未钉住标签之后就取消钉住。

**前置**：无。

**8.15a 基本行为（已完成）**：模型用每组的钉住数表示开头的连续钉住段，所有增删、移动、排序、合并、拆分和恢复都维持这一段；非固定收纳条没有钉住段。`WindowManager::setTabSticky()`／`isTabSticky()`、命令 `tab.stick`／`tab.unstick`／`tab.toggleSticky`、标签右键与“布局 → 当前标签”菜单、批量关闭跳过钉住标签、标签栏的钉子图标（点击取消钉住）、布局保存及 Lua `tabs.sticky()`、`groups.list()` 的 `sticky_tabs`。

**8.15b 外观与保护（已完成）**：用户 2026-10-24 确认四项都做，前三项照 VS Code 做成设置，选项和默认值与 VS Code 相同。设置放在设置页“标签外观”下的“钉住标签”分类。

1. 设置 `tabs.stickySizing`（VS Code `workbench.editor.pinnedTabSizing`）：`normal`（默认）、`compact`（只显示图标或首字）、`shrink`（显示标题开头，标题长度由 `tabs.shrinkStickyTitleWidth` 限制，默认 40 px，和普通标题裁切分开，用户 2026-10-24 要求）。紧凑和收缩时，钉住段在标签栏滚动时不离开视野，其余标签在它后面滚动。**已完成**。
2. 设置 `tabs.stickyTabsOnSeparateRow`（VS Code `workbench.editor.pinnedTabsOnSeparateRow`）：钉住标签单独显示在上方一行，两行之间拖动即钉住或取消。**已完成**。
3. 设置 `tabs.preventStickyClose`（VS Code `workbench.editor.preventPinnedEditorClose`）：按键、中键、两者都不关闭钉住标签或都关闭，默认两者都不关闭。前置的中键关闭标签已实现。按键关闭指关闭当前标签的快捷键，以及会连带关闭标签的关闭窗格快捷键；被拦住时和 VS Code 一样改为切到第一个未钉住的标签（先本组，再同窗口其他组）。不需要 3.x 的上下文按键条件：快捷键分发时就知道来源。**已完成**。
4. 完整标签列表中标出钉住标签：行尾一直显示钉子，关闭按钮照旧悬停时出现。**已完成**。

**测试**：8.15a 见模型、标签条、命令与 Lua 用例；8.15b 各项需要真实拖动、滚动和像素断言，三种主题下截图。

**状态**：8.15a 已完成；8.15b 四项和前置的中键关闭已完成，相关自动验证见 [记录](../../development/verification-log/2026-10.md#钉住标签的外观与保护815b2026-10-24)；只在默认主题下截图核对过，三主题截图和用户真实试用未做。

---

## 8.16 按层级保存输入法状态

**目标**：按 [交互细则 · 输入法状态](../../design/window-interaction-design.md#输入法状态) 实现：设置 `window.inputMethodScope`（默认 `tab`）选择在哪一层分开记输入法状态，焦点切到另一个范围时恢复那里的状态。用户 2026-10-24 提出并确认：输入法和它的全部模式一起记，不只针对中英文；默认按标签；轻量浮窗是窗口下面的一层。

**前置**：无。设置先按现有方式加入 `WindowBehavior` 和设置页，4.2 时接入注册表，ID 加进阶段 4 的表格。设置页的下拉框属于新界面，由 Claude 做。

**涉及文件**：新建 `modules/window-system/src/platform/input_method_state.{hpp,cpp}`（平台接口和 Windows 实现）、`src/input_method_scopes.{hpp,cpp}`（范围跟踪）；`window_manager.{hpp,cpp}`（`WindowBehavior::inputMethodScope`、焦点变化）；`settings/settings_page.cpp`；`layout_persistence.cpp`（随行为保存）；Lua 设置接口和指南；新测试 `tests/input_method_scope_test.cpp`。

**实现**：

1. 平台接口只有“读出当前状态”和“恢复一个状态”两个操作，状态对上层是不透明的值。Windows 实现：输入法或键盘布局用 `GetKeyboardLayout`／`ActivateKeyboardLayout`；开关和模式在焦点窗口的输入上下文上用 `ImmGetOpenStatus`／`ImmGetConversionStatus` 读，用对应的 `Set` 函数恢复，全部模式位原样保存。其他平台先用空实现，不报错。
2. 范围跟踪：`QApplication::focusChanged` 时，按当前层级算出新旧焦点所属的范围键（窗格、标签、标签组、顶层窗口、窗口族或整个程序）；键不同才保存旧范围、恢复新范围。临时界面（命令面板、对话框、菜单）不算范围。
3. 新建：新范围复制创建时所在范围的状态。移动、合并、关闭：按设计更新或丢弃记下的状态，不让已删除的标签、窗格留下记录。
4. 修改设置时清空全部记录。

**测试**：范围跟踪用假的平台实现做模型测试：六种层级下焦点在两个范围之间来回切换的保存和恢复；同一范围内移动焦点不调用平台接口；新建沿用来源状态；移动、合并、关闭后的记录；修改设置后清空；临时界面不触发。Windows 实现的读写往返另有桌面用例（切换系统里已装的键盘布局），真实输入法（微软拼音、日文、韩文和第三方输入法）由用户试用，未试用的如实记录。

**状态**：未开始。
