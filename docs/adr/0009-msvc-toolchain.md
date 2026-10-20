# 0009 Release 改用 MSVC 工具链

**状态**：采用（取代 [0004](0004-windows-toolchain.md)）

## 背景

[0004](0004-windows-toolchain.md) 记录的是首个预览以来的实际做法：Release 和日常开发用 Qt 的 MinGW 套件，MSVC 只用于诊断构建。复核后续计划时发现，接下来要接入的东西大多偏向 MSVC：

- Windows 系统通知首选 WinRT（[0007](0007-windows-system-notifications.md)），MSVC 有官方的 C++/WinRT，MinGW 没有。
- libssh 及其加密依赖计划通过 vcpkg 构建，vcpkg 对 MSVC 的支持最完整，MinGW 的 triplet 由社区维护。
- ASan 诊断构建、Visual Studio 的性能分析与内存快照、PDB 调试符号和崩溃转储分析，在 MSVC 下都是现成的。
- Qt 在 Windows 上最主要的预编译套件是 MSVC。

两者的运行性能相近，不是这次选择的理由。Qt 6.12 官方支持的 Windows x64 编译器是 MSVC 2022 和 MinGW-w64 15.1（[支持平台](https://doc.qt.io/qt-6/supported-platforms.html)）。

## 决定

用户 2026-10-20 决定：

- Release、日常开发和全部自动化测试改用 Qt 6.12 的 **MSVC 2022 64-bit** 套件（`msvc2022_64`），编译器用本机 Visual Studio 2022 的 MSVC。Qt 6.12 对 x64 不支持 MSVC 2026，等 Qt 官方支持表中加入后再一起升级。
- 编译器版本必须不低于 Qt 套件构建时的版本（微软的二进制兼容规则：链接时用的工具集要至少和构建任何一个被链接二进制的工具集一样新）。`tools/toolchain.ps1` 从套件的 `mkspecs/qconfig.pri` 读出 `QT_MSVC_*_VERSION` 并核对，低于时报错。
- 构建脚本通过 `vswhere` 找到装有 x64 C++ 工具的最新 Visual Studio，载入 `vcvars64` 环境；Qt 套件中 MSVC 套件优先。MinGW 套件仍能识别和构建，保留到 MSVC 构建经过试用、用户卸载 MinGW 为止。
- 部署时把 Visual C++ 运行库 DLL 复制到程序旁边（app-local），试用者不用另装 vc_redist。
- 所有目标对 MSVC 加 `/utf-8`：源码是 UTF-8，MSVC 默认按系统代码页读源文件。

## 后果

- 需要安装 Visual Studio（C++ 桌面开发工作负载）和 Qt 的 MSVC 套件；Qt 不提供 MSVC 编译器。
- 诊断构建（实现计划 6.4）可以直接用 MSVC 的 `/fsanitize=address`，不再需要单独的 MSVC 版 Qt。
- WinRT 通知（8.7）、vcpkg 依赖（7.5）走 MSVC 的标准路线。
- 部署目录中的运行库从 MinGW 的三个 DLL 换成 Visual C++ 运行库；第三方声明相应更新。
- 中文字符串在没有 `/utf-8` 的目标里会悄悄变成乱码，而且不一定有编译警告；新增目标时必须带上（各目录 CMakeLists 末尾统一为本目录的编译目标添加）。
- 升级 Visual Studio 后编译器版本变化，按 §6.1 的升级流程完整验证。

## 替代方案

- **继续用 MinGW（0004）**：现状可用，但 WinRT、vcpkg、诊断与调试工具都要走旁路。
- **LLVM-MinGW 套件**：不在 Qt 6.12 官方支持表中，运行库与 MinGW 同属一类，上面的问题依旧。
- **clang-cl**：使用 MSVC 的 ABI 和标准库，能链接 MSVC 版 Qt；以后如需比较编译器优化，可以用基准程序实测后再决定，不影响这里的套件选择。

## 日期

2026-10-20 决定并记录。
