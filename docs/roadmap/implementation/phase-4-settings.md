# 阶段 4：设置（schema、生成式界面、持久化）

**目的**：设置项由声明驱动：模块、应用、Lua 都通过同一个注册表声明设置；设置页从声明生成；用户值统一保存在 `User/settings.json`（JSONC），布局文件只存结构。对应审查记录 R10。

**阶段约束**：

- 每类数据放在哪个文件按 [设置设计 §2](../../design/settings-and-commands-design.md#2-文件划分) 的表格。
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
  | `window.captionButtonOrder` | `captionButtonOrder` |
  | `tabs.centralPlacement`、`tabs.pinnedPlacement`、`tabs.multiline`、`tabs.wheelExpandRows`、`tabs.showIcons`、`tabs.showNewTabButton`、`tabs.showNumbers`、`tabs.showSidebarIcons`、`tabs.showSidebarNumbers`、`tabs.clipTitles`、`tabs.maximumTitleWidth`、`tabs.clipSidebarTitles`、`tabs.maximumSidebarTitleWidth`、`tabs.stickySizing`、`tabs.shrinkStickyTitleWidth`、`tabs.stickyTabsOnSeparateRow`、`tabs.preventStickyClose`、`tabs.colors` | `TabAppearance` 各字段 |
  | `window.visibility.*` | 四栏显隐（属于窗口状态，见 4.4） |

- 模块不读写文件。注册表只保存“值”和“是否修改过”，由宿主负责持久化。
- 设置与布局的读写同步切换，遵守 [当前配置格式约束](../../development/engineering-design.md#24-当前配置格式)。

---

## 4.1 设置注册表

**涉及文件**：新建 `include/window_system/settings_registry.hpp`、`src/settings/settings_registry.cpp`；`window_manager.hpp/.cpp`；新测试 `tests/settings_registry_test.cpp`（model 层）。

**实现**：

1. 类型：

   ```cpp
   enum class SettingType { Boolean, Integer, Number, String, Enum, StringList, Color, Json };
   struct SettingEnumValue { QString value; QString label; QString description; };
   // 不填时按类型生成控件。滑条只覆盖常用的一段，数字框、文件和 Lua 仍按
   // minimum／maximum 校验。
   struct SettingEditor {
       enum class Kind { Default, SliderWithNumber } kind = Kind::Default;
       int sliderMinimum = 0, sliderMaximum = 0;
       QString unit;                                // 只用于显示，例如 "px"
   };
   struct SettingDefinition {
       QString id;                     // [a-z][A-Za-z0-9]*(\.[A-Za-z0-9]+)+，≤ 128
       QString category;               // 设置页分类路径，例如 "window/drag"
       QString title;
       QString description;
       SettingType type = SettingType::Boolean;
       QVariant defaultValue;
       std::optional<double> minimum, maximum;     // Integer / Number，程序能正确处理的范围
       SettingEditor editor;
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
2a. **需要确认的变更**（供 `ssh.config.syncAppSessions` 等使用，见阶段 7.4m）：`SettingDefinition` 增加可选的 `std::function<void(const QVariant& oldValue, const QVariant& newValue, std::function<void(bool accepted)> done)> confirmChange`。设置了它的设置项，`setValue()` 和 `load()` 先调用它，只有 `done(true)` 之后才提交新值并发出变化通知；`done(false)` 时保持旧值，并通知宿主把 `settings.json` 中的值改回。确认是异步的，期间该设置处于“待确认”状态，设置页显示对应提示，同一设置的再次修改会取消上一次待确认。
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
   - 定义里指定了 `editor` 时用对应控件：`SliderWithNumber` 用现有的 `IntegerSlider`，标题长度这几项（`0–1000` 的滑条）照此声明。
   - 控件的 `objectName` 使用设置 ID；同步更新按对象名查找控件的测试，不建立旧对象名映射。
2. 有特殊交互的设置（`window.visibility.*` 的四个开关属于当前窗口，`workbench.language` 需要 `TranslationManager`）用“自定义编辑器”注册：`registerSettingEditor(id, factory)`，设置页优先使用。
3. 每行布局：标题、ID（灰色小字）、说明、控件；修改过的行在左侧显示 2 像素的强调色竖条，行尾齿轮按钮菜单：“重置此设置”“复制设置 ID”“复制为 JSON”。
4. 搜索：普通文字匹配标题、说明、ID、标签；`@modified` 只显示修改过的；`@id:<前缀>`；`@tag:<标签>`。保留分类树、点击定位和随滚动同步的现有行为。
5. 顶部增加“打开 settings.json”按钮：模块发出信号 `settingsFileRequested()`（在 `WindowManager` 上），由宿主决定打开方式。
6. `requiresRestart` 的设置修改后，行内显示“重启后生效”。
7. 注册表增删设置（例如 Lua 重载）时，设置页增量更新对应行，不重建整页；滚动位置和搜索词保留。

**测试**：所有现有设置页测试通过（同步使用设置 ID 查找控件）。新增：每种控件类型修改后注册表值正确；外部修改注册表后控件同步；`@modified` 过滤；重置；新增和删除设置时页面增量更新。

**状态**：未开始。

---

## 4.4 应用持久化

**涉及文件**：`src/app/user_preferences.{hpp,cpp}`、`src/app/main.cpp`、`modules/window-system/src/layout_persistence.cpp`、`src/app/tests/user_preferences_test.cpp`、`tools/tests/application-exit-test.ps1`（如需检查文件）。

**实现**：

1. `settings.json` 改用 `jsonc`（阶段 3.6）读取；写入时对每个变化的 ID 调用 `jsonc::setValue`／`remove`（值回到默认时删除该键），保留注释。
2. 启动顺序（`main.cpp`）：创建 `WindowManager` → 读取 `settings.json` → `manager.settings().load(values, diagnostics)` → 恢复布局。诊断显示在状态栏，与现有语言设置的处理一致。
3. 行为与标签外观值只由设置注册表及 `settings.json` 保存；同步从布局读写中移除对应字段，不导入旧布局或双写。
4. 未指定的设置使用注册表默认值；布局恢复不修改全局设置。
5. `window.visibility.*` 是逐窗口状态，继续随布局保存，不进入 `settings.json`；工具条排布同样留在布局里。
5a. 布局文件不再带行为和标签外观后，`src/app/layout_file.cpp` 的分类恢复只剩各栏默认显隐和默认工具条排布两类，同步删掉另外两类。
6. 外部修改监听：与 3.7 相同的 `QFileSystemWatcher` 加去抖动；重新加载时只应用变化的项。

**测试**：

- `user_preferences_test`：读写、注释保留、非法值诊断、外部修改重新加载。
- 设置缺省、覆盖及重启保持；布局中不写全局设置，恢复布局不改变已加载的注册表。
- 部署夹具：`application-exit-test.ps1` 照常通过。

**状态**：未开始。

---

## 4.5 Lua 与应用声明设置

**涉及文件**：`window-system-lua/src/script_api.cpp`（`settings.get`／`settings.update` 现有实现）、`lua-guide.md`、`src/app/main.cpp`。

**实现**：

1. Lua 新增 `muxora.settings.register{ id=..., type=..., default=..., title=..., description=..., enum={...} }`。ID 必须以 `user.` 或脚本名前缀开头，不能覆盖内置设置；脚本卸载时注销定义，值保留在未知值表中。
2. `settings.get`／`settings.update` 改为读写注册表，使用当前设置 ID，并同步更新脚本调用方和指南。
3. 应用自己的设置（例如以后的 `terminal.*`、`ssh.*`）在 `main.cpp` 或对应业务模块中声明，所有者为 `muxora`。

**测试**：`window-system.lua` 中新增注册、读取、更新、卸载后值保留；非法定义被拒绝。

**状态**：未开始。

---

## 4.6 配置文件（Profile，后续）

在 4.1–4.5 稳定后再设计。要点：一个 Profile 是一组 `settings.json`、`keybindings.json`、`init.lua` 和布局的组合，位于 `User/profiles/<name>/`；切换时整体替换注册表值、绑定和脚本，窗口布局可选是否切换。写成独立设计文档后再拆步骤。

**状态**：未开始（需要设计）。

---

## 4.7 配置文件的智能提示

**目标**：在内置编辑器中编辑 `settings.json`、`keybindings.json`、主题文件时，像 VS Code 一样补全键和值、悬停显示说明、标出未知或错误的设置（[内置编辑器设计 §5](../../design/text-editor-design.md#5-编辑配置文件时的智能提示)）。

**前置**：5.2（编辑器）、3.6（JSONC 解析，带节点位置）、4.1（设置注册表）、3.1（上下文键）、2.2（颜色主题与颜色注册表）。

**涉及文件**：新建 `src/editor/plugin/json_language_service.{hpp,cpp}`、`src/app/config_schemas.{hpp,cpp}`（从注册表生成结构描述）、测试 `src/app/tests/json_language_service_test.cpp`。

**实现**：

1. **结构描述**：JSON Schema 的一个子集（`type`、`properties`、`enum` 加 `enumDescriptions`、`default`、`description`、`deprecationMessage`、`items`、`additionalProperties`）。`config_schemas` 在第一次需要时从设置注册表、命令注册表（含参数说明）、上下文键列表、颜色注册表生成，并在注册表变化时失效。
2. **定位**：用 3.6 的 JSONC 解析器得到光标所在的路径（例如 `["window.newTabPosition"]` 或 `[3, "command"]`）以及光标处于键、值还是空位置。
3. **补全**：按路径在结构描述中找到可用的键或值；插入完整的 `"键": 值`（值为默认值并选中）；`keybindings.json` 中 `command` 的值补全命令 ID（显示命令标题），`key` 的值补全按键名称，`when` 的值补全上下文键和运算符。
4. **悬停说明**：键和值的标题、说明、类型、默认值、当前生效值、是否需要重启。
5. **诊断**：未知设置 ID 以灰色样式加提示；类型或取值错误用波浪下划线；JSON 语法错误；废弃的设置用删除线并提示替代项。解析在停止输入 200 毫秒后进行，大文件在后台线程解析。
6. **快速修复**：未知设置名按编辑距离给出最接近的已知名称，`Ctrl+.` 替换。

**测试**：每个功能在三种文件上的数据驱动用例；注册表新增设置后补全立即包含它；诊断的出现与消失；10 MiB 的 JSON 文件中输入不卡顿（解析不在界面线程）。

**状态**：未开始。
