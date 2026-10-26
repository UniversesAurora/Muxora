# 参与开发

当前优先完善 WindowSystem 的公开 API、Muxora 应用预览和测试。这个预览就是后续产品的起点；应用入口与宿主内容接入分别位于 `src/app/` 和 `src/workspace_host/`。提出需求或修复时，请说明触发步骤、预期行为和实际结果；窗口问题同时记录 Windows 版本、Qt 版本、屏幕缩放及窗口布局。

## 开始修改

1. 阅读 [文档入口](docs/README.md)、[工程规范](docs/development/engineering-design.md)、[文档维护规范](docs/development/documentation.md) 和涉及的设计。
2. 按 [预览说明](docs/development/window-system-preview.md) 建立本地构建。SDK、编译器和产物留在仓库外或忽略目录。
3. 将改动限制在明确职责内。宿主使用公开 API，不引用窗口模块内部头文件；窗口模块不引入远程业务依赖。
4. 运行相关自动化测试；缺陷修复补充能够复现问题的用例。`pwsh ./tools/verify.ps1` 编排 Debug / Release 测试、模块独立构建、安装后的 C++／Lua 外部宿主验证与部署检查；它和其他含桌面测试的命令一样经 `pwsh ./tools/tests/quiet-desktop.ps1 -Elevate` 运行，见 [测试要求](docs/development/testing-design.md)。真实交互无法自动检查的部分记录复现步骤和实际验证结果。

## 提交改动

**一个提交只做一件事。** 判断标准：这个提交能不能用一句话说清，单独回退它会不会连带撤掉另一件无关的改动。

- 一件事的全部内容放在同一个提交：实现、测试、因它而改的文档、ADR 和开发经验。例如修复一个缺陷，连同它的回归测试和经验记录一起提交。
- 由这件事直接引起的必要改动也算同一件事。例如切换编译器后才暴露的测试写法问题，随切换一起提交。
- 顺手发现、与当前这件事无关的改动单独提交，即使只改一行文档。例如修缺陷时顺便更正另一份文档里的错误说法，应分成两个提交。
- 同一个文件里混有两件事的改动时，分次暂存，分别提交。
- 一次会话做了多件事时，按事情逐个提交；提交说明只写这一件事。
- 提交前用 `git diff --staged` 核对暂存内容只属于这一件事，并按 [文档维护规范](docs/development/documentation.md#提交前检查) 检查受影响的文档都已更新，运行 `python tools/check-docs.py`。
- 不提交 `out/`、SDK、本机路径、用户设置、凭据或只为某次排查写的临时诊断文件。

提交信息说明改动及原因，格式见下一节。公开接口和行为变化同步更新文档；长期影响的架构决定写入 `docs/adr/`，说明问题、选择与代价，不为每个局部实现建立决定记录。

### 提交信息格式

参考 [Linux 内核提交规范](https://www.kernel.org/doc/html/latest/process/submitting-patches.html#the-canonical-patch-format)：标题以“改动范围”开头，后接冒号和一句祈使句英文摘要，保持单行，尽量不超过 70 个字符，最多 75 个字符（含范围前缀）。

标题后空一行再写正文，说明行为变化、原因和实际验证。正文按单词在 75 列以内硬换行，段落之间空一行，不依赖展示页面的自动折行。`Fixes:`、`Link:`、`Signed-off-by:` 等标签行保持完整，不受正文折行宽度限制。

```text
window-system/chrome: Offer three caption button orders

Body: what changed for the user, why, and which checks actually ran.
```

| 前缀 | 范围 |
| --- | --- |
| `window-system` | `modules/window-system/` 整体或跨多个部分的改动 |
| `window-system/<部分>` | 只涉及其中一部分时使用：`model`（布局模型与事务）、`views`（标签组、分割、内容挂载等视图重建）、`chrome`（标题、工具条、窗口控制按钮、状态栏）、`frame`（Windows 原生窗口框架与命中）、`docking`（拖动、停靠与浮窗）、`tabs`（标签栏与标签列表）、`sidebar`（收纳条与临时面板）、`commands`（命令与快捷键）、`settings`（设置页）、`persistence`（布局保存与恢复） |
| `lua` | `modules/lua-runtime/`、`modules/window-system-lua/` 和 Lua 接口 |
| `app` | `src/app/` 与 `src/workspace_host/` |
| `terminal`、`ssh`、`sftp`、`sessions`、`x11`、`editor`、`theme` | 对应的业务模块（实现后启用） |
| `build` | CMake、构建、打包与部署脚本 |
| `test` | 测试基础设施、测试脚本与基准测试（某个模块自己的测试随该模块的前缀） |
| `third-party` | `third_party/` 中的依赖 |
| `docs/design` | 设计文档 |
| `docs/roadmap` | 规划与实现步骤文档 |
| `docs/dev` | 开发说明：工程规范、测试要求、预览说明 |
| `docs/manual` | 用户手册（以后） |
| `docs` | 跨多类文档或文档入口 |
| `repo` | 仓库规则与元数据：`AGENTS.md`、本文件、`.gitignore` 等 |
| `release` | 版本发布准备 |
| `treewide` | 涉及整个仓库、无法归入一个范围的改动 |

规则：

- 按改动的主要部分选一个前缀；代码改动连同它的测试和文档使用代码的前缀。两个范围同等重要时可写 `lua, window-system:`，不写三个以上，此时应拆分提交。
- 子范围只在改动确实限于该部分时使用，拿不准就用上一级。
- 新增业务模块时在上表中加入它的前缀，再开始使用。
- 不产生合并提交（见下文）。

评审说明应包括解决的问题、用户行为变化、验证命令和结果，以及尚未验证的范围。不得将未运行检查写为通过，也不得削弱测试标准以掩盖实现缺陷，详见 [测试要求](docs/development/testing-design.md)。

禁止使用 merge：整合分支和解决冲突只用 rebase 或快进（fast-forward），保持线性历史。贡献的自有代码和文档按仓库 MIT 许可证发布；引入第三方代码时保留来源与原许可，并核对依赖对分发的实际要求。

远程只推送 `main`，不创建或推送其他远程分支。可以使用本地工作分支，完成后以快进或 rebase 整合到 `main` 再推送；不把本地工作分支直接发布到远程。
