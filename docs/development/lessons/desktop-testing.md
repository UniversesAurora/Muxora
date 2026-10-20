# 桌面测试经验

测试规则见 [测试要求](../testing-design.md)；这里记录在真实桌面上跑测试时遇到的问题。

## 真实输入

### 真实输入只能发给本进程的窗口

- **现象**：跑 `window-system.toolbar-popup` 时，Claude Code 的当前任务和远程控制被中断。
- **原因**：测试用 `SendInput` 发送 Esc / Tab / 空格和鼠标点击，以复现原生弹窗的捕获和焦点恢复。`SendInput` 发给当时的前台窗口或指针下的窗口；测试窗口因前台锁没抢到前台时，Esc 落到了别的程序上。
- **做法**：
  - 桌面测试和全量测试用 `pwsh tools/tests/quiet-desktop.ps1 <命令>` 运行。它先记下所有可见的应用窗口并全部最小化（包括 Claude 等正在使用的程序），命令结束后在 `finally` 里按原来的层叠顺序和状态恢复；窗口列表存在 `out/test-results/quiet-desktop-windows.json`，运行被意外打断时用 `-Restore` 恢复，`-List` 只列出将被最小化的窗口。
  - 真实输入统一走 `modules/window-system/tests/native_input.hpp`。按键前检查前台窗口、点击前检查指针下的顶层窗口属于本测试进程，否则判测试失败，不发送。
  - 能用 Qt 合成事件验证的行为就不用真实输入。
- **出处**：2026-10-20。

### 不在测试里用真实输入做按下拖动

- **现象**：一次实验性测试用 `SendInput` 按下鼠标后进入系统移动循环，测试卡死，左键在系统里一直处于按下状态。
- **做法**：拖动用 Qt 鼠标事件或模块 API（例如 `moveStrip`、`setStripWidth`）驱动；如果已经卡住，用一次 `MOUSEEVENTF_LEFTUP` 松开按键。

## 环境

### 置顶窗口会挡住测试窗口

- **现象**：截图、像素和提示框断言随机失败。
- **原因**：桌面上有置顶窗口压在测试窗口上方；Qt 也会在窗口失去激活时立即关闭提示框。
- **做法**：用 `quiet-desktop.ps1` 运行，先把其他窗口最小化；提示框测试还要先激活自己的窗口，并关闭 `UI_FadeTooltip` / `UI_AnimateTooltip` 动画（测试结束后恢复）。

### 最大化后要等几何真正稳定

- **现象**：最大化后立刻读取按钮位置，得到的还是还原状态的坐标。
- **原因**：Qt 先报告最大化状态，原生尺寸调整稍后才完成。
- **做法**：`QTRY_COMPARE(window->geometry(), screen->availableGeometry())`，并等待关键控件到达工作区边缘后再断言。

## 菜单

### 用 `setActiveAction()` 驱动菜单悬停，不用合成的鼠标移动

- **现象**：用 `QTest::mouseMove` 或一串 `QMouseEvent` 悬停菜单项来打开子菜单，大约十次里有一次打不开，失败时菜单的 `activeAction()` 为空。
- **原因**：`QMenu` 在指针离开菜单弹出时的位置不到拖动距离、且移动不超过 6 次时忽略移动，而弹出位置取的是真实光标，所以结果取决于光标当时停在哪里；另外子菜单打开时，指针朝它移动会被推迟切换（sloppy submenu）。
- **做法**：测试悬停时调用 `QMenu::setActiveAction()`（`tests/menu_hover.hpp` 的 `hoverMenuItem`），它和悬停走同一条延迟弹出路径（`internalDelayedPopup`）。只有要测鼠标路由本身时才发鼠标事件。
- **验证**：两个原生子窗口回归用例改用它之后各连续 10 次通过。
- **出处**：2026-10-20。

## 回归用例

### 回归用例必须先在旧代码上失败

- **现象**：为原生子窗口问题写的第一版测试在修复前后都通过。
- **原因**：夹具和真实路径差了关键条件：菜单栏扩展按钮从未创建过，菜单也只打开了一次（见 [Qt Widgets](qt-widgets.md#qmenu-关闭后句柄会被销毁abouttoshow-比重新创建句柄更早)）。
- **做法**：写完回归用例后，用 `git stash push <修复文件>` 暂时撤掉修复，确认用例失败、失败信息指向问题，再恢复修复确认通过。测不出来时，把能复现的实验步骤逐步删减，找出真正必需的条件，而不是放宽断言。
- **出处**：2026-10-20，`CaptionStripTest::compressedMenuStripMovedUpKeepsTheWindowAlien`。
