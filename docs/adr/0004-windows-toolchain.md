# 0004 Release 用 Qt MinGW 工具链，MSVC 只用于诊断构建

**状态**：已被 [0009](0009-msvc-toolchain.md) 取代（2026-10-20）

## 背景

[工程设计 §6.1](../development/engineering-design.md#61-qt-sdk-的安装版本绑定与升级) 最初写的是“首选 MSVC 2022 预编译 Qt”。实际从首个预览开始，构建、测试和部署用的都是 Qt 官方安装器提供的 MinGW 套件：Qt 6.12.0 `mingw_64`，配套 MinGW GCC 15.1。两者都能用，但文档和实际不一致，而且两条路线在诊断构建上有差别：

- MinGW GCC 在 Windows 上没有可用的 AddressSanitizer。
- MSVC 有 `/fsanitize=address`（不含泄漏检测），但需要另装一套 MSVC 版 Qt，并且 MSVC 和 MinGW 编出的 C++ 库不能混着链接。

## 决定

- **Release 和日常开发用 Qt MinGW 套件。** 编译器必须是 Qt 套件自带、与套件同版本的 MinGW GCC：`tools/build.ps1` 从套件的 `mkspecs/qconfig.pri` 读出 GCC 版本，在 Qt 安装目录的 `Tools/mingw*` 中找同版本的 `g++`，找不到就报错退出，不使用其他版本凑合。CMake、Ninja 也取自 Qt 安装目录下的 `Tools`。
- **MSVC 只用于诊断构建**（ASan 等，见 [实现计划 6.4](../roadmap/implementation/phase-6-engineering.md)）：需要单独的 MSVC 版 Qt 套件和单独的构建目录，不参与 Release 预览，也不替代 MinGW 的全量测试。
- Qt 套件的位置按以下顺序查找：`-QtRoot` 参数、环境变量 `QT_ROOT`、不提交的 `.local/sdk.json`、仓库旁边的 `../sdk/Qt/<版本>/mingw_64`（取最新版本）。SDK 和个人路径不提交。
- 部署目录随程序附带 MinGW 运行库（`libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll`）及其许可证。

## 后果

- 构建只需要 Qt 官方安装器装好的一套东西，编译器版本由 Qt 套件决定，不会混入不兼容的 GCC。
- 升级 Qt 时编译器跟着变，需要按 §6.1 并行安装、单独构建目录验证、全部测试通过后再切换。
- 诊断构建要多装一套 MSVC 版 Qt；不能用 MinGW 版 Qt 直接加 `-fsanitize=address`。另一条可能的路线是 llvm-mingw，但它和 Qt MinGW 套件的 C++ 标准库是否兼容需要实测，见实现计划 6.4。
- 涉及 Windows 新 API 的功能（例如 WinRT 系统通知，见 [0007](0007-windows-system-notifications.md)）要先确认 MinGW 头文件是否可用，不可用时走 COM 接口或其他后备方案。

## 替代方案

- **全部改用 MSVC 版 Qt**：可以直接使用 ASan 和 Visual Studio 的诊断工具，但要换掉现有已验证的构建、部署和运行库，收益主要在诊断方面，用单独的诊断构建就能得到。
- **同时维护两套 Release 构建**：测试和部署成本翻倍，没有用户可见的收益。

## 日期

MinGW 路线自 2026-10-12 首个预览（`d94d7f4`）起实际使用；“MSVC 只用于诊断”在 2026-10-18 的工程审查中提出（[工程方案](../roadmap/engineering-plan.md)）；2026-10-20 记录并修正 §6.1。
