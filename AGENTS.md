# 仓库协作规则

本文件是所有助手（Claude、Codex 等）共用的唯一规则文件，`CLAUDE.md` 只引用它。这里只写每个助手都必须遵守的规则和常用命令，细节在链接的文档里，做相关工作前先读对应文档。

优先级：用户在当前会话中的明确要求 > 本文件 > 本文件链接的规范。公开仓库不泄露本机信息这一条任何时候都不放宽。

与用户交流一律用简体中文；代码、注释、提交说明按 [参与开发](CONTRIBUTING.md)。

## 常用命令

```powershell
pwsh tools/build.ps1 -Configuration Debug -Run                                           # 构建并启动
pwsh tools/tests/quiet-desktop.ps1 -Elevate pwsh tools/build.ps1 -Configuration Release -Test   # 全量测试
pwsh tools/tests/quiet-desktop.ps1 -Elevate pwsh tools/verify.ps1                         # 完整验证
pwsh tools/tests/quiet-desktop.ps1 -Elevate -Restore                                      # 测试被打断后恢复窗口
python tools/check-docs.py                                                                # 提交前检查文档
```

参数和其他入口见 [预览说明](docs/development/window-system-preview.md#开发环境) 与 [测试要求](docs/development/testing-design.md#3-工具与统一入口)。

## 当前范围

- 现阶段开发 WindowSystem 窗口系统和 Muxora 应用的首个预览：应用入口 `src/app/`，宿主内容接入 `src/workspace_host/`，后续产品继续用这些代码。不另建演示产品或示例应用。
- SSH、SFTP、Ghostty、X11 按后续阶段接入，不能把占位内容说成真实连接。暂不设计或添加应用 logo。
- `modules/window-system/` 是可独立构建和复用的模块，不依赖 Muxora 的业务服务；边界见 [通用窗口系统设计](docs/design/window-system-design.md)。

## 轮流开发与交接

Claude 和 Codex 按额度轮流在同一台电脑的同一个目录里开发，同一时间只有一个在工作。Claude 主导规划、架构决定和复杂排查，Codex 按交接记录执行指定步骤。新增或改变界面外观的工作（新的设置项、控件、图标、配色、布局、动效）由 Claude 做（用户 2026-10-24 决定）；Codex 的步骤里遇到需要新外观的部分，停下来写进交接记录，不自行设计。双方看不到彼此的对话和私有记忆，**仓库是唯一的交接依据**，入口是 [交接记录](docs/development/handoff.md)。

- **接手时**：读交接记录最上面一节和其中的链接，看 `git status` 和最近的提交；中途被打断留下的未提交改动先弄清属于哪一步，不直接丢弃。构建一次，跑交接记录提到的相关套件，结果与记录不符时先写进交接记录。按 [文档维护规范](docs/development/documentation.md#接手时的审查) 审查上一位的提交：文档是否同步、有没有重复或冲突、文件是否放对位置、有没有遗留临时文件。
- **工作中**：小步推进，每完成一个可验证的小步就提交并更新交接记录，不留到最后；推送可以分批，最晚在一轮对话结束时全部推送。和用户新定的决定当轮写进仓库（ADR、设计文档、实现计划、本文件或 CONTRIBUTING），并列进交接记录“本轮新决定”。完成或暂停一个步骤时，按 [文档维护规范](docs/development/documentation.md#改了什么同步更新什么) 更新步骤状态、当前位置、验证日志和预览说明。
- **不直接联系另一位助手**：工作中不自行向另一位助手提问、请它评审或发消息：不用命令行调用它（例如 `claude -p`、`codex exec`），不用会话之间的消息工具，也不打开它的会话。这类调用没有完整上下文、用户看不到，回答也不会留在仓库里。自己启动的子代理不在此列。需要另一位回答或评审时：
  - 能等到下次交接的，写进交接记录的“请下一位回答”，写清问题、相关材料的位置和需要的答复形式，并在对话中提醒用户及时安排交接；
  - 等不了的，暂停这一步，在对话中把问题整理成可以直接转发的提示词交给用户，由用户发给另一位并把回答带回；
  - 收到的回答和据此做的决定，写进交接记录（属于验证的写进验证日志）。提问和答复的格式见 [交接记录 · 助手之间的问答](docs/development/handoff.md#助手之间的问答)。
- **交出时**：改过的入口脚本按文档写的方式至少跑通一次；验证结论写清每个数字来自哪次运行、哪个提交。工作区干净，提交已推送，临时文件已删除；交接记录最上面一节按其格式写好；在对话里告诉用户做完了什么、验证到什么程度、下一步是什么。

## 必须遵守

- **问用户**：会改变用户能看到的界面、交互、默认设置，或要偏离计划、改架构时，先问用户；用户不在时写进交接记录“待用户决定”。实现达不到设计或 ADR 的要求时也一样，不能把限制写成“用法”。只影响内部的改动自行评审，提交说明写清楚即可。向用户说明时用用户能理解的说法。
- **验证**：未执行、失败、跳过或受环境阻碍的检查不能写成通过。不为通过测试删用例、改预期、放宽阈值或屏蔽诊断；测试确有错误时按 [测试要求 §1](docs/development/testing-design.md#1-必须遵守的规则) 处理。修复用程序验证；computer use 只用来观察自动化无法复现的现象，真实试用留给用户，没试用的如实记录。
- **桌面测试**：桌面测试和全量测试一律经 `quiet-desktop.ps1 -Elevate` 整批以管理员权限运行，不先用普通权限试（用户 2026-10-21 决定）；脚本选项写在被测命令之前。规则见 [测试要求 §3](docs/development/testing-design.md#3-工具与统一入口)。
- **提交**：改动完整、相关验证通过就自主提交。一个提交只做一件事，提交前核对暂存内容和受影响的文档，格式见 [参与开发](CONTRIBUTING.md#提交改动)。禁止 merge，只用 rebase 或快进；改写已推送的历史要用户明确要求；不丢弃用户的改动。
- **文档**：每次提交都让文档反映最新状态，每类信息只写在 [文档维护规范](docs/development/documentation.md) 规定的位置，别处只链接。修复缺陷或排查花了明显工夫时，在同一提交里写 [开发经验](docs/development/lessons/README.md)。对外文字按本地 `shuorenhua` skill 处理（代码、日志、配置和命令输出除外）。
- **配置格式**：只维护当前格式，不保留旧配置兼容层；范围和必需校验见 [工程设计 §2.4](docs/development/engineering-design.md#24-当前配置格式)。
- **公开仓库**：文档、注释、经验和提交说明里不写本机的具体配置（PATH、已装软件、用户名、私人路径），只写通用描述；用户本机环境的问题在对话中告诉用户怎么改，不在仓库里加规避代码。
- **临时文件**：只为某次排查写的脚本、状态目录和提交说明草稿放系统临时目录；测试报告和排查文件按 [测试结果与排查文件](docs/development/testing-design.md#测试结果与排查文件) 存放，证据写进验证日志，`out/diagnostics/<主题>/` 在主题结束时删除。想保留诊断脚本先问用户。
- **部署和打包**：Release 预览固定覆盖 `out/package/muxora-preview/`，保留其中的 `data/` 和未知文件，程序被占用时正常关闭，不强杀。用户目前不需要便携包，不主动运行打包；需要时用 `tools/package-portable.ps1`，结束后清理暂存。不例行计算文件校验值。
- **文件操作**：搜索优先用 `rg`。Windows 上移动或递归删除前先核对绝对路径在预期目录内，用 PowerShell 的 `-LiteralPath`。
- **真实 SSH 测试**（阶段 7 起）：目标由用户提供，第一次用到时询问地址和账号；不改远端系统设置；优先用 `ssh -F` 指向临时配置，必须改 `~/.ssh/config` 或远端文件时先备份，结束后（包括失败时）逐字节恢复并核对。

## 详细规范

| 做什么 | 先读 |
| --- | --- |
| 写 C++／Qt 代码、加依赖、组织目录 | [工程设计](docs/development/engineering-design.md) |
| 做界面 | [界面设计规范](docs/design/ui-design-system.md)、[主题系统设计](docs/design/theme-system-design.md) |
| 写或跑测试 | [测试要求](docs/development/testing-design.md) |
| 改文档、加文档、提交前检查 | [文档维护规范](docs/development/documentation.md) |
| 提交 | [参与开发](CONTRIBUTING.md) |
| 选下一步、看步骤要求 | [实现计划](docs/roadmap/implementation/README.md) |
| 其他文档 | [文档入口](docs/README.md) |
