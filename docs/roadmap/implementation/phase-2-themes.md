# 阶段 2：主题（深色、黑色、跟随系统）

**目的**：提供 Violet Dark 和 Black 两套内置深色主题，可以固定使用某一套，也可以跟随系统并分别指定浅色、深色时使用的主题；所有自绘部分都从主题取色。

**阶段约束**：

- 模块仍然以值类型 `Theme` 为核心，`WindowManager::setTheme()` 的语义不变；宿主可以继续传入任意主题。
- 模块不读写宿主配置文件。主题偏好的保存方式与界面语言相同：模块提供状态和信号，应用负责写入 `User/settings.json`。
- 本阶段沿用现有的样式表路径（所有颜色已经来自令牌），不迁移到 `QProxyStyle`。原因见 2.7。
- 颜色值在下面给出初稿。实现时以对比度测试（2.2）为准，可以微调，但调整必须让测试通过，不能放宽阈值。

---

## 2.1 补齐主题令牌，去掉硬编码颜色

**目标**：拖拽投放指示、投放说明标签和拖动阴影都从 `Theme` 取色。默认主题下外观像素不变。

**涉及文件**：`include/window_system/theme.hpp`、`src/style/theme.cpp`、`src/docking/drag_controller.cpp`（约 53–92 行的 `paintEvent`）、`src/docking/drag_presentation.cpp`（约 41–46 行的阴影）、`window-system-lua/src/appearance_api.cpp`（`themeColors` 表）、`tests/drag_presentation_test.cpp`。

**实现**：

1. `Theme` 增加字段，默认值等于当前硬编码值，保证默认外观不变：

   | 字段 | 默认值 | 原位置 |
   | --- | --- | --- |
   | `dropPreviewFill` | `QColor(138, 82, 199, 62)` | 投放区域填充 |
   | `dropPreviewBorder` | `#8A52C7` | 投放区域边框、指示内块 |
   | `dropGuideBorder` | `#725591` | 指示方块边框 |
   | `dropGuideBackground` | `#FAF8FD` | 指示方块背景 |
   | `dropGuideSelected` | `#D9BDF5` | 选中方块背景 |
   | `dropLabelBackground` | `#4D356E` | 说明标签背景 |
   | `dropLabelText` | `#FFFFFF` | 说明标签文字 |
   | `shadow` | `QColor(0, 0, 0, 10)` | 拖动标签阴影每层颜色 |

2. 再增加两个元数据字段：`QString id = QStringLiteral("violet-light");`、`bool dark = false;`。`name` 仍是显示名。
3. `drag_controller.cpp` 的投放覆盖层需要主题：它是 `DragController` 创建的窗口，`DragController` 持有 `manager_`，在创建或每次 `paintEvent` 时读取 `manager_->theme()`（覆盖层生命周期短，直接在绘制时读取即可）。
4. `drag_presentation.cpp` 中 `DragLabelPreview` 构造时传入 `Theme`（调用点已经有主题可用），保存 `shadow` 颜色。
5. `appearance_api.cpp` 的 `themeColors` 表加入新字段名（`drop_preview_fill` 等，沿用该表现有的命名风格——先看表中已有条目的写法再决定），并更新 `docs/development/lua-guide.md` 的主题字段列表。
6. 全仓库再搜一次硬编码颜色，确认只剩主题定义、标签颜色预设（2.3 处理）和测试：

   ```bash
   grep -rnE "QColor\(\"#|QColor\([0-9]+, *[0-9]+|Qt::white|Qt::black" modules/window-system/src src
   ```

**测试**：`drag_presentation_test` 中增加 `dropOverlayUsesThemeTokens`：设置一个各字段都不同的测试主题，触发拖拽到中心目标，对覆盖层 `grab()` 取样，断言预览边框和说明标签背景像素等于主题值。原有像素断言保持（默认值未变）。

**验证**：`window-system.drag-presentation`、`window-system.drag-interaction`、`window-system.strip-drag`、`window-system.lua`。

**完成标准**：默认主题下现有像素测试全部通过；grep 结果中没有遗漏的界面硬编码颜色（每个剩余位置在提交说明中解释）。

**状态**：未开始。

---

## 2.2 内置主题目录

**目标**：模块提供 `violet-light`、`violet-dark`、`black` 三套主题，并有自动化的对比度检查。

**涉及文件**：`theme.hpp`、`theme.cpp`、新测试 `tests/theme_catalog_test.cpp`（model 层，只链接 `WindowSystem::Widgets` 而不创建控件；`QColor` 计算不需要窗口）。

**实现**：

1. 公开函数：

   ```cpp
   [[nodiscard]] QVector<Theme> builtInThemes();               // 顺序固定：violet-light, violet-dark, black
   [[nodiscard]] std::optional<Theme> builtInTheme(QStringView id);
   ```

   `defaultTheme()` 保持返回 `violet-light`。

2. **Violet Dark** 初稿：

   | 字段 | 值 | 字段 | 值 |
   | --- | --- | --- | --- |
   | background | `#1e1a24` | surface | `#17141c` |
   | raisedSurface | `#241f2c` | headerBackground | `#2a2433` |
   | headerText | `#e8e2f0` | text | `#e4def0` |
   | mutedText | `#9a90a8` | border | `#3d3448` |
   | accent | `#a77bdb` | accentHover | `#b991e6` |
   | accentSoft | `#3a2d4d` | buttonHover | `#332a40` |
   | activeTab | `#3a2f4a` | inactiveTab | `#2a2433` |
   | edgeTabBackground | `#4a3a66` | edgeTabHover | `#5d4a80` |
   | edgeTabText | `#ffffff` | statusBackground | `#3b2752` |
   | statusText | `#f2ecf8` | selection | `#4b3866` |
   | selectionText | `#ffffff` | focus | `#b98ae6` |
   | danger | `#ff5c72` | checkedIndicatorText | `#ffffff` |
   | dropPreviewFill | `rgba(167,123,219,70)` | dropPreviewBorder | `#a77bdb` |
   | dropGuideBorder | `#8a6cb0` | dropGuideBackground | `#241f2c` |
   | dropGuideSelected | `#4b3866` | dropLabelBackground | `#3b2752` |
   | dropLabelText | `#ffffff` | shadow | `rgba(0,0,0,40)` |

3. **Black** 初稿（纯黑背景，强调色保持紫色系）：

   | 字段 | 值 | 字段 | 值 |
   | --- | --- | --- | --- |
   | background | `#000000` | surface | `#000000` |
   | raisedSurface | `#0d0d0f` | headerBackground | `#0a0a0c` |
   | headerText | `#e6e6e6` | text | `#e0e0e0` |
   | mutedText | `#8a8a8f` | border | `#26262b` |
   | accent | `#9d6cdf` | accentHover | `#b48af0` |
   | accentSoft | `#1f1630` | buttonHover | `#18181c` |
   | activeTab | `#1c1526` | inactiveTab | `#0a0a0c` |
   | edgeTabBackground | `#1c1526` | edgeTabHover | `#2a1f3a` |
   | edgeTabText | `#f0f0f0` | statusBackground | `#0a0a0c` |
   | statusText | `#d0d0d0` | selection | `#3a2a55` |
   | selectionText | `#ffffff` | focus | `#a77bdb` |
   | danger | `#ff4d5e` | checkedIndicatorText | `#ffffff` |
   | dropPreviewFill | `rgba(157,108,223,60)` | dropPreviewBorder | `#9d6cdf` |
   | dropGuideBorder | `#5a4580` | dropGuideBackground | `#0d0d0f` |
   | dropGuideSelected | `#3a2a55` | dropLabelBackground | `#1c1526` |
   | dropLabelText | `#ffffff` | shadow | `rgba(0,0,0,60)` |

   两套主题的 `metrics` 与默认主题相同。

4. 对比度辅助函数放在测试中（不进公开 API）：按 WCAG 2.x 公式计算相对亮度和对比度。

**测试**（`theme_catalog_test.cpp`）：

- `idsAreUniqueAndLookupWorks`：三个 ID 唯一，`builtInTheme("violet-dark")->dark == true`，未知 ID 返回空。
- `textContrastMeetsMinimum_data/…`：对每套主题检查下列组合对比度 ≥ 4.5：`text/surface`、`text/raisedSurface`、`headerText/headerBackground`、`statusText/statusBackground`、`selectionText/selection`、`edgeTabText/edgeTabBackground`、`dropLabelText/dropLabelBackground`；`mutedText/surface` ≥ 3.0；`focus/surface` ≥ 3.0（非文字元素）。
- `defaultThemeUnchanged`：`defaultTheme()` 各字段与 2.1 之前的值相同（防止误改默认主题）。

**完成标准**：测试通过；如需改色以满足对比度，在本文档表格中同步更新数值。

**状态**：未开始。

---

## 2.3 标签颜色预设的深色变体

**目标**：深色主题下，带颜色的标签使用深底浅字的配色。

**涉及文件**：`include/window_system/tab_appearance.hpp`、`src/style/tab_appearance.cpp`（`defaultTabColorPresets()`、JSON 读写）、`src/style/tab_appearance_p.hpp`、使用预设绘制标签的位置（`tab_strip.cpp`、`edge_tab_button.cpp` 中读取 `TabColorPreset` 的代码）、`tests/tab_appearance_test.cpp`。

**实现**：

1. `TabAppearance` 增加 `QVector<TabColorPreset> darkColors = defaultDarkTabColorPresets();`，新函数 `defaultDarkTabColorPresets()` 与浅色预设使用相同 ID 和名称。
2. 深色预设生成：沿用 `defaultTabColorPresets()` 中的 `seeds`，规则为
   - 活动：背景 = 强调色与 `#17141c` 按 35% 混合；文字 `#f2f2f2`；线 = 强调色提亮 130%；图标 = 文字色；
   - 非活动：背景 = 强调色与 `#17141c` 按 20% 混合；文字 `#d8d8d8`；线 = 强调色；图标 = `#bdbdbd`。
   混合后用 `QColor::name(QColor::HexArgb)` 回写，避免 16 位通道在保存恢复后变化（现有代码注释说明了这个问题）。
3. 新增内部函数 `const QVector<TabColorPreset>& activePresets(const TabAppearance&, const Theme&)`：`theme.dark` 时返回 `darkColors`，否则 `colors`。所有按颜色 ID 取预设的地方改用它。`WindowManager::tabColorPreset(id)` 按当前主题返回。
4. JSON：`tabAppearance` 增加 `darkColors` 数组，缺省时使用默认深色预设；旧布局可以正常读取。写入时仅当与默认不同才写出（与 `colors` 的现有处理方式一致——先确认现有代码的写法）。
5. 设置页的“标签颜色”编辑器（`settings_tab_appearance.cpp`）编辑当前主题对应的那一组，并在标题上注明“深色主题”或“浅色主题”。

**测试**：`tab_appearance_test` 增加 `darkPresetsFollowTheme`：切换到 `violet-dark` 后，给标签设置 `blue`，取样标签背景等于深色预设；切回浅色等于浅色预设；`darkColors` 保存恢复后逐字段相等；旧快照（没有 `darkColors`）恢复为默认深色预设。对比度：深色预设文字与背景 ≥ 4.5。

**状态**：未开始。

---

## 2.4 主题偏好与跟随系统

**目标**：可以选择固定主题，或跟随系统并分别指定浅色、深色时的主题；系统切换深浅色时立即生效。

**涉及文件**：`theme.hpp`（偏好结构）、`window_manager.hpp/.cpp`（偏好状态与信号）、`window_manager_p.hpp`、`src/app/user_preferences.{hpp,cpp}`、`src/app/main.cpp`、`window-system-lua/src/appearance_api.cpp`、新测试 `tests/theme_preference_test.cpp`、`src/app/tests/user_preferences_test.cpp`。

**实现**：

1. 偏好结构：

   ```cpp
   struct ThemePreference {
       bool followSystem = false;
       QString theme = QStringLiteral("violet-light");      // followSystem == false 时使用
       QString lightTheme = QStringLiteral("violet-light");
       QString darkTheme = QStringLiteral("violet-dark");
       bool operator==(const ThemePreference&) const = default;
   };
   ```

2. `WindowManager` 新增：

   ```cpp
   bool registerTheme(Theme theme);                  // 宿主或 Lua 增加的主题；ID 不能与内置重复
   [[nodiscard]] QVector<Theme> availableThemes() const;   // 内置 + 已注册
   bool setThemePreference(const ThemePreference& preference); // ID 不存在时返回 false，不改变状态
   [[nodiscard]] ThemePreference themePreference() const;
   signals:
   void themePreferenceChanged(const window_system::ThemePreference& preference);
   ```

3. 解析规则：`followSystem` 为真时，读取 `QGuiApplication::styleHints()->colorScheme()`；`Qt::ColorScheme::Dark` 用 `darkTheme`，其他（包括 `Unknown`）用 `lightTheme`。得到主题后调用现有的 `setTheme()`。
4. 构造函数中连接 `QStyleHints::colorSchemeChanged`（接收者为管理器自身），只有 `followSystem` 时才重新解析。
5. **与直接 `setTheme()` 的关系**：宿主或 Lua 直接调用 `setTheme()` 时，视为一次临时覆盖，不修改偏好；下一次偏好变化或系统切换时按偏好重新解析。在头文件注释中写明。Lua 的 `theme.update` 保持现有语义（修改当前主题）。
6. 应用侧：
   - `UserPreferences` 增加 `themePreference()`、`saveThemePreference()`，写入 `settings.json` 的四个键：`workbench.colorTheme`、`window.autoDetectColorScheme`、`workbench.preferredLightColorTheme`、`workbench.preferredDarkColorTheme`（与 VS Code 同名）。读取时校验类型，非法值回退默认并记录诊断，与语言设置的处理方式一致。
   - `main.cpp`：启动时读取偏好并调用 `setThemePreference()`；连接 `themePreferenceChanged` 保存，保存失败时回滚并显示诊断（参照语言设置的回滚写法）。
7. 命令：新增 `view.theme.select`（打开命令面板中的主题列表，选中即应用，Esc 恢复原主题——即 VS Code 的预览行为）和 `view.theme.toggleDark`（在当前浅色／深色偏好之间切换）。注册到“视图 → 外观”子菜单。快捷键默认不绑定。

**测试**（`theme_preference_test.cpp`）：

- `fixedPreferenceAppliesTheme`、`unknownIdIsRejectedAtomically`。
- `followSystemTracksColorScheme`：Qt 6.8 起可以用 `QStyleHints::setColorScheme()` 在测试中模拟系统切换；若当前 Qt 版本的平台插件不支持，测试内用 `QSKIP` 是不允许的——改为把解析逻辑抽成纯函数 `resolveTheme(preference, Qt::ColorScheme, themes)` 单独测试，再用一个信号注入的私有入口验证管理器会重新解析。
- `directSetThemeIsTemporary`：`setTheme(custom)` 后改偏好，主题回到偏好结果。
- `selectCommandPreviewsAndRestores`：命令面板中移动选择时主题跟随，Esc 后恢复。
- 应用 `user_preferences_test`：四个键的读写、非法值、文件缺失、写入失败。

**验证**：新套件、`window-system.floating-panel`、`window-system.menu-structure`（新菜单项）、`muxora.user-preferences`、`window-system.lua`。

**状态**：未开始。

---

## 2.5 原生深色边框

**目标**：深色主题下，Windows 的窗口边框、系统菜单、Snap 弹窗和滚动条等原生部分使用深色。

**涉及文件**：`src/platform/windows/window_frame.{hpp,cpp}`、`src/platform/windows/window_menu.cpp`、`src/chrome/workspace_window.cpp`（应用主题的位置）、`src/commands/floating_panel.cpp`、`src/commands/popup_menu.cpp`、`tests/native_frame_test.cpp`。

**实现**：

1. `window_frame.hpp` 增加 `void applyNativeColorScheme(QWidget* window, bool dark);`。Windows 实现：

   ```cpp
   constexpr DWORD immersiveDarkMode = 20; // DWMWA_USE_IMMERSIVE_DARK_MODE（Windows 10 20H1 起）
   const BOOL value = dark ? TRUE : FALSE;
   DwmSetWindowAttribute(handle, immersiveDarkMode, &value, sizeof(value));
   ```

   旧系统返回错误时忽略，与现有圆角属性的处理方式一致（见 `applyNativeFrame` 中的注释），并把结果记入现有的 `state->configuration` 诊断结构，方便测试读取。
2. 调用时机：窗口首次准备原生帧时（`applyNativeFrame` 之后），以及 `WindowManager::setTheme()` 遍历窗口应用主题时。浮动面板（`FloatingPanel`）和弹出菜单（`DismissOnlyMenu`）在显示前调用。
3. 非 Windows 平台提供空实现，不加条件编译到调用方。
4. 现有边框颜色属性（`DWMWA_BORDER_COLOR` = 34）目前设置为默认／无；深色主题下是否改用 `theme.border`，在实际桌面上对比后决定，本步先不改。

**测试**：`native_frame_test` 增加 `darkThemeSetsImmersiveDarkMode`：切换到 `violet-dark` 后，用 `DwmGetWindowAttribute(handle, 20, ...)` 读回为真；切回浅色为假。浮动窗口和完整窗口各一行。该测试属于 `desktop` 层；运行时确保测试窗口没有被其他程序遮挡。

**状态**：未开始。

---

## 2.6 外观设置

**目标**：设置页增加“外观”分类，可以选择主题和跟随系统。

**涉及文件**：`src/settings/settings_page.{hpp,cpp}`、`tests/pane_settings_test.cpp` 或新增 `tests/appearance_settings_test.cpp`、两个翻译文件。

**实现**：

1. 在“应用”分类之后加入分类 `appearance`（“外观”），包含：
   - 复选框“跟随系统深浅色”（`window.autoDetectColorScheme`）；
   - 下拉框“颜色主题”（`workbench.colorTheme`），跟随系统时禁用；
   - 下拉框“浅色时使用”“深色时使用”，不跟随系统时禁用。
   列表内容来自 `availableThemes()`，显示 `name`，数据为 `id`。深色主题条目后面加“（深色）”。
2. 修改立即调用 `setThemePreference()`；监听 `themePreferenceChanged` 同步控件（用 `QSignalBlocker` 防止循环），与现有 `syncControls()` 的做法一致。
3. 搜索词包含设置 ID 和“主题”“深色”“dark”“theme”，保证搜索能找到。

**测试**：`appearanceSettingsDriveThePreference`：操作三个控件，断言 `themePreference()` 变化、窗口主题变化；外部调用 `setThemePreference()` 后控件同步；搜索“dark”能定位到该分类。

**状态**：未开始。

---

## 2.7 样式实现迁移（可选，先测量）

**背景**：审查记录 R6 建议把样式表换成调色板加 `QProxyStyle`。但 `theme.cpp` 中 `styleSheet()` 的注释说明了一个限制：`QWidget::setStyle()` 只作用于单个控件，不会随子控件的增加和改挂传播；要让整棵控件树使用同一个样式，只能用 `QApplication::setStyle()`，而模块的契约是“不改变应用级样式”。

**做法**：

1. 先用 0.1 基准和 1.6 之后的数据确认样式表是否仍是明显成本（例如创建 1000 个标签时 polish 的耗时占比，可以用 `QElapsedTimer` 包住 `applyTheme()` 和首次显示测量）。改挂次数在 1.6 之后已大幅减少，样式表的成本可能已经不重要。
2. 确实需要时，提供**可选**的应用级入口：`void installApplicationStyle(const Theme&)`，只有拥有整个应用的宿主（Muxora）调用；模块默认仍然使用样式表。两条路径共用同一套令牌，并用同一组像素测试覆盖。
3. 这一步写成单独的设计评审，不在没有数据时实施。

**状态**：未开始（需要先完成 1.6 并取得数据）。
