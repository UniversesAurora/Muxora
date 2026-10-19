# 内置编辑器设计

状态：设计稿，2026-10-20。实现步骤见 [阶段 5.2](../roadmap/implementation/phase-5-lua-tools.md#52-内置编辑器)。取代 [Lua 方案 §2](../roadmap/lua-plan.md#2-内置编辑器) 中“基于 `QPlainTextEdit` 自写”的方案。

## 1. 定位

一个统一的轻量编辑器控件，承担两类工作：

1. **写 Lua**：编辑 `User/init.lua`、`User/scripts/`、`User/lua/` 中的脚本。
2. **编辑配置文件**：像 VS Code 一样直接打开并编辑 `settings.json`、`keybindings.json`、主题文件（`User/themes/*.json`）、终端配置文件；编辑时能补全设置名和命令名、鼠标悬停显示说明、无效的设置显示为灰色并提示。

它不是完整的 IDE。只做基础编辑所需的功能（§4），把精力放在“对 Muxora 的配置和 Lua API 有用”的智能提示上（§5、§6）。

## 2. 选用的库

| 候选 | 结论 |
| --- | --- |
| **Scintilla + Lexilla**（Notepad++、Geany、SciTE 使用的编辑器内核） | **采用**。宽松许可（只要求保留版权声明），可以随 MIT 项目发布和收费；源码自带 Qt 控件 `ScintillaEditBase`，Scintilla 5.2.0 起兼容 Qt 6；内置语法着色、行号边栏、代码折叠、括号匹配、多选区、补全列表、提示框（call tip）、指示器（波浪下划线等）、鼠标停留事件（用于悬停说明）；Lexilla 提供 Lua 和 JSON 词法器，JSON 词法器可以开启注释高亮（适用于 JSONC） |
| QScintilla | 不用：GPL 或商业许可，与 MIT 发布冲突 |
| Monaco（VS Code 的编辑器）、CodeMirror | 不用：需要嵌入浏览器内核，单进程上百 MB 内存 |
| KTextEditor（KDE） | 不用：依赖整套 KDE Frameworks，体积大 |
| 基于 `QPlainTextEdit` 自写 | 原方案。补全、提示、指示器、折叠等都要自己做，工作量大且效果不如成熟内核 |

具体版本在实现时固定（与 `third_party/lua` 相同：`FetchContent` 加 `URL_HASH`），并在 `THIRD_PARTY_NOTICES.md` 和部署目录的许可证中加入 Scintilla 与 Lexilla 的许可。实现第一步先确认 `ScintillaEditBase` 在 Qt 6.12 + MinGW 下能编译；不能时在 ADR 中记录原因并回到自写方案。

## 3. 打开方式：标签页或弹窗

同一个编辑器控件，按入口决定放在哪里：

| 入口 | 打开方式 |
| --- | --- |
| “打开 settings.json”“打开 keybindings.json”、设置项齿轮菜单中的“在 settings.json 中编辑”、“工具 → Lua → 打开配置／新建脚本”、脚本管理器中的“编辑”、Lua 错误中的位置链接 | **工作区标签页**：和其他标签一样可以拆分、拖动、浮动；同一文件只打开一个标签，再次打开时切换到它并定位 |
| 快捷键编辑器中“编辑参数…”、Lua 控制台的多行输入、需要快速修改一小段 JSON 或代码的地方 | **弹窗**（统一对话框，非模态），关闭时询问是否保存修改 |
| Lua 自定义界面中的 `code` 节点（[Lua 扩展 API §5.2](lua-extension-api-design.md#52-怎么描述)） | 嵌在脚本的面板或对话框中 |

“在 settings.json 中编辑”的行为与 VS Code 相同：文件中已有该设置时跳到它；没有时在合适位置插入这个设置及其当前值并选中值，用户直接修改即可（使用 JSONC 局部编辑，保留其他内容和注释）。

## 4. 基础编辑功能

- 语法着色、行号、当前行高亮、括号匹配、缩进参考线、代码折叠；
- 撤销／重做、多选区（`Alt+点击`、`Ctrl+D` 选中下一个相同文字）、按行移动（`Alt+↑／↓`）、复制当前行、切换注释（`Ctrl+/`）；
- 查找／替换栏（`Ctrl+F`／`Ctrl+H`）：区分大小写、整词、正则；全部替换为一次撤销；显示“第 n 个，共 m 个”；
- 转到行（`Ctrl+G`）；
- 保存（`Ctrl+S`）、修改标记（标签标题上的 `●`）、关闭时询问未保存修改、外部修改提示重新加载；文件统一 UTF-8；
- 配色和字体跟随主题：编辑器的颜色使用主题颜色 ID（`editor.background`、`editor.foreground`、`editor.lineHighlightBackground`、`editorLineNumber.foreground`、`editor.selectionBackground`、`editorError.foreground` 等，以及语法颜色 `editorSyntax.keyword`、`editorSyntax.string`、`editorSyntax.comment` 等），深色主题下自动协调；编辑器字体单独设置（`editor.fontFamily`、`editor.fontSize`）。

这些快捷键只在编辑器获得焦点时生效（阶段 3 的 `when: editorFocus`），不与全局快捷键冲突。

## 5. 编辑配置文件时的智能提示

编辑器根据文件类型选择一个“语言服务”。`settings.json`、`keybindings.json`、主题文件各有一份由 Muxora 自动生成的结构描述（JSON Schema 的子集），内容来自程序本身，不会与实际功能脱节：

| 文件 | 结构描述的来源 |
| --- | --- |
| `settings.json` | 设置注册表（阶段 4.1）：每个设置的 ID、类型、默认值、枚举值及说明、范围、是否需要重启 |
| `keybindings.json` | 命令注册表（全部命令 ID、标题、参数说明）、按键名称、上下文键（阶段 3.1，用于 `when`） |
| 主题文件 | 颜色注册表（阶段 2）：全部颜色 ID 及说明 |

行为与 VS Code 一致：

| 功能 | 表现 |
| --- | --- |
| 补全 | 在对象中输入时列出可用的键（设置 ID、颜色 ID），每项带简短说明；输入值时列出枚举值、`true`／`false`、命令 ID、按键名称、上下文键；选中后插入完整的 `"键": 值` 并把光标放在值上 |
| 悬停说明 | 鼠标停在键或值上时弹出提示框：标题、说明、类型、默认值、当前生效值、是否需要重启 |
| 诊断 | 未知的设置 ID 显示为灰色并带提示“未知的设置”；类型或取值不对的值显示波浪下划线并说明原因；JSON 语法错误标出位置；已废弃的设置显示删除线并给出替代项 |
| 快速修复 | 例如把拼错的设置名改为最接近的已知名称（`Ctrl+.`） |
| 默认值查看 | “打开默认设置（只读）”“打开默认快捷键（只读）”以同一编辑器的只读模式打开，便于对照和复制 |

光标所在的 JSON 路径由阶段 3.6 的 JSONC 解析器得出（它保存每个节点的位置），再到结构描述中查找对应定义。解析在用户停止输入 200 毫秒后进行，大文件也只在后台线程解析，不卡输入。

## 6. 编辑 Lua 时的智能提示

| 功能 | 来源与表现 |
| --- | --- |
| 补全 | Lua 关键字、当前文件中的标识符、`muxora.*` API（来自 API 描述单一来源，阶段 5.4），带参数说明 |
| 悬停说明 | 停在 API 名称上显示参数、返回值和说明 |
| 参数提示 | 输入 `(` 后显示当前函数的参数列表（Scintilla 提示框） |
| 诊断 | 编辑停止 500 毫秒后在沙箱中只编译、不执行（`muxora._compile`），语法错误用波浪下划线标出 |
| 可选语言服务 | 已安装 lua-language-server 时，可在设置中开启，获得更完整的诊断和跳转（阶段 5.7） |

## 7. 按需加载

编辑器整体（Scintilla、Lexilla 以及 Muxora 的编辑器代码）编译为一个单独的 DLL（`muxora-editor.dll`），由应用在**第一次打开编辑器时**用 `QPluginLoader` 加载；从未打开过编辑器时，这个 DLL 不会被加载进进程。应用只通过一个很小的接口使用它：

```cpp
class EditorFactory {                       // 插件实现的接口，定义在应用侧头文件中
public:
    virtual ~EditorFactory() = default;
    virtual QWidget* createEditor(const EditorOptions& options, QWidget* parent) = 0;
};
```

- 结构描述（§5）在第一次需要时生成，并在注册表变化时失效，不在启动时生成。
- 编辑器标签或弹窗关闭时，编辑器控件、文档内容、补全列表和语言服务数据全部销毁；最后一个编辑器关闭后，结构描述等派生数据也释放。
- DLL 本身加载后不在进程运行期间卸载：插件中注册的 Qt 类型和静态数据在卸载后被访问会导致崩溃。留下的只是 DLL 的代码，长时间不用时 Windows 会把这些页面移出内存。
- 部署：DLL 放在程序目录，随部署清单和便携包一起分发。

## 8. 与其他设计的关系

- 编辑器快捷键依赖阶段 3 的上下文键和“内容优先按键”（阶段 3.5）。
- 设置页的“打开 settings.json”按钮（阶段 4.3）和快捷键页的“打开 keybindings.json”（阶段 3.8）都打开本编辑器。
- 查找栏、补全列表、提示框、对话框的外观遵守 [界面设计规范](ui-design-system.md)，颜色来自主题颜色 ID。
