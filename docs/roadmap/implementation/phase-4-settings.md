# 阶段 4：设置（schema、生成式界面、存储迁移）

**目的**：设置项由声明驱动：模块、应用、Lua 都通过同一个注册表声明设置；设置页从声明生成；用户值统一保存在 `User/settings.json`（JSONC），布局文件只存结构。对应审查记录 R10。

**阶段约束**：

- 设置 ID 沿用现有设置页已经显示的 ID，不改名：

  | 现有 ID | 对应字段 |
  | --- | --- |
  | `workbench.language` | 界面语言（已在 `settings.json`） |
  | `window.dragOutsideKind` | `WindowBehavior::dragOutsideKind` |
  | `window.closeEmptyWorkbench` | `closeEmptyWorkbench` |
  | `window.revealDuringDrag` | `revealDuringDrag` |
  | `window.revealDelayMs` | `revealDelayMs` |
  | `window.newTabPosition` | `newTabPosition` |
  | `window.hideSingleTabWorkbench` | `hideSingleTabWorkbench` |
  | `window.hideSingleTabFloating` | `hideSingleTabFloating` |
  | `window.hideToolbarOptionsWhenLocked` | `hideToolbarOptionsWhenLocked` |
  | `window.zenFullscreenMode` | `zenFullscreenMode` |
  | `tabs.centralPlacement`、`tabs.pinnedPlacement`、`tabs.multiline`、`tabs.wheelExpandRows`、`tabs.showIcons`、`tabs.showNewTabButton`、`tabs.showNumbers`、`tabs.showSidebarIcons`、`tabs.showSidebarNumbers`、`tabs.colors` | `TabAppearance` 各字段 |
  | `window.visibility.*` | 四栏显隐（属于窗口状态，见 4.4） |

- 模块不读写文件。注册表只保存“值”和“是否修改过”，由宿主负责持久化。
- 迁移期间旧布局中的 `behavior`、`tabAppearance` 继续可读，保证用户不丢设置。

---

## 4.1 设置注册表

**涉及文件**：新建 `include/window_system/settings_registry.hpp`、`src/settings/settings_registry.cpp`；`window_manager.hpp/.cpp`；新测试 `tests/settings_registry_test.cpp`（model 层）。

**实现**：

1. 类型：

   ```cpp
   enum class SettingType { Boolean, Integer, Number, String, Enum, StringList, Color, Json };
   struct SettingEnumValue { QString value; QString label; QString description; };
   struct SettingDefinition {
       QString id;                     // [a-z][A-Za-z0-9]*(\.[A-Za-z0-9]+)+，≤ 128
       QString category;               // 设置页分类路径，例如 "window/drag"
       QString title;
       QString description;
       SettingType type = SettingType::Boolean;
       QVariant defaultValue;
       std::optional<double> minimum, maximum;     // Integer / Number
       QVector<SettingEnumValue> enumValues;        // Enum
       QStringList tags;                            // 搜索用
       bool requiresRestart = false;
       int order = 0;
       QString owner;                               // "window-system"、"muxora"、Lua 脚本名
   };
   class SettingsRegistry {
   public:
       bool add(SettingDefinition definition);      // ID 重复、默认值不符合类型时返回 false
       bool remove(const QString& id);
       [[nodiscard]] const SettingDefinition* find(const QString& id) const;
       [[nodiscard]] QVector<SettingDefinition> definitions() const; // 按 category、order 排序
       [[nodiscard]] QVariant value(const QString& id) const;        // 未修改时返回默认值
       bool setValue(const QString& id, const QVariant& value);      // 校验后写入；等于默认值时视为未修改
       bool reset(const QString& id);
       [[nodiscard]] bool isModified(const QString& id) const;
       // 宿主加载：逐项校验；非法项不应用并写入 diagnostics；返回是否全部有效
       bool load(const QVariantMap& values, QStringList& diagnostics);
       [[nodiscard]] QVariantMap modifiedValues() const;
       void setChangedHandler(std::function<void(const QStringList& ids)> handler); // 同一次 load 或批量修改只通知一次
   };
   ```

   取值与校验规则写在头文件注释中：`Enum` 必须是 `enumValues` 中的值；`Color` 是 `#RRGGBB` 或 `#AARRGGBB` 字符串；`Json` 只做“是对象或数组”的检查，内容由对应的使用方校验（例如 `tabs.colors`）。

2. 未知 ID 的值（例如被卸载的 Lua 脚本留下的设置）由 `load()` 保留在一个“未知值”表中，`modifiedValues()` 照样返回它们，保存时不会丢失。
3. `WindowManager` 拥有一个注册表：`SettingsRegistry& settings();`。

**测试**：每种类型的合法与非法值；默认值等于值时 `isModified` 为假；`load` 部分非法时其他项照常生效并给出诊断；未知 ID 往返保留；通知合并。

**状态**：未开始。

---

## 4.2 模块设置接入注册表

**目标**：窗口行为与标签外观以注册表为唯一来源；现有 `setBehavior()`、`setTabAppearance()` API 保持可用。

**涉及文件**：新建 `src/settings/builtin_settings.cpp`（模块设置的声明表）；`window_manager.cpp`（构造、`setBehavior`、`setTabAppearance`）；`style/tab_appearance.cpp`；测试 `tests/pane_settings_test.cpp`、`tests/tab_appearance_test.cpp`。

**实现**：

1. `registerBuiltInSettings(SettingsRegistry&)` 按本阶段开头的表格声明全部设置，默认值取自 `WindowBehavior{}`、`TabAppearance{}`；`title`、`description` 使用设置页现有的翻译文字（直接搬过来，保证翻译不丢）。
2. 同步方向：
   - 注册表变化 → 处理函数根据变化的 ID 重建 `WindowBehavior`／`TabAppearance`，调用内部的 `applyBehavior()`／`applyTabAppearance()`（即现有 `setBehavior()`、`setTabAppearance()` 的实现体）。
   - 宿主调用 `setBehavior()` → 把各字段写入注册表（批量），再由处理函数应用。用一个“正在同步”标志防止循环。
3. 现有 `revealDelayMs` 的 `qBound(200, …, 2000)` 改为注册表的 `minimum`／`maximum`，行为一致：超出范围的值被拒绝而不是被截断——**这是行为变化**。为保持一致，`setBehavior()` 路径仍先截断再写入注册表；只有通过设置文件或设置页写入的非法值才被拒绝并报告。写进注释。

**测试**：现有设置相关测试全部通过；新增 `behaviorApiAndRegistryStayInSync`（两个方向各改一次，另一边一致，且只应用一次）。

**状态**：未开始。

---

## 4.3 设置页由声明生成

> 界面规格以 [界面设计规范 §4.9](../../design/ui-design-system.md#49-设置页参照-vs-code-设置编辑器) 为准，并遵守 [执行规则中的界面标准](README.md#界面标准)。注意规范要求设置 ID 不在每行常驻显示（放进齿轮菜单），控件放在说明下方。

**涉及文件**：`src/settings/settings_page.{hpp,cpp}`、`settings_view_options.cpp`、`settings_tab_appearance.cpp`；新建 `src/settings/setting_editors.{hpp,cpp}`（各类型控件工厂）；测试 `tests/pane_settings_test.cpp` 等使用设置页的测试。

**实现**：

1. 控件工厂：`QWidget* createEditor(const SettingDefinition&, SettingsRegistry&, QWidget* parent)`：
   - `Boolean` → `ThemedCheckBox`；`Enum` → `ThemedComboBox`；`Integer`／`Number` → 现有主题数字框；`String` → `QLineEdit`；`StringList` → 列表加“添加／删除”按钮；`Color` → 色块按钮加 `QColorDialog`；`Json` → 现有 `tabs.colors` 使用的多行编辑框，失焦时校验。
   - 控件的 `objectName` 默认为设置 ID。现有测试通过对象名查找的控件，保留原对象名：在 `builtin_settings.cpp` 中为这些设置声明一个可选字段 `legacyObjectName`（只在模块内部使用，不进入公开结构时可以用一个内部映射表）。先 `grep -n "findChild" tests/*settings*_test.cpp tests/pane_settings_test.cpp` 列出全部被引用的名字。
2. 有特殊交互的设置（`window.visibility.*` 的四个开关属于当前窗口，`workbench.language` 需要 `TranslationManager`）用“自定义编辑器”注册：`registerSettingEditor(id, factory)`，设置页优先使用。
3. 每行布局：标题、ID（灰色小字）、说明、控件；修改过的行在左侧显示 2 像素的强调色竖条，行尾齿轮按钮菜单：“重置此设置”“复制设置 ID”“复制为 JSON”。
4. 搜索：普通文字匹配标题、说明、ID、标签；`@modified` 只显示修改过的；`@id:<前缀>`；`@tag:<标签>`。保留分类树、点击定位和随滚动同步的现有行为。
5. 顶部增加“打开 settings.json”按钮：模块发出信号 `settingsFileRequested()`（在 `WindowManager` 上），由宿主决定打开方式。
6. `requiresRestart` 的设置修改后，行内显示“重启后生效”。
7. 注册表增删设置（例如 Lua 重载）时，设置页增量更新对应行，不重建整页；滚动位置和搜索词保留。

**测试**：所有现有设置页测试通过（按 1 中的对象名保留）。新增：每种控件类型修改后注册表值正确；外部修改注册表后控件同步；`@modified` 过滤；重置；新增和删除设置时页面增量更新。

**状态**：未开始。

---

## 4.4 应用持久化与迁移

**涉及文件**：`src/app/user_preferences.{hpp,cpp}`、`src/app/main.cpp`、`modules/window-system/src/layout_persistence.cpp`、`src/app/tests/user_preferences_test.cpp`、`tools/tests/application-exit-test.ps1`（如需检查文件）。

**实现**：

1. `settings.json` 改用 `jsonc`（阶段 3.6）读取；写入时对每个变化的 ID 调用 `jsonc::setValue`／`remove`（值回到默认时删除该键），保留注释。
2. 启动顺序（`main.cpp`）：创建 `WindowManager` → 读取 `settings.json` → `manager.settings().load(values, diagnostics)` → 恢复布局。诊断显示在状态栏，与现有语言设置的处理一致。
3. **迁移**：恢复布局时，若 `settings.json` 中没有某个行为或标签外观 ID，而布局快照的 `behavior`／`tabAppearance` 中有非默认值，则把该值写入注册表并标记需要保存（一次性迁移）。迁移后 `saveLayout()` **仍然写出** `behavior` 和 `tabAppearance` 一个版本周期，保证用户回退到旧版本不丢设置；在 `CHANGELOG.md` 记录将在之后的版本移除。
4. 布局恢复时，若 `settings.json` 已有值，以 `settings.json` 为准，忽略布局中的同名值。
5. `window.visibility.*` 是逐窗口状态，继续随布局保存，不进入 `settings.json`。
6. 外部修改监听：与 3.7 相同的 `QFileSystemWatcher` 加去抖动；重新加载时只应用变化的项。

**测试**：

- `user_preferences_test`：读写、注释保留、非法值诊断、外部修改重新加载。
- 迁移：用旧版本保存的布局（在测试中构造含 `behavior` 的快照）启动，断言设置迁入 `settings.json`，第二次启动不再迁移；`settings.json` 与布局冲突时以前者为准。
- 部署夹具：`application-exit-test.ps1` 照常通过。

**状态**：未开始。

---

## 4.5 Lua 与应用声明设置

**涉及文件**：`window-system-lua/src/script_api.cpp`（`settings.get`／`settings.update` 现有实现）、`lua-guide.md`、`src/app/main.cpp`。

**实现**：

1. Lua 新增 `muxora.settings.register{ id=..., type=..., default=..., title=..., description=..., enum={...} }`。ID 必须以 `user.` 或脚本名前缀开头，不能覆盖内置设置；脚本卸载时注销定义，值保留在未知值表中。
2. `settings.get`／`settings.update` 改为读写注册表（现有键名保持兼容；先阅读现有实现确认支持哪些键）。
3. 应用自己的设置（例如以后的 `terminal.*`、`ssh.*`）在 `main.cpp` 或对应业务模块中声明，所有者为 `muxora`。

**测试**：`window-system.lua` 中新增注册、读取、更新、卸载后值保留；非法定义被拒绝。

**状态**：未开始。

---

## 4.6 配置文件（Profile，后续）

在 4.1–4.5 稳定后再设计。要点：一个 Profile 是一组 `settings.json`、`keybindings.json`、`init.lua` 和布局的组合，位于 `User/profiles/<name>/`；切换时整体替换注册表值、绑定和脚本，窗口布局可选是否切换。写成独立设计文档后再拆步骤。

**状态**：未开始（需要设计）。
