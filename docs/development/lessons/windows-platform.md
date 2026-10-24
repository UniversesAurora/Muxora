# Windows 平台经验

适用于 Windows 11（26200），自绘标题栏的无边框窗口。

## 输入法

### 输入法状态要把 TSF 配置和 IMM 模式一起保存

- **现象**：TSF 输入法在两个标签里设成不同的输入模式，切回后没有恢复。最初只保存键盘布局和 IMM 模式。
- **原因**：活动输入处理器只能从 TSF 配置接口取得；Qt 的编辑控件不一定有可读的 TSF 转换模式隔离区，这时准确的模式位只在输入窗口的 IMM 上下文里。只保存一层，换一种输入法就缺一部分状态。另外，读得到模式不等于写得回去：[ImmSetConversionStatus](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immsetconversionstatus) 说明 Windows 8 起部分日文 IME 按用户保持模式，获得焦点时可能拒绝修改。
- **做法**：平台层把活动 TSF 配置、键盘布局、TSF 隔离区和 IMM 开关／转换／句子模式作为一个不透明状态整体保存，不简化成中英文布尔值。恢复时先激活 TSF 配置，再写隔离区，缺的项用 IMM 补；写入被拒绝时不报错，也不改用户的系统设置。IMM 上下文取顶层窗口已有的句柄，不对编辑器调用 `winId()`，否则会建出原生子窗口（见 [原生子窗口会破坏命中测试](#窗口里出现原生子窗口会破坏命中测试)）。复现要用真实的标签点击和编辑器点击，不用直接调用管理器 API 代替。
- **验证**：`InputMethodNativeTest::installedLayoutsRoundTripAndTabsRestore` 检查已装布局和模式往返；`inputProcessorModeFollowsTabScope` 遍历系统里的 TSF 输入处理器，取第一个能写模式的，在两个真实标签之间切换并逐次读取。候选窗和实际输入效果要用户试用。
- **出处**：2026-10-24，`26696f8`、`91956aa`、`a13a17c`。

## 标题栏与命中测试

### 只有最大化 / 还原按钮返回 `HTMAXBUTTON`

- **现象**：最大化按钮悬停时不出现 Win11 的 Snap 布局浮窗；或者最大化后悬停最小化、关闭按钮，系统黄底黑边的经典提示和 Qt 的主题提示同时出现。
- **原因**：Snap 浮窗只在 `WM_NCHITTEST` 返回 `HTMAXBUTTON` 时出现，由系统处理，普通窗口和最大化窗口都一样。而只要返回原生标题按钮命中（`HTMINBUTTON`、`HTCLOSE` 等），Windows 就会显示自己的经典提示；拦截 `WM_NCMOUSEHOVER` 等悬停消息也挡不住。
- **做法**：最大化 / 还原按钮返回 `HTMAXBUTTON` 以保留 Snap；最小化和关闭在任何状态下都返回 `HTCLIENT`，交给 Qt 处理点击和提示。原生命中只集中在 `platform/windows/window_frame.cpp`。
- **验证**：`CaptionStripTest::maximizedCaptionButtonsShowThemedToolTips`，以及边缘、角落命中测试。
- **出处**：`944a9c6`。

### 窗口里出现原生子窗口会破坏命中测试

子控件变成原生窗口后，鼠标所在位置的 `WM_NCHITTEST` 发给子窗口而不是顶层窗口，顶层窗口的 `HTMAXBUTTON` 不再生效，Snap 浮窗消失。原因和避免方法见 [Qt Widgets：不要对还没有句柄的弹窗调用 `winId()`](qt-widgets.md#不要对还没有句柄的弹窗调用-winid)。排查时先枚举子窗口，见 [调试方法](debugging.md#枚举原生子窗口)。

## DPI

### 最大化时屏幕最后一个物理像素落在所有子控件之外

- **现象**：窗口最大化后，鼠标顶到屏幕右上角点关闭按钮没有反应，左上角正常。200% 的显示器和 150% 的笔记本屏幕（1920×1200，位于主屏左侧）都出现过。
- **原因**：客户区最后一列物理像素换算成逻辑坐标时会超过最后一个整数像素：200% 时 3839 → 1919.5，150% 时 1919 → 1279.33。只要超过“宽度 − 1”，哪怕只多 0.33，Qt 就找不到子控件，把移动和按下事件交给窗口本身，按钮收不到。`QWidget::childAt(QPoint)` 用取整后的点仍会返回按钮，所以用它检查不出问题。最左、最上的像素是 0，不受影响，所以只有右边和下边出问题。按钮由系统原生命中（`HTCLOSE`）时不经过这一步，改成 Qt 处理后才暴露。
- **做法**：窗口最大化或全屏时，在平台层（`window_frame.cpp` 的 `keepEdgePointerInside`）把客户区鼠标消息的坐标向内收，直到换算后不超过最后一个整数逻辑像素。滚轮消息带的是屏幕坐标，不处理。第一版只在“舍入后落到窗口外”时才收，200% 下有效，150% 下 1279.33 舍入后仍在窗口内，于是没有收，问题依旧。
- **验证**：`CaptionStripTest::expandedCaptionControlsReachEveryScreenCorner` 和 `maximizedCaptionControlsOwnTheActualScreenEdge` 向窗口发送 `WM_LBUTTONDOWN` / `WM_LBUTTONUP`，由 Qt 自己换算物理坐标。ctest 条目 `window-system.caption-strip-scaled` 设置 `QT_SCALE_FACTOR=0.75`，在 200% 的显示器上按 1.5 倍率再跑一遍这两组测试；第一版修复在这个条目下失败 16 条，现在通过。用 `QT_SCALE_FACTOR` 可以在一台机器上模拟其他缩放比例的换算，但模拟不了 Windows 本身的 DPI 和多屏布局。
- **排查**：`tools/diagnostics/window-frame-probe.ps1` 只读地列出窗口、客户区、工作区、DPI，以及四个角像素的命中结果和像素下面实际是哪个窗口，可以在别的机器上运行后对比。
- **出处**：2026-10-20。

### 没有声明 DPI 感知的工具拿到的是虚拟化坐标

- **现象**：用 PowerShell 读到的窗口坐标是实际值的一半（200% 缩放时）。
- **原因**：`pwsh.exe` 没有声明 DPI 感知，Windows 对它返回按 96 DPI 虚拟化后的坐标。
- **做法**：在 PowerShell 里比较坐标时，先确认对方是否感知 DPI；需要物理坐标时，在测试进程内用 Win32 API 读取，或在脚本里调用 `SetThreadDpiAwarenessContext`。不要按主屏 DPR 统一换算所有屏幕。

## 窗口句柄

### `Process.MainWindowHandle` 可能是工具提示窗口

- **做法**：按窗口类名（Qt 6.12 为 `Qt6120QWindowIcon`）和可见性枚举进程的顶层窗口，不依赖 `MainWindowHandle`。

### 前台锁会阻止测试窗口抢到前台

- **现象**：测试调用 `activateWindow()` 后窗口仍不是前台窗口，依赖前台的断言失败，或者真实按键落到别的程序上。
- **原因**：Windows 只允许满足条件的进程设置前台窗口（最近接收过输入等）。
- **做法**：依赖前台的测试先等 `QTest::qWaitForWindowActive`，再在发送真实输入前确认前台窗口属于本进程，见 [桌面测试](desktop-testing.md#真实输入只能发给本进程的窗口)。
