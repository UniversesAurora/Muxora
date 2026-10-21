# 阶段 2：主题系统

**目的**：按 [主题系统设计](../../design/theme-system-design.md) 实现与 VS Code 相同的主题机制：颜色注册表（每个颜色 ID 带四类默认值）、主题定义与继承、`workbench.colorTheme` 等设置与切换逻辑、用户颜色覆盖；提供 Muxora Dark 与 Muxora Black，并用对比度测试和组件画廊保证深色下配色协调。

> 主题行为完全按 VS Code 的逻辑、深色配色整体协调（用户要求），所以采用颜色注册表方案，不在现有 `Theme` 结构上加字段；“去掉硬编码颜色”“原生深色边框”“外观设置”都在下面的步骤里。

**阶段约束**：

- **浅色主题像素不变**：注册表中 `light` 默认值和 Muxora Light 主题必须让现有界面逐像素保持原样，现有像素测试不改预期。这是迁移正确性的主要检查。
- 公开结构 `window_system::Theme` 和 `setTheme()` 继续可用（设计 §5 的兼容视图）。
- 模块不读写文件；内置主题以 Qt 资源打包在模块中，用户主题文件由宿主读取后交给模块。
- 颜色值以设计文档 §3 为初稿，以本阶段的对比度测试为准；调整颜色让测试通过，不调整阈值。
- 每个步骤都更新组件画廊（2.5 之后）并导出三套截图到 `out/diagnostics/ui-review/<步骤>/`。

**步骤顺序**：2.1 → 2.2 → 2.3 → 2.4 → 2.5 → 2.6 → 2.7 → 2.8 → 2.9。2.10 可选。

---

## 2.1 颜色注册表核心

**目标**：可注册颜色 ID、表达默认值、按主题类型解析的纯数据模块。

**涉及文件**：新建 `include/window_system/color_registry.hpp`、`src/style/color_registry.cpp`、`src/style/color_expression.{hpp,cpp}`（内部）；`modules/window-system/CMakeLists.txt`；新测试 `tests/color_registry_test.cpp`（model 层）。

**实现**：

1. 公开类型：

   ```cpp
   enum class ColorThemeType { Light, Dark, HighContrastDark, HighContrastLight };
   class ColorId {                       // 注册后得到的轻量句柄，内部为下标
   public:
       [[nodiscard]] int index() const noexcept;
       [[nodiscard]] bool isValid() const noexcept;
   };
   struct ColorDefaults { QString light, dark, hcDark, hcLight; }; // 每项是一个默认值表达式
   class ColorRegistry {
   public:
       static ColorRegistry& instance();   // 进程内唯一；只在 GUI 线程修改
       ColorId registerColor(QString id, ColorDefaults defaults, QString description);
       [[nodiscard]] ColorId find(QStringView id) const;
       [[nodiscard]] QString idOf(ColorId color) const;
       [[nodiscard]] int count() const;
   };
   ```

   单例的理由：颜色 ID 是全局词汇表（与 VS Code 相同），多个 `WindowManager` 共享；注册只追加，不删除，因此下标稳定。在头文件注释中写明。

2. 默认值表达式（设计 §1.1）：`#rrggbb`、`#rrggbbaa`、`ref(id)`、`transparent(id, a)`、`mix(id1, id2, t)`、`lighten(id, f)`、`darken(id, f)`、`none`。`color_expression` 中实现解析（注册时解析一次并保存语法树，非法表达式让注册失败）和求值。
3. 解析器：

   ```cpp
   class ResolvedColors {               // 一次解析的结果，按 ColorId 下标访问
   public:
       [[nodiscard]] QColor color(ColorId id) const;
       [[nodiscard]] ColorThemeType type() const;
   };
   ResolvedColors resolveColors(ColorThemeType type, const QHash<QString, QColor>& explicitColors, QStringList& diagnostics);
   ```

   求值顺序：显式颜色（主题与覆盖合并后的结果）→ 当前类型的默认表达式。引用链递归求值并缓存；检测循环（循环时该颜色回退为透明并写入诊断）。

4. 变换的数学定义写进源码注释：`mix` 在线性 sRGB 中按比例混合后转回 sRGB；`lighten`／`darken` 在 HSL 空间加减亮度（与 VS Code 的 `Color.lighten`／`darken` 一致：`l' = l + l·f`／`l' = l − l·f`）；结果用 8 位通道保存，避免 16 位通道在保存恢复后变化（现有 `tab_appearance.cpp` 注释说明过这个问题）。

**测试**：各表达式求值；引用链；循环检测；未注册 ID 的引用；同名重复注册失败；四类主题分别解析；变换结果与手算值一致（固定数据）。

**状态**：未开始。

---

## 2.2 颜色主题定义与内置主题

**涉及文件**：新建 `include/window_system/color_theme.hpp`、`src/style/color_theme.cpp`；新建资源目录 `modules/window-system/themes/`（`muxora-light.json`、`muxora-dark.json`、`muxora-black.json`）及 `themes.qrc`；`CMakeLists.txt`；新测试 `tests/color_theme_test.cpp`。

**实现**：

1. 结构：

   ```cpp
   struct ColorThemeDefinition {
       QString id;            // 稳定 ID，例如 "muxora-dark"
       QString label;         // 显示名称，例如 "Muxora Dark"；设置中按名称引用
       QStringList aliases;   // 旧名称
       ColorThemeType type;
       QString include;       // 继承的主题名称，可空
       QHash<QString, QColor> colors;
   };
   [[nodiscard]] QVector<ColorThemeDefinition> builtInColorThemes();
   // 解析 JSON 对象（不含注释；JSONC 由宿主先转换）。失败返回诊断，不部分生效。
   std::optional<ColorThemeDefinition> parseColorTheme(const QJsonObject& json, QStringList& diagnostics);
   ```

2. 主题文件格式按设计 §2.3。内置主题的 `colors` 只写与注册表默认值不同的部分；因为注册表默认值就是按 Muxora 的设计写的，内置主题文件通常很短。
3. 继承：解析时按 `include` 链合并，深度上限 8，循环报错。
4. `WindowManager` 增加：

   ```cpp
   bool registerColorTheme(ColorThemeDefinition theme);   // 名称不能与内置主题冲突
   bool removeColorTheme(const QString& label);           // 不能删除内置主题
   [[nodiscard]] QVector<ColorThemeDefinition> colorThemes() const;
   ```

**测试**：三个内置主题可解析；继承合并顺序；循环继承；非法颜色值；同名冲突；别名查找。

**状态**：未开始。

---

## 2.3 注册全部颜色 ID，浅色默认值等于现有颜色

**目标**：设计 §4 中的全部颜色 ID 注册完毕；`light` 默认值让现有界面不变。

**涉及文件**：新建 `src/style/builtin_colors.{hpp,cpp}`（声明表）；`src/style/theme.cpp`；测试 `tests/color_registry_test.cpp`、新建 `tests/color_coverage_test.cpp`。

**实现**：

1. 声明表的写法，每个 ID 一行，便于审阅：

   ```cpp
   const ColorId tabActiveBackground = registerColor(u"tab.activeBackground"_s,
       {u"#ead9fa"_s, u"ref(muxora.content.background)"_s, u"#000000"_s, u"#ffffff"_s},
       u"Active tab background in a focused group."_s);
   ```

   `light` 值逐个取自 `theme.cpp` 的 `defaultTheme()`、`styleSheet()` 中的 QSS 颜色、`tab_appearance.cpp` 的预设和 2.4 中要替换的硬编码颜色。逐项列一张对照表放在 `builtin_colors.cpp` 顶部注释中：“颜色 ID ← 原来的字段或 QSS 选择器”。
2. `dark` 值使用设计 §3.1 的 Muxora Dark 阶梯和 §3.2 的做法；Muxora Black 只在其主题文件中覆盖与 Dark 不同的颜色（中性阶梯和强调色）。`hcDark`、`hcLight` 先给出合理的高对比值（黑底白字、白底黑字、边框用 `contrastBorder`），2.9 再完善。
3. 兼容视图：`Theme defaultTheme()` 改为“用 Muxora Light 解析后填充 `Theme` 字段”，字段映射按设计 §5。
4. **覆盖检查测试**（`color_coverage_test`）：
   - 所有注册 ID 在四类主题下都能解析为非空颜色，诊断为空；
   - `Theme` 兼容视图中每个字段都有对应 ID；
   - 新增：`defaultThemeIsPixelIdentical`：解析得到的 `defaultTheme()` 与本步骤之前硬编码的值逐字段相等（把旧值写在测试中作为固定数据）。

**完成标准**：全量测试通过，不修改任何像素测试的预期。

**状态**：未开始。

---

## 2.4 组件改用颜色 ID

**目标**：样式表、自绘控件、拖拽覆盖层、标签颜色都从解析结果取色；不再有界面硬编码颜色。

**涉及文件**：`src/style/theme.cpp`（`styleSheet()` 改为从 `ResolvedColors` 生成）、`src/style/themed_controls.cpp`、`src/docking/drag_controller.cpp`（53–92 行）、`src/docking/drag_presentation.cpp`（41–46 行阴影）、`src/docking/tab_strip.cpp`、`src/docking/edge_tab_button.cpp`、`src/docking/edge_strip.cpp`、`src/chrome/*.cpp`、`src/style/tab_appearance.cpp`；`window_manager.cpp`（解析结果的保存与分发）。

**实现**：

1. `WindowManager::Private` 保存当前 `ResolvedColors`；`applyTheme()` 的根属性 `windowSystemThemeColors` 改为保存指向解析结果的共享句柄（`std::shared_ptr<const ResolvedColors>`，只读共享，主题变化时替换），自绘控件从最近的根读取。
2. `styleSheet()`：QSS 模板中的 `@text` 等占位符改为颜色 ID 占位，例如 `@{tab.activeBackground}`；生成时查解析结果。模板中出现未注册 ID 时，在 Debug 构建断言、Release 写诊断。
3. 拖拽覆盖层、阴影、标签条指示线等改用对应 ID（`muxora.dropTarget.*`、`widget.shadow` 等）。
4. **标签颜色预设**：每个预设注册三个 ID（`muxora.tabColor.<id>.background`／`.foreground`／`.indicator`），四类默认值；浅色值等于现有预设。`TabAppearance::colors` 中用户自定义的颜色继续作为显式值，优先于注册表（保持现有用户数据语义）。深色值按设计 §3.2：背景为颜色与内容背景混合 18–28%。
5. grep 确认没有剩余的界面硬编码颜色：

   ```bash
   grep -rnE "QColor\(\"#|QColor\([0-9]+, *[0-9]+|Qt::white|Qt::black" modules/window-system/src src
   ```

   剩余的每一处在提交说明中解释（例如测试或非界面用途）。

**测试**：全部现有像素测试通过（浅色不变）；新增 `drag_presentation_test::overlayUsesRegisteredColors`：以覆盖方式把相关 ID 改成特殊值，取样覆盖层像素等于这些值。

**状态**：未开始。

---

## 2.5 深色主题定稿、对比度测试与组件画廊

**目标**：Muxora Dark、Muxora Black 在所有组件、所有状态下协调可用，并且有可以重复执行的检查。

**涉及文件**：`themes/muxora-dark.json`、`themes/muxora-black.json`、`src/style/builtin_colors.cpp`；新建 `src/developer/component_gallery.{hpp,cpp}`（模块内，开发者命令）；新测试 `tests/theme_contrast_test.cpp`、`tests/component_gallery_test.cpp`。

**实现**：

1. **对比度测试**（`theme_contrast_test`，数据驱动）：组合清单写在测试数据中，每行为“前景 ID、背景 ID、最低对比度”。至少包括：`foreground` 对 `muxora.content.background`、`sideBar.background`、`titleBar.activeBackground`、`input.background`、`menu.background`、`quickInput.background`；`tab.activeForeground`／`tab.inactiveForeground` 对各自背景；`statusBar.foreground` 对 `statusBar.background`；`list.activeSelectionForeground` 对选中背景叠加后的颜色（带透明度的背景先与底色合成再计算）；`muxora.rail.tabForeground` 对 `muxora.rail.tabBackground`；每个标签颜色预设的前景对背景；`descriptionForeground` ≥ 3.0；`focusBorder` 对相邻背景 ≥ 3.0。对 Light、Dark、Black 都执行；高对比主题正文要求 ≥ 7.0（2.9）。
2. **状态完整性**：注册表声明每个组件的状态组（例如按钮：`button.background`、`button.hoverBackground`、按下、焦点、禁用）；测试检查每个状态组的 ID 都已注册。
3. **组件画廊**：命令 `developer.showComponentGallery` 打开一个工作区标签，网格展示：标签条（活动、非活动、悬停、带颜色、固定）、收纳条与临时面板、工具条（正常、悬停、按下、禁用、溢出）、菜单、快速输入（8.4 之后）、按钮、输入框（正常、焦点、错误）、复选框、下拉框、列表（悬停、选中、失焦选中）、滚动条、状态栏、拖拽覆盖层的各种目标。画廊用真实组件，不用截图拼图。
4. 命令 `developer.exportThemeGallery`：依次应用每个内置主题，把画廊 `grab()` 保存到 `out/diagnostics/theme-gallery/<主题>.png`，结束后恢复原主题。
5. 按设计 §6 的评审清单逐项检查截图，并据此调整 `muxora-dark.json`、`muxora-black.json` 和注册表的深色默认值。调整记录（改了哪些 ID、原因）写进提交说明。

**测试**：`component_gallery_test`：画廊能在三个主题下构建并显示，组件数量与清单一致；导出命令生成三个文件。

**完成标准**：对比度测试通过；三套截图经过评审（在状态中记录评审结论和截图路径）。

**状态**：未开始。

---

## 2.6 主题设置与切换命令（VS Code 逻辑）

**目标**：设计 §2 的全部设置和两个命令。

**前置**：2.5；选择主题的列表依赖 8.4 快速输入组件——8.4 未完成时先用现有命令面板容器实现，8.4 完成后替换。设置的持久化：阶段 4.1／4.4 已完成时使用设置注册表；否则先按界面语言的方式写入 `User/settings.json`（键名相同），4.4 时迁移进注册表，不改键名。

**涉及文件**：`window_manager.hpp/.cpp`（偏好状态）、新建 `src/style/theme_preference.{hpp,cpp}`、`src/commands/theme_commands.cpp`、`src/app/user_preferences.{hpp,cpp}`、`src/app/main.cpp`、`command_menus.cpp`（“视图 → 外观”子菜单）、测试 `tests/theme_preference_test.cpp`、`src/app/tests/user_preferences_test.cpp`。

**实现**：

1. 偏好结构包含设计 §2 表中的七个设置（`workbench.colorCustomizations` 在 2.8 处理）。
2. 解析函数是纯函数，单独测试：

   ```cpp
   QString resolveThemeLabel(const ThemePreference& preference, Qt::ColorScheme scheme, bool highContrast);
   ```

   规则：`autoDetectHighContrast && highContrast` → 高对比首选项（按系统深浅选深色或浅色高对比）；否则 `autoDetectColorScheme` → 按系统深浅选首选项；否则 `colorTheme`。主题名找不到时回退到 Muxora Light 并写诊断。
3. 命令 `workbench.action.selectTheme`（默认 `Ctrl+K Ctrl+T`）：分组列表、移动即预览、`Enter` 确认、`Esc` 恢复；跟随系统开启时写入当前深浅色对应的首选项，并在列表标题中说明（设计 §2.1）。
4. 命令 `workbench.action.toggleLightDarkThemes`（默认不绑定）：跟随系统开启时先关闭跟随，然后在浅色和深色首选项之间切换，并在状态栏提示一次。
5. 设置变化、系统深浅色变化（`QStyleHints::colorSchemeChanged`）、高对比变化（2.7）都触发重新解析；同一事件循环内多次变化只解析一次。
6. 应用：读取和保存这些设置；保存失败时回滚并显示诊断（参照语言设置的回滚写法）。

**测试**：解析函数的全部组合（数据驱动）；预览与恢复；跟随系统时选择主题写入的设置键；切换命令；设置非法值的诊断；同一轮多次变化只解析一次。

**状态**：未开始。

---

## 2.7 系统检测与原生深色

**涉及文件**：`src/platform/windows/window_frame.{hpp,cpp}`、新建 `src/platform/windows/system_appearance.{hpp,cpp}`（非 Windows 提供空实现）、`src/floating_panel.cpp`、`src/commands/popup_menu.cpp`、`tests/native_frame_test.cpp`。

**实现**：

1. 高对比检测：`SystemParametersInfoW(SPI_GETHIGHCONTRAST, ...)`；在一个隐藏的消息窗口（或现有顶层窗口的原生事件过滤）中监听 `WM_SETTINGCHANGE`（`lParam` 为 `"WindowsThemeElement"` 或高对比相关），变化时发信号。
2. 深色边框：主题类型为深色或深色高对比时，对每个顶层窗口（完整窗口、浮窗、浮动面板、菜单、快速输入）调用 `DwmSetWindowAttribute(handle, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &TRUE, sizeof(BOOL))`；旧系统返回错误时忽略，并记录到现有的原生帧诊断结构。调用时机：首次准备原生帧时、主题类型变化时。
3. 浮层的圆角和阴影在 8.3 统一处理，本步只处理深色属性。

**测试**：`native_frame_test::darkThemeSetsImmersiveDarkMode`（完整窗口、浮窗各一行）：切换到 Muxora Dark 后用 `DwmGetWindowAttribute` 读回为真，切回浅色为假。属于 `desktop` 层；运行时确保测试窗口未被遮挡。

**状态**：未开始。

---

## 2.8 用户颜色覆盖、用户主题文件与 Lua

**前置**：3.6（JSONC 模块）。

**实现**：

1. `workbench.colorCustomizations`：解析全局项与 `"[主题名]"` 作用域块（支持 `*` 通配）；优先级按设计 §2.2。非法 ID 或颜色写入诊断，其余照常生效。
2. 用户主题：应用读取 `User/themes/*.json`（JSONC 转成 `QJsonObject` 后交给 `parseColorTheme`），调用 `registerColorTheme`；文件变化时重新加载（与设置文件相同的监听）。
3. Lua：`muxora.theme.get()` 返回当前主题名、类型和解析后的全部颜色；`muxora.theme.update(colors)` 写入当前主题作用域的覆盖，脚本卸载时撤销；现有字段名（`background`、`accent` 等）通过设计 §5 的映射转换为颜色 ID，旧脚本继续可用。`lua-guide.md` 更新。

**测试**：覆盖优先级（全局、作用域、通配、主题、默认）；用户主题加载、继承内置主题、错误文件不影响其他主题；Lua 旧字段名兼容、卸载撤销。

**状态**：未开始。

---

## 2.9 高对比主题

**实现**：Muxora High Contrast（`hcDark`）与 Muxora High Contrast Light（`hcLight`）。规则参照 VS Code 高对比主题：所有交互元素有 1 像素 `contrastBorder`，焦点和活动元素用 `contrastActiveBorder`；不使用半透明背景表达状态；正文对比 ≥ 7.0。对比度测试加入这两个主题。

**状态**：未开始。

---

## 2.10 样式实现迁移（可选，先测量）

`theme.cpp` 中 `styleSheet()` 的注释说明：`QWidget::setStyle()` 只作用于单个控件，不随子控件增加和改挂传播；整棵控件树统一样式只能用 `QApplication::setStyle()`，而模块契约是不改变应用级样式。因此：

1. 先用阶段 0.1 基准和阶段 1.6 之后的数据确认样式表是否仍是明显成本。
2. 确有必要时，提供宿主可选的 `installApplicationStyle()`，模块默认仍用样式表；两条路径共用同一解析结果，用同一组像素测试覆盖。
3. 写成设计评审后再实施。

**状态**：未开始（需要数据）。
