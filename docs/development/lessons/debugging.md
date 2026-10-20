# 调试方法

## 界面状态异常

### 枚举原生子窗口

窗口出现绘制、悬停或命中测试异常且重启才恢复时，先检查是否出现了原生子窗口。正常情况下 Qt Widgets 窗口只有一个顶层 HWND，子控件都没有句柄。

- 进程外：用 `EnumChildWindows` 枚举顶层窗口的子窗口，记录类名、可见性和矩形。出现一串 `Qt6120QWindowIcon` 子窗口就说明有控件被原生化了。
- 进程内：遍历 `findChildren<QWidget*>()`，找出 `!isWindow()` 且 `internalWinId()` 非 0 或带 `Qt::WA_NativeWindow` 的控件。

### 用 gdb 抓某个事件的调用栈

想知道“是谁让这个控件变成了原生窗口”这类问题时：

1. 在实验测试里装一个应用级事件过滤器，遇到目标事件（这里是非窗口控件的 `QEvent::WinIdChange`）时打印对象，并在第一次出现时调用 `DebugBreak()`。
2. 用 Qt 自带的 MinGW gdb 运行：
   ```
   gdb -batch -ex run -ex "bt 40" --args <测试程序> <测试函数名>
   ```
3. Qt 的发布版 DLL 带导出符号，调用栈能看到 `QWidget::winId()`、`QWidget::setAttribute()` 等函数名，往上找第一个属于本项目的帧。

这次的调用栈直接指向 `preparePopupFrame()` 在 `aboutToShow` 里调用的 `winId()`。

### 把真实操作改写成可重复的实验

用 computer use 复现一次之后，不要靠它反复验证：它慢，而且会占用使用者的桌面。改为在测试里用模块 API 按同样的顺序操作，逐步打印状态；能复现后再删减步骤，找出必需的条件，最后整理成回归用例。实验代码不提交。
