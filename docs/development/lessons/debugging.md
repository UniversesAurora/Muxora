# 调试方法

## 界面状态异常

### 枚举原生子窗口

窗口出现绘制、悬停或命中测试异常且重启才恢复时，先检查是否出现了原生子窗口。正常情况下 Qt Widgets 窗口只有一个顶层 HWND，子控件都没有句柄。

- 进程外：用 `EnumChildWindows` 枚举顶层窗口的子窗口，记录类名、可见性和矩形。出现一串 `Qt6120QWindowIcon` 子窗口就说明有控件被原生化了。
- 进程内：遍历 `findChildren<QWidget*>()`，找出 `!isWindow()` 且 `internalWinId()` 非 0 或带 `Qt::WA_NativeWindow` 的控件。

### 用调试器抓某个事件的调用栈

想知道“是谁让这个控件变成了原生窗口”这类问题时：

1. 在实验测试里装一个应用级事件过滤器，遇到目标事件（这里是非窗口控件的 `QEvent::WinIdChange`）时打印对象，并在第一次出现时调用 `DebugBreak()`。
2. 用 Visual Studio 调试器运行，或用 Windows SDK 调试工具中的 `cdb`：
   ```
   cdb -g -c "k 40; q" <测试程序> <测试函数名>
   ```
3. `DebugBreak()` 处停下后 `k` 打印调用栈，往上找第一个属于本项目的帧。Qt 安装时勾选了 MSVC 套件的调试符号（Qt Debug Information Files），调用栈能显示 Qt 内部函数名和源码行；Release 构建需要带 `/Zi` 和 `/DEBUG` 才有本项目的源码行。

当时（2026-10-20，MinGW 构建）用的是 Qt 自带的 gdb，调用栈直接指向 `preparePopupFrame()` 在 `aboutToShow` 里调用的 `winId()`。项目改用 MSVC（[ADR 0009](../../adr/0009-msvc-toolchain.md)）后 gdb 读不了 MSVC 的调试信息，MinGW 工具也已卸载，改用上面的方法。

### 把真实操作改写成可重复的实验

用 computer use 复现一次之后，不要靠它反复验证：它慢，而且会占用使用者的桌面。改为在测试里用模块 API 按同样的顺序操作，逐步打印状态；能复现后再删减步骤，找出必需的条件，最后整理成回归用例。实验代码不提交。

### 测试进程崩溃时看标准输出里的调用栈

- **现象**：用例跑到一半进程退出（退出码 -1073741819，访问冲突），`-o 文件,txt` 的输出停在某一行，之后的 `qDebug` 也没有，看不出崩在哪。
- **原因**：QtTest 写文件的输出在崩溃时没有全部写出；它在 Windows 上捕获崩溃后把调用栈打印到标准输出，而不是写进 `-o` 指定的文件。
- **做法**：崩溃时不加 `-o`，把标准输出和标准错误一起重定向到文件（PowerShell 用 `*>`），看末尾的 “Exception address” 和调用栈；本机没有调试器时这是最快的定位办法。
- **出处**：2026-11-06，10.4i，`QuickDialogsTest::severalLinesAndRaising`，见 [记录](../verification-log/2026-11.md#quick-对话框2026-11-06)。
