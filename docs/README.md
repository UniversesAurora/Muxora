# 文档入口

Muxora 当前进入窗口系统开发阶段。首个预览就是 Muxora 应用，后续在同一套代码上继续开发；窗口模块的独立复用由测试夹具验证。设计文档描述最终行为与约束，不能据此判断功能已经完成；实际预览范围和验证结果见 [窗口系统预览说明](development/window-system-preview.md)。

## 设计

| 文档 | 内容 |
| --- | --- |
| [V0 总览](design/v0-design.md) | 产品范围、技术选型、业务流程与阶段验收 |
| [通用窗口系统](design/window-system-design.md) | 独立模块、公开 API、窗口壳、主题与 DPI |
| [工作区与导航](design/workspace-design.md) | 四边栏、中央标签组、标签内窗格与导航 |
| [浮动与完整窗口](design/floating-window-design.md) | 两种窗口、窗口族、整窗拖拽与跨窗口合并 |
| [设置与命令](design/settings-and-commands-design.md) | JSONC、图形设置、命令面板与快捷键 |
| [终端 Normal / Visual 模式](design/terminal-mark-mode-design.md) | 本地键盘浏览、选择、搜索与复制 |

## 开发

| 文档 | 内容 |
| --- | --- |
| [工程、资源与依赖](development/engineering-design.md) | 所有权、模块目录、资源约束、SDK 与依赖升级 |
| [自动化测试要求](development/testing-design.md) | 测试链路、场景矩阵、证据与失败处理 |
| [Muxora 窗口系统预览](development/window-system-preview.md) | 当前实现范围、构建、运行、快捷键试用与验证记录 |
| [WindowSystem 模块入口](../modules/window-system/README.md) | 模块构建与宿主使用方式 |

设计文档使用小写英文加连字符命名。产品与接口设计放在 `design/`，开发规范和操作说明放在 `development/`；后续需要长期记录的架构决定放在 `adr/`。引用仓库文件使用相对路径，机器 SDK 路径只作为本地示例出现。
