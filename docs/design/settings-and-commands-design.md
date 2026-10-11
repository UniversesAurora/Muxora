# Muxora 设置、会话存储与命令设计

版本：0.2，讨论稿\
日期：2026-10-11\
状态：V0 设计要求；首个预览的命令、设置和快捷键实现范围见 [预览说明](../development/window-system-preview.md)。完整配置服务及编辑能力按本规范继续实现。

关联：[V0 设计](v0-design.md)、[工作区设计](workspace-design.md)、[终端 Normal 与选择模式](terminal-mark-mode-design.md)、[测试规范](../development/testing-design.md)。

## 1. 参考范围

设置和快捷键参考 VS Code：可直接编辑的 `settings.json`、`keybindings.json`，统一配置定义、默认值、搜索、校验、上下文和命令 ID。命令面板也参考 Windows Terminal 的搜索及动作入口。窗口视觉继续参考 Visual Studio 2019 之后的设计，不因配置借鉴 VS Code 而改变。

VS Code 的用户设置使用 JSONC，允许注释和尾逗号；窗口位置和工作台运行状态由独立状态／存储服务管理，并非都写进 `settings.json`。Muxora 借鉴这种职责分离，文件组织按本项目需要设计。[VS Code 设置](https://code.visualstudio.com/docs/configure/settings)、[JSONC](https://code.visualstudio.com/docs/languages/json)、[窗口状态源码](https://github.com/microsoft/vscode/blob/main/src/vs/platform/windows/electron-main/windowsStateHandler.ts)

配置和交互使用原生 C++／Qt 实现，不为模仿设置体验引入 Electron、Monaco 或 JavaScript 运行时。

## 2. 文件划分

**全局设置集中在一个文件，自建会话一会话一文件，快捷键和机器运行状态另存。** 不把所有内容堆进一个大 JSON，也不把每个设置项拆成一个文件。

```text
<UserConfigRoot>/
├── settings.json                 # 全局用户设置，JSONC
├── keybindings.json              # 用户快捷键覆盖，JSONC
├── sessions/
│   ├── catalog.json              # 分组、顺序等目录元数据，带版本的 JSON
│   └── items/<session-id>.json    # 每个自建会话，JSONC
└── ssh-alias-overrides.json       # 仅别名附加设置，JSONC

<LocalStateRoot>/
├── window-layout.json            # 窗口及工作区恢复，带版本的严格 JSON
├── runtime.json                  # 少量最近操作及界面状态，严格 JSON
└── transfers.json                # 有界任务记录，严格 JSON
```

Windows 用户配置目录建议为 `%APPDATA%\Muxora\User`，机器状态目录为 `%LOCALAPPDATA%\Muxora\State`。由路径服务通过 Qt 标准路径能力求值，不硬编码用户名或盘符；Windows 可以用 `AppDataLocation` 的 roaming 路径承载用户文件，状态使用 `StateLocation`。后续 macOS／Linux 使用对应标准配置及状态位置，公共服务只接收解析后的根目录。[QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html)

默认值由应用内配置定义提供，命令可打开只读的默认设置及默认快捷键视图，不复制一整套默认值到用户文件。缓存、日志和临时文件不混入用户设置；导出用户配置时可独立选择会话，不默认携带机器窗口坐标或任务历史。

SSH config 仍是外部只读、动态来源，不复制到 `sessions/items/`。这些目录是安装后的用户数据位置，与源码中的 `src/sessions/` 等模块目录不同。

## 3. 全局设置与编辑

每个设置有稳定 ID、类型、默认值、说明、分类、允许范围、作用域和生效方式。设置界面与 JSON 编辑入口使用同一配置定义，支持搜索、查看修改项、恢复某项默认值、打开 JSON 与默认设置。键盘可以完成这些操作。

用户文件只保存明确的覆盖值。以下键名为设计示意，最终 schema 在实现时固定：

```jsonc
{
  // 字体按顺序回退；仍采用逗号分隔的列表形式。
  "terminal.fontFamily": "JetBrains Mono, Sarasa Mono SC, monospace",
  "terminal.fontSize": 12,
  "workbench.colorTheme": "muxora-violet",
  "window.restoreLayout": true,
  "window.tabDragOutsideMode": "floating",
  "window.emptyWorkbenchAction": "keep",
  "window.dragReveal.enabled": true,
  "window.dragReveal.delayMs": 500,
  "terminal.markMode.defaultKeymap": "vim",
}
```

`window.restoreLayout` 控制是否恢复，实际窗口尺寸、标签和分割树写入状态文件。设置项不包含运行时句柄、当前鼠标位置、选区正文或当前终端 Normal 模式状态。

窗口行为设置必须有同等的图形入口：拖出类型 floating／workbench、完整空窗口 keep／close、拖拽悬停唤起开关及 200–2000 ms 延迟。搜索、修改状态、说明、恢复默认及 JSON 编辑共用 schema。判空包含整个窗口族，仍有从属浮窗不能自动关根；具体默认及生效时机见 [双窗口设计](floating-window-design.md)。

JSONC 支持 `//`、`/* ... */` 和尾逗号，不扩大为任意 JSON5 或可执行脚本。解析采用支持这些语法的成熟原生组件；Qt 严格 JSON 解析器不能单独承担此入口。Boost.JSON 的相应解析选项可作为候选，但其语义解析不自动解决注释保留编辑，必须分别验证。[Boost.JSON 解析选项](https://www.boost.org/doc/libs/latest/libs/json/doc/html/ref/parse_options.html)

配置文本与有效值分开处理。图形设置编辑采用针对目标字段的源码范围修改，保留已有注释、无关字段、顺序和格式，不解析后全量序列化覆盖用户文件；不能用正则剥注释。JSON 编辑提供语法、类型、未知键、重复键和范围诊断，以及从 schema 生成的键／值提示，不要求嵌入完整代码编辑器。

## 4. 作用域、加载与写入

V0 的显示／交互设置层次为：内置默认 → 全局用户设置 → 会话允许的设置。基本值和数组整体覆盖；定义为可合并的对象按 schema 递归合并，不把任意对象隐式当作连接参数。层次和来源在设置详情中可查看。

每个设置的 schema 明确能否用于会话。全局行为不接受会话覆盖；终端字体、颜色、光标和文件布局等允许按会话调整。**这些覆盖规则不作用于 SSH config 的 HostName、User、Port、IdentityFile、代理或认证参数。** 自建会话的连接字段由自己的会话文件管理，SSH config 会话的连接身份由原来源和成熟后端求值。

多工作区设置及独立用户 profile 留有作用域扩展位置，V0 不自动读取服务器目录里的配置来改变本地快捷键或执行程序。快捷键 V0 使用一份全局用户规则，通过上下文区分会话／视图；后续方案引用也使用同一解析器。

启动及运行中监听文件和父目录，合并事件并处理原子替换。配置读取、解析和 schema 校验成功后发布有效快照，按实际变化通知使用方，不重建所有终端和窗口。未知键保留并诊断但不执行；重复键或无效值不悄悄采用“最后一个”。

编辑中的损坏文件保留原文，报告位置，继续使用最近有效快照；首次启动没有有效快照时使用内置默认值，并显示配置错误。不能自动覆盖用户文件来“修复”。不能把损坏的 SSH 来源或会话连接文件按显示设置的回退规则处理，继而用旧参数建立新连接。

写入前复核文件版本／内容哈希，外部变化后重新加载并合并不冲突修改；同一字段冲突明确呈现，不能覆盖另一编辑者。V0 同一配置根的多个窗口共享一个应用进程、配置服务与写入者，再次启动将打开窗口的请求转交该进程；独立数据根可使用独立实例。文件锁与版本复核共同防止写入竞争，锁不替代对外部编辑器变化的检测。[QLockFile](https://doc.qt.io/qt-6/qlockfile.html)

采用 `QSaveFile` 原子替换，写入失败不发布虚假的成功状态。短时间内的机器状态变更合并写入，未改变内容不重复写；内存只持有必要文本、当前有效快照和有限诊断，不保留无限配置历史。[QSaveFile](https://doc.qt.io/qt-6/qsavefile.html)

## 5. 会话拆分与删除

自建会话使用稳定 UUID 文件名，显示名和分组可以变化，不以服务器地址或显示名作为文件名。单文件包含 schema 版本、ID、连接定义和允许的会话设置；一项损坏不影响其他会话。密码和私钥口令使用凭据库引用，不写明文。

`sessions/catalog.json` 管理分组定义、会话顺序等目录元数据，不再保存第二份连接定义。会话文件是事实源，目录索引可增量重建；重复 ID 报错，损坏 catalog 不删除正常会话。新建先可靠写入会话再更新目录，删除先移除对应会话，再清理目录引用；跨文件操作有可恢复的明确顺序，不能把多次独立写入宣称为一个原子事务。批量导入逐项报告结果。

SSH 别名附加设置先集中在 `ssh-alias-overrides.json`，以“来源 ID + 原始别名”关联。集中存储便于一次原子提交删除及防止旧任务误删新条目；无需为每个动态别名复制配置文件。记录只允许显示名、分组、收藏、终端和文件偏好等字段。

完整有效刷新确认别名删除时，当前目录立即移除并原子提交附加设置删除；清理与同名重建按记录 ID／代数区分。临时保存中间态或来源读取错误不触发删除。旧目录、布局和备份不能自动复活已删除别名的设置；重新出现的同名别名使用新的默认设置。活动连接可以继续，不能据旧连接快照恢复或重连。

会话拆分粒度可以以后调整，但所有 UI 通过 `SessionStore` 使用，不直接操作文件。共享设置由 `SettingsService` 提供有效值和来源；两者不承担 SSH 协议解析。

## 6. 窗口与运行状态

`window-layout.json` 保存 schema 版本、WindowId、kind、FamilyId 与完整根映射、窗口正常几何及最大化状态、屏幕信息、四边栏、中央标签组、PaneTree、活动标签／窗格、条带位置和锁定状态。引用稳定内容 ID 及必要恢复描述，宿主业务状态与通用布局分开，网络句柄和终端历史不序列化。

两种窗口及族归属进入同一完整快照；旧版窗口迁移为一窗口一族，缺根浮窗按恢复规则补根，不丢内容。不保存 OS 层叠顺序、前台权限、悬停计时或半次拖拽。浮窗与完整窗口的升降级、合并都只更新布局状态，不改用户设置。

恢复时按当前显示器和 DPI 重算可见几何，屏幕移除后将窗口放回可见区域；不能把旧物理像素直接当当前逻辑坐标。关闭最后窗口后的状态、异常退出和“恢复默认布局”都定义明确结果；正常退出等待有界提交并报告失败。

结构恢复与远程连接恢复分开：恢复标签不自动保证远端 shell 仍存在。会话及 SSH 别名须重新检查当前来源，删除后的配置不因旧布局重建。瞬时选区、待完成快捷键序列和 Normal 模式状态不跨应用重启恢复。

`runtime.json` 只保存有界的命令最近使用及必要界面状态；`transfers.json` 保存可展示的任务记录和真实恢复条件，不把中断任务标成已完成。运行状态格式允许迁移及重建；损坏时给出可理解的提示，并恢复到可用默认状态，不覆盖用户 settings。

这借鉴 VS Code 独立状态服务、合并写入和持久化职责，不复刻其 Electron 实现或全部存储数据库。[VS Code 状态服务](https://github.com/microsoft/vscode/blob/main/src/vs/platform/state/node/stateService.ts)

## 7. 一个命令目录驱动全部入口

**每项公开用户操作先定义命令，再接入菜单、工具条、右键菜单、命令面板和快捷键。所有公共命令都可绑定快捷键，包括没有默认按键的命令。** 参数缺失时进入键盘可操作的选择器／对话框；不能因为需要选择服务器、目录或目标位置就取消快捷键入口。

| 覆盖领域 | 命令范围 |
| --- | --- |
| 设置与应用 | 打开设置／JSON／默认值、快捷键编辑、命令面板、布局恢复、字体、主题和条带布局。 |
| 会话与连接 | 搜索、分组、收藏、新建自建会话、打开终端／文件、刷新来源、连接、取消、断开和诊断。 |
| 工作区 | 两种新窗口、族内／全部／仅完整窗口切换、返回所属根、升降级及改归属、单标签／整窗移动、拖拽目标选择、空窗口行为、四边收纳／固定、外层分割／大小／焦点。 |
| 窗格 | 内部分割、大小、方向焦点、最大化、关闭与提取为窗口。 |
| 终端 | 复制、粘贴、查找、滚动、字体调整、Normal 模式、移动／选择／端点／计数与查找动作。 |
| 文件与传输 | 导航、筛选、选取、创建／重命名／删除、上传／下载、双栏复制、目录跟随、任务取消和按实际能力重试。 |
| X11 与状态 | 组件配置、启用转发工作流、查看诊断／任务／通知等公开操作。 |

每个命令有稳定且带命名空间的 ID、标题、说明、参数 schema、可用前提和处理者。低层回调、内部资源释放等不是用户操作，不需要公开为可配置命令。命令覆盖清单随功能审查，不能只为少数按钮添加快捷键。

WindowSystem 提供通用命令注册、上下文及路由 API 和自己的窗口命令；Muxora 注册 SSH、终端和文件命令。宿主注入设置及用户按键规则，窗口模块不读取 Muxora 配置目录、不依赖其业务设置服务。

命令执行器统一校验参数、目标与前提。`when` 控制某个入口是否显示／匹配，命令前提决定实际能否执行；修改快捷键不能绕过前提。错误与取消有明确结果，菜单和快捷键调用同一处理者。[VS Code 命令及菜单定义](https://code.visualstudio.com/api/references/contribution-points)

命令面板默认 `Ctrl+Shift+P`，按标题、分类、别名和 ID 搜索，显示快捷键及有界最近使用。键盘可完成参数选择和执行。打开时捕获来源 Window／Group／Tab／Pane ID；面板获得焦点后仍操作原目标，执行前重新验证目标存活，不能误操作另一终端。[Windows Terminal 命令面板](https://learn.microsoft.com/en-us/windows/terminal/command-palette)

窗口命令通过参数表达 `scope: family / all / workbenches`、新建 `kind: floating / workbench` 和中心／方向／边缘落点；缺参显示键盘选择器。`window.promote/demote`、`window.reassignFamily`、`window.moveAllTabs`、`window.redock` 和 `window.drag.selectTarget` 等均在命令目录中。具体 ID、默认键和焦点规则见双窗口设计；标题栏与鼠标拖拽复用其布局事务。

## 8. VS Code 风格快捷键

用户规则使用 `key`、`command`、可选 `args` 和 `when`。内置默认规则只读，用户规则可以增加、覆盖或解除默认绑定；规则在运行中更新。多键序列用同一 `key` 字符串表达，例如 `ctrl+k ctrl+s` 或 Normal 模式的 `g g`。[VS Code 快捷键](https://code.visualstudio.com/docs/configure/keybindings)

```jsonc
[
  { "key": "ctrl+shift+p", "command": "workbench.showCommandPalette" },
  { "key": "ctrl+k ctrl+s", "command": "preferences.openKeyboardShortcuts" },
  {
    "key": "escape",
    "command": "keybindings.cancelPending",
    "when": "keybindingSequencePending || terminalMarkCountPending",
  },
  {
    "key": "ctrl+shift+m",
    "command": "terminal.markMode.enter",
    "when": "terminalFocus && terminalMode == 'remoteInput' && !textInputFocus && !imeComposing",
  },
  {
    "key": "g g",
    "command": "terminal.markMode.move",
    "args": { "motion": "bufferStart" },
    "when": "terminalFocus && terminalMarkMode && !textInputFocus && !imeComposing",
  },
  {
    "key": "alt+left",
    "command": "pane.focus.left",
    "when": "workspaceFocus && !textInputFocus && !imeComposing",
  },
]
```

示例命令和参数是拟定契约，具体默认键位在完整冲突检查后固定。为解除默认绑定提供 `-commandId` 规则和图形操作：匹配命令 ID 与键序列，提供 `when` 或 `args` 时进一步匹配规范化条件或参数；未提供时移除该 ID／序列的全部默认规则，不影响同命令的其他按键。图形删除操作生成精准移除规则，参数化移动命令不会因为删除 `j` 而丢失所有方向绑定。

用户层优先于默认层，同一层从后向前排序。一次按键按此顺序选择第一个上下文匹配的完整绑定或序列前缀：完整绑定获胜立即执行，前缀获胜进入等待。因此 `g` 与 `g g` 的竞争由明确顺序决定，并在编辑界面提示；超时不会自动执行较低优先级的单键命令。

V0 `when` 支持上下文键、布尔值、`!`、`&&`、`||`、括号及相等／不等比较，不运行 JavaScript；进一步表达式按需求增加并给出诊断。上下文至少包括窗口／组／标签／窗格焦点、内容类型、连接状态、`textInputFocus`、`imeComposing`、`terminalMode`、`terminalMarkMode`、`terminalVisualMode`、`keybindingSequencePending`、`terminalMarkCountPending`、选区、文件能力及平台。[VS Code 上下文机制](https://code.visualstudio.com/api/references/when-clause-contexts)

窗口相关上下文包括 `windowKind`、`windowFamilyId`、`hasOwnerWindow`、`windowDragActive`、`windowHasTabs`。拖拽时命令限于当前操作，原生移动循环的取消也交同一控制器，不将按键发给终端；悬停抬前不等于内容焦点变化。形态转换及显式窗口切换恢复对应实例焦点，不改变终端模式。

连续按键进入等待状态，显示待完成序列。提供公共命令 `keybindings.cancelPending`，默认 Esc 在存在序列／计数时执行它，优先于模式退出；这次只取消等待，下一次 Esc 才按当前模式规则执行。取消键同样可以重绑。超时、焦点变化、IME 开始、规则更新或目标关闭自动取消，属于生命周期处理。

待完成状态中先按同一优先级检查可配置取消命令的单次按键绑定，再匹配已开始序列的续键；取消不要求拼成 `ctrl+k escape` 一类续键。普通完整命令不会在无效续键后悄悄执行。取消键的重绑和这一路由均须覆盖自动化测试。

无效续键取消并消费当前序列，不将续键重新作为一次独立输入执行，也不执行较低优先级的命令；显示取消原因。已消费的前缀、续键和取消按键不能补发 SSH 或延迟发送给另一会话。没有待完成序列时，远端输入上下文中未消费的普通输入正常进入终端编码；Normal／Visual 中未绑定键仍留在本地，不回退 SSH。需要直达远端的组合键可以解除本地规则。

Normal 和三个 Visual 状态中的数字计数是这些模式限定的有界前缀，注入支持 `count` 的公共移动／选择命令，Visual 中 `5j` 可以扩选。数字追加通过 `terminal.markMode.appendCount` 等公共命令绑定；`0` 无计数时行首、有计数时追加零，由上下文规则表达。计数不在 `remoteInput` 或 `findInput` 生效。同一个解析器处理 Vim 单键和多键规则，不另建不可修改的 Vim 按键引擎。打开搜索框或命令面板后，普通字符和输入法优先输入控件，不被 `hjkl` 等规则抢走。

快捷键界面可搜索全部公共命令、录入按键、编辑条件和参数、查看默认／用户来源、解除／重置绑定，并显示同键与前缀冲突。提供可开启的分发诊断，记录匹配规则和目标 ID，默认不记录实际终端文本。匹配表在规则更新时编译，按键时不解析文件或线性扫描全部设置。

## 9. 自动化验收

- JSONC 注释、尾逗号、未知项保留和精确编辑；重复键、语法／类型错误、首次启动和最近有效快照。
- GUI 与直接编辑一致，作用域来源正确；会话外观可覆盖，全局设置不能覆盖 SSH config 连接身份。
- 原子替换监听、并发窗口、外部编辑冲突、只读文件、写入失败和损坏恢复；不丢失用户注释或悄悄写旧版本。
- 自建会话 UUID、改名、目录重建、单文件损坏与删除；SSH 别名删除／同名重建不复活旧设置。
- 所有公共命令可发现、可绑定、可通过参数或选择器使用；菜单、面板和按键行为一致，可用前提不能绕过。
- 用户覆盖、解除默认绑定、`when`、同键／前缀冲突、序列超时、焦点／IME 变化、参数校验和原目标关闭。
- Normal／Visual 键盘操作零远端按键泄漏；计数、多键、搜索输入和其他窗格互不污染。
- 窗口／布局恢复、混合 DPI、屏幕移除、关闭及异常退出；运行状态不写入 settings，状态损坏不破坏会话。
- 两种窗口的类型／归属／整窗移动和族内／全局导航均可绑定；GUI 与 JSON 的拖出、空窗口及悬停设置一致，拖拽中热更新按约定生效，取消键不穿透。
- 高频布局变化写入合并、无变化不写、配置快照及最近记录有界；文件变化不导致所有终端重建。

各项映射到有效用例并进入完整测试链路；失败修复问题后通过，不自行减少覆盖、改预期或放宽阈值。V0 只构建 Windows 应用，后续平台路径和键位扩展不宣称已验证。
