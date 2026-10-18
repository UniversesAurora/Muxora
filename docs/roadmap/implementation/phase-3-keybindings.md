# 阶段 3：快捷键（上下文键、`when`、终端冲突、JSONC、编辑器）

**目的**：快捷键能力对齐 VS Code：按上下文条件启用绑定；内容（终端、编辑器）获得焦点时不被全局快捷键抢走按键；用户文件支持注释；编辑界面能看清来源、条件和冲突。语法和语义以 [设置与命令 §8](../../design/settings-and-commands-design.md) 为准。

**阶段约束**：

- 不带 `when` 的现有绑定，行为与现在完全一致（包括多段序列、前缀等待、参数、`runCommands`）。现有 `shortcut-dispatch`、`shortcut-settings` 测试不改预期。
- 表达式不执行任何脚本，只做求值；长度、深度、正则长度都有上限。
- 分发热路径保持 [审查记录](../code-review-2026-10.md#本轮修改) 中的“首键候选索引”：先按首键取候选，再求 `when`，不在每次按键时解析表达式。

---

## 3.1 上下文键服务

**目标**：任何命令分发时都能取到一组分层的上下文键值。

**涉及文件**：新建 `include/window_system/context_keys.hpp`、`src/commands/context_keys.cpp`；修改 `window_manager.hpp/.cpp`、`window_manager_p.hpp`、`modules/window-system/CMakeLists.txt`（加入源文件）；新测试 `tests/context_keys_test.cpp`。

**实现**：

1. 公开 API（`WindowManager`）：

   ```cpp
   // Global keys apply everywhere; pane keys apply while that pane is the
   // command target. Null QVariant removes a key. Names: [A-Za-z][A-Za-z0-9_.]*, ≤ 64 chars.
   bool setContextKey(const QString& key, QVariant value);
   bool setPaneContextKey(PaneId pane, const QString& key, QVariant value);
   // Resolved values for a target, for diagnostics, the keybinding editor and Lua.
   [[nodiscard]] QVariantMap contextKeys(const CommandContext& target) const;
   ```

   允许的值类型：`bool`、整数、浮点、`QString`、`QStringList`（供 `in` 使用）。其他类型返回 `false`。

2. **焦点链上的控件键**：内容控件可以设置动态属性 `windowSystemContextKeys`（`QVariantMap`）。解析时从当前焦点控件向上走到窗格内容根，合并沿途的属性，越靠近焦点的优先。终端以后只需在自己的控件上设置 `{"terminalFocus": true, "terminalMode": "remoteInput"}`。
3. **内置键**（求值时计算，不缓存）：

   | 键 | 值 |
   | --- | --- |
   | `windowKind` | `"workbench"` / `"floating"` |
   | `groupLocation` | `"central"` / `"pinned"` / `"autoHide"` |
   | `contentType` | 目标窗格描述中的 `TabOptions::contentType` |
   | `paneCount`、`tabCount`、`groupCount` | 目标标签的窗格数、目标组的标签数、窗口中央组数 |
   | `viewMode` | `"normal"` / `"windowFullscreen"` / `"groupFullscreen"` / `"paneFullscreen"` |
   | `zenMode`、`tabBarsVisible`、`sideBarsVisible`、`toolbarsVisible`、`statusBarVisible` | 布尔 |
   | `textInputFocus` | 焦点控件是 `QLineEdit`、`QTextEdit`、`QPlainTextEdit`（非只读）、可编辑 `QComboBox` 或 `QAbstractSpinBox` |
   | `keybindingSequencePending` | 正在等待续键 |
   | `dragActive` | 正在拖拽 |
   | `isWindows` | 平台 |

4. 解析顺序：内置键 → 全局键 → 窗格键 → 焦点链控件键，后者覆盖前者。窗格关闭时在 `refresh()` 删除窗格键的流程中一并清理（与 `d->descriptors` 一起删除）。
5. 容量：全局键与每窗格键各最多 256 个，超出返回 `false`。

**测试**（`context_keys_test.cpp`，widget 层）：内置键在不同目标下的值；全局、窗格、控件属性的覆盖顺序；窗格关闭后其键消失；非法名称和类型被拒绝；容量上限。

**验证**：新套件、`window-system.shortcut-dispatch`。

**状态**：未开始。

---

## 3.2 `when` 表达式解析与求值

**目标**：独立、可单测的表达式模块。

**涉及文件**：新建 `src/commands/when_expression.{hpp,cpp}`（内部头文件，不公开），新测试 `tests/when_expression_test.cpp`（model 层）。

**实现**：

1. 语法（与 VS Code 子集一致）：

   ```
   expr     := or
   or       := and ( "||" and )*
   and      := unary ( "&&" unary )*
   unary    := "!" unary | primary
   primary  := "(" expr ")" | comparison
   comparison := key [ op value ] | key "=~" regex | key ["not"] "in" key
   op       := "==" | "!=" | "<" | "<=" | ">" | ">="
   value    := 'single quoted' | "true" | "false" | number | bareword
   regex    := "/" ... "/" [i]
   key      := [A-Za-z][A-Za-z0-9_.]*
   ```

   只有 `key` 时按真值判断：`false`、`0`、空字符串、不存在为假。`a in b`：`b` 的值为字符串列表时判断成员；为字符串时判断子串。

2. 数据结构：

   ```cpp
   class WhenExpression {
   public:
       static std::variant<WhenExpression, WhenError> parse(QStringView text);
       [[nodiscard]] bool evaluate(const std::function<QVariant(QStringView)>& lookup) const;
       [[nodiscard]] QString normalized() const; // 统一空白和引号，用于比较与显示
   private:
       struct Node; std::shared_ptr<const Node> root_;
   };
   struct WhenError { int offset; QString message; };
   ```

   `Node` 用标签联合（`enum Kind`）加子节点，不使用虚函数层次。正则在解析时编译为 `QRegularExpression`，保存在节点中。

3. 上限：文本 ≤ 1024 字符，嵌套 ≤ 32 层，节点 ≤ 256，正则源 ≤ 256 字符。超限返回 `WhenError`。
4. 错误信息使用翻译文字，并给出偏移量；快捷键编辑器（3.7）用它在输入框中标出位置。

**测试**：真值、比较、数字与字符串比较规则、`in`／`not in`、正则（含 `i` 标志）、优先级（`a || b && c`）、括号、`!` 的结合、所有错误类别和偏移量、上限边界（1024 可以、1025 拒绝）、`normalized()` 稳定性。用数据驱动（`_data()`）写，至少 60 行数据。

**状态**：未开始。

---

## 3.3 绑定携带条件与来源

**目标**：`CommandBinding` 增加 `when` 和来源；注册表校验表达式；分发按条件过滤并按来源决定优先级。

**涉及文件**：`include/window_system/command_registry.hpp`、`src/commands/command_registry.cpp`、`window_manager.cpp`（`eventFilter`、`Private::shortcutCandidates`）、`window_manager_p.hpp`、`tests/shortcut_dispatch_test.cpp`。

**实现**：

1. 结构：

   ```cpp
   enum class BindingSource { Default, Extension, User };
   struct CommandBinding {
       QKeySequence sequence;
       QVariantMap args;
       QString when;                                 // 空表示总是可用
       BindingSource source = BindingSource::Default;
       bool operator==(const CommandBinding&) const = default;
   };
   ```

   成员按此顺序追加，保持现有聚合初始化 `{sequence, args}` 可用。

2. `normalizeBindings()`：`when` 非空时调用 `WhenExpression::parse()`，失败则整批拒绝（与现有原子性一致）。重复判断改为“序列 + 规范化后的 when 相同”才算重复。
3. 候选索引：`BindingCandidate` 增加 `std::shared_ptr<const WhenExpression> when;` 和 `BindingSource source;`、`int order;`（全局注册顺序）。索引重建时解析一次。
4. 分发规则（写入 `settings-and-commands-design.md` 并作为 ADR）：
   - 候选按首键取出后，依次判断：序列匹配 → `when` 为真 → `enabled` 为真。
   - 精确匹配可能有多条。优先级：来源 `User` > `Extension` > `Default`；同一来源中，带 `when` 的优先于不带 `when` 的；再相同时按注册顺序，先注册的优先（这一条保持现有行为）。
   - 前缀等待：存在任何一条通过 `when` 与 `enabled` 的更长匹配时进入等待，与现在相同。
   - 现有“`ShortcutOverride` 只保留输入、不推进序列”的规则不变。
5. `when` 求值使用 3.1 的 `contextKeys(target)`；为了不在每个候选上重复计算，分发时为一次按键构造一个惰性查找函数（第一次访问某键时计算并缓存到局部 `QHash`）。
6. 应用的用户文件规则（`user_preferences.cpp` 第 174 行起只允许 `key`、`command`、`args`）增加 `when`。用户规则写入时 `source = User`；Lua 注册的绑定 `source = Extension`；内置为 `Default`。
7. Lua：`muxora.keymap.set(keys, target, { when = "..." })`，`window-system-lua/src/script_api.cpp` 的 `keymap.set` 处理函数读取 `when` 并传入；非法表达式返回错误。`docs/development/lua-guide.md` 更新。

**测试**（`shortcut_dispatch_test.cpp` 新增）：

- `whenGatesBindings`：同一按键绑定两个命令，条件分别为 `contentType == 'a'`、`contentType == 'b'`，在两种内容中按键各触发对应命令。
- `userSourceOverridesDefault`、`specificWhenBeatsUnconditional`、`sameSourceKeepsRegistrationOrder`（锁定现有行为）。
- `falseWhenDoesNotReservePrefix`：条件为假的长绑定不会让短按键进入等待。
- `invalidWhenRejectsTheWholeBatch`。
- `whenUsesFocusChainKeys`：内容控件属性 `windowSystemContextKeys` 生效。
- 所有原有测试保持通过。

**状态**：未开始。

---

## 3.4 菜单与右键贡献的条件

**目标**：菜单项、命令面板条目、右键贡献可以按 `when` 显示或隐藏。

**涉及文件**：`command_registry.hpp`（`Command` 增加 `QString menuWhen;`，`ContextMenuPlacement` 增加 `QString when;`）、`command_menus.cpp`、`command_dialogs.cpp`（命令面板）、`window-system-lua/src/script_api.cpp`（`commands.register` 的描述字段）、`tests/menu_structure_test.cpp`、`tests/extension_api_test.cpp`。

**实现**：

1. `menuWhen` 为假时，主菜单和命令面板不显示该命令；`enabled` 仍决定是否可点。快捷键不受 `menuWhen` 影响（与 VS Code 的 `menus.when` 和 `keybindings.when` 分开一致）。
2. 菜单在 `aboutToShow` 时求值（现有菜单已在打开时刷新启用状态，沿用该时机）。
3. 右键贡献：`ContextMenuPlacement::when` 与现有 `contentType` 过滤同时生效。

**测试**：`menuWhenHidesItemsButKeepsShortcut`；右键贡献按 `when` 出现与消失；命令面板过滤。

**状态**：未开始。

---

## 3.5 内容优先按键与默认绑定调整

**目标**：终端、编辑器这类内容获得焦点时，默认快捷键不会吃掉 `Ctrl+W`、`Ctrl+S` 等按键；用户可以列出“即使内容获得焦点也由应用处理”的命令。

**涉及文件**：`window_manager.hpp/.cpp`、`window_commands.cpp` 及其他注册默认绑定的位置、`src/app/main.cpp`（`host.newTab`、`layout.save`、`application.exit` 的默认键）、`docs/development/window-system-preview.md` 的快捷键表、`tests/shortcut_dispatch_test.cpp`。

**实现**：

1. 内容声明：控件属性 `windowSystemCapturesKeys = true`（沿焦点链查找，最近的生效）。
2. API：

   ```cpp
   // While focused content captures keys, only these commands may consume a
   // keystroke. Others reach the content unchanged. Default: see below.
   void setCommandsToSkipContent(QStringList commandIds);
   [[nodiscard]] QStringList commandsToSkipContent() const;
   ```

3. 分发：焦点内容声明捕获时，候选只保留 `commandsToSkipContent` 中的命令；其余按键原样交给内容（`eventFilter` 返回 `false`，`ShortcutOverride` 不接受）。正在等待续键时，若续键属于跳过列表中的命令，继续等待；否则取消等待并把按键交给内容。
4. 默认跳过列表（参照 VS Code `terminal.integrated.commandsToSkipShell` 的思路，只列确实需要全局可用的命令）：命令面板、窗口选择、标签组和窗格导航、全屏切换、设置、`Ctrl+PgUp/PgDn` 切换标签、新建／关闭标签的 `Ctrl+Shift` 变体（第 5 条）。
5. 新增默认绑定（只在捕获按键的内容中需要）：`Ctrl+Shift+T` 新建标签、`Ctrl+Shift+W` 关闭标签。原有 `Ctrl+T`、`Ctrl+W` 保留，不再出现在跳过列表中。
6. 当前预览只有占位编辑器（`QPlainTextEdit`），它不声明捕获，所以现有行为不变。测试中使用声明捕获的夹具控件。
7. 设置项（阶段 4 完成后接入设置页）：`keyboard.commandsToSkipContent`，字符串数组。之前先通过 API 和 Lua（`muxora.keymap.skip_content(list)`）设置。

**测试**：

- `capturingContentReceivesControlKeys`：夹具控件声明捕获，按 `Ctrl+W`、`Ctrl+S`、`Ctrl+K`，控件收到按键，命令都不执行。
- `skipListCommandsStillRun`：`Ctrl+Shift+P` 打开命令面板。
- `pendingChordYieldsToContent`：在非捕获内容中按 `Ctrl+K` 进入等待，然后焦点切到捕获内容，按 `Z`：内容收到 `Z`，等待被取消。
- 原有按键测试全部通过（占位内容不捕获）。

**状态**：未开始。

---

## 3.6 JSONC 读写模块

**目标**：读取带注释和尾逗号的 JSON；修改某个路径时保留其余文字（注释、空行、缩进）不变。阶段 4 的设置文件也使用它。

**涉及文件**：新建 `modules/jsonc/`（只依赖 Qt Core，可独立测试）：`CMakeLists.txt`、`include/jsonc/jsonc.hpp`、`src/jsonc.cpp`、`tests/jsonc_test.cpp`；顶层 `CMakeLists.txt` 加 `add_subdirectory(modules/jsonc)`。

**实现**：

1. API：

   ```cpp
   namespace jsonc {
   struct Error { qsizetype offset = -1; int line = 0; int column = 0; QString message; };
   struct ParseResult { QJsonValue value; std::optional<Error> error; };
   ParseResult parse(QStringView text);                    // 允许 //、/* */、尾逗号
   using Path = QVector<std::variant<QString, int>>;       // 对象键或数组下标
   struct EditResult { QString text; std::optional<Error> error; };
   EditResult setValue(QStringView text, const Path& path, const QJsonValue& value);
   EditResult remove(QStringView text, const Path& path);
   EditResult append(QStringView text, const Path& arrayPath, const QJsonValue& value);
   }
   ```

2. 实现方式：带位置的词法器（字符串、数字、字面量、标点、注释、空白），递归下降解析生成带偏移的节点树（类型、起止偏移、键、子节点）。编辑时只替换目标节点的文本区间；新增属性插入到对象的最后一个属性之后，补逗号，缩进取同级属性的缩进（没有同级时取父级缩进加 4 个空格）；删除时连带删除相邻的一个逗号，保留注释。
3. 新值的文字用 `QJsonDocument` 的紧凑格式生成单行文本；对象或数组值按所在层级缩进展开。
4. 上限：文本 ≤ 4 MiB，嵌套 ≤ 128 层。

**测试**：解析（注释位置、尾逗号、错误行列号）；编辑（替换数字为对象、在空对象中新增、在有注释的对象末尾新增、删除第一个／中间／最后一个属性、数组追加、路径不存在时的错误）；每个编辑用例同时断言“解析编辑结果得到的值正确”和“非目标区域的文字逐字不变”。

**状态**：未开始。

---

## 3.7 用户快捷键文件改为 JSONC 并监听修改

**涉及文件**：`src/app/user_preferences.{hpp,cpp}`、`src/app/scripting_setup.cpp`（保存时机）、`src/app/CMakeLists.txt`（链接 `jsonc`）、`src/app/tests/user_preferences_test.cpp`。

**实现**：

1. 读取改用 `jsonc::parse()`。错误报告包含行列号。
2. 写入：图形界面修改一条规则时，用 `jsonc::setValue`／`append`／`remove` 改动对应数组元素，保留用户注释。现有“检测外部修改，不覆盖”的机制（`keybindingsBytes_`）保持。
3. 监听：`QFileSystemWatcher` 监听文件和所在目录（应对保存时替换文件），300 ms 去抖动后重新加载；内容与最近一次自己写入的字节相同则忽略。加载失败时保留当前有效绑定，报告错误。
4. 新文件模板带注释说明格式与示例。

**测试**：带注释文件的读取；图形修改后注释保留；外部修改触发重新加载；损坏文件不影响当前绑定；自己写入不触发重复加载。

**状态**：未开始。

---

## 3.8 快捷键编辑器重做

> 界面规格以 [界面设计规范 §4.10](../../design/ui-design-system.md#410-快捷键编辑器参照-vs-code-键盘快捷方式编辑器) 为准，并遵守 [执行规则中的界面标准](README.md#界面标准)。下文与规范冲突时以规范为准。

**目标**：对齐 VS Code 键盘快捷方式编辑器。

**涉及文件**：`src/settings/shortcuts_page.cpp`（现约 500 行，基于 `QTreeWidget`），拆分为 `shortcuts_model.{hpp,cpp}`（`QAbstractTableModel`）、`shortcuts_page.cpp`（界面）、`shortcut_capture.{hpp,cpp}`（现有 `ShortcutCapture` 类移出）；`tests/shortcut_settings_test.cpp`。

**实现**：

1. 模型：每行一条绑定（命令无绑定时一行空绑定），列：命令标题、命令 ID（隐藏列，可搜索）、按键、`when`、来源。用 `QSortFilterProxyModel` 过滤。命令数量上千时不创建逐行控件。
2. 搜索语法：普通文字匹配标题和 ID；`@source:user|default|extension`；`@command:<id>`；`@when:<text>`；按键录入模式（工具栏按钮切换，复用现有“按键查询”）。
3. 行操作（右键菜单和工具栏按钮）：修改按键、修改条件（行内编辑，用 3.2 的解析器即时校验并标出错误位置）、删除、重置为默认、复制命令 ID、显示相同按键。
4. 冲突显示：同一序列在同一来源下有多条且条件可能同时为真（无法判定时视为可能）时，在按键列显示警告图标和提示。
5. 顶部按钮：打开 `keybindings.json`（用户文件，交给宿主处理：模块发信号，应用打开文件或内置编辑器）、显示默认绑定（只读 JSON，模块生成）。
6. 键盘操作：`Enter` 修改按键、`Ctrl+K Ctrl+E` 修改条件（只在页面获得焦点时生效，使用 `when: "keybindingsEditorFocus"`）、`Delete` 删除。

**测试**：保留现有 `shortcut_settings_test` 的全部断言（它们针对行为而不是控件类型的部分）。依赖 `QTreeWidget` 具体结构的断言，按测试规范提交独立证据说明“同一行为在新模型中如何检查”后修订，保留等效覆盖。新增：过滤语法、`when` 编辑与校验、冲突标记、重置。

**状态**：未开始。

---

## 3.9 键位预设

**目标**：可以一键导入 VS Code、Visual Studio、Windows Terminal、Xshell 风格的键位。

**实现**：预设是应用资源中的 JSONC 规则列表（`src/app/keymaps/*.jsonc`），导入时生成用户规则追加到 `keybindings.json`，并在文件中用注释标出导入来源和日期。导入前在界面中列出将新增和覆盖的绑定，确认后执行。每份预设只映射 Muxora 实际存在的命令，不存在的命令跳过并在列表中说明。

**测试**：每份预设中的命令 ID 都存在（防止命令改名后预设失效）；导入后再导入不产生重复规则。

**状态**：未开始。
