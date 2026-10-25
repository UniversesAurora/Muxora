# 主题系统设计

状态：设计稿，2026-10-19。实现步骤见 [阶段 2 实现计划](../roadmap/implementation/phase-2-themes.md)。

Muxora 的主题行为对齐 VS Code：同样的设置名、同样的切换逻辑、同样的“颜色注册表 + 主题文件”结构。目的有两个：用户从 VS Code 过来不需要重新学习；每个控件的每种状态在浅色、深色、高对比主题下都有经过设计的颜色，不会出现“浅色下合适、深色下突兀”的组件。

## 1. 概念

| 概念 | 说明 |
| --- | --- |
| 颜色 ID | 语义化的颜色名，例如 `tab.activeBackground`、`list.hoverBackground`。界面代码只使用颜色 ID，不写具体颜色值。 |
| 颜色注册表 | 每个颜色 ID 注册时必须给出四种主题类型的默认值：`light`、`dark`、`hcDark`、`hcLight`。默认值可以是具体颜色，也可以是对其他颜色 ID 的引用或变换。 |
| 颜色主题 | 一份定义：名称、类型（四种之一）、可选的继承主题、以及若干颜色 ID 的取值。主题只需写出与默认值不同的部分。 |
| 解析结果 | 当前主题下每个颜色 ID 的最终颜色，主题切换时计算一次，界面直接查表。 |
| 用户覆盖 | 设置 `workbench.colorCustomizations` 中的颜色，可以对所有主题生效，也可以只对某个主题生效。 |

这与 VS Code 的颜色注册表（`colorRegistry`）和主题文件结构相同。差别只在于 Muxora 没有编辑器语法高亮的 `tokenColors`；Lua 编辑器的语法颜色以后用同样的方式另设一组 ID（`editorSyntax.*`），不引入 TextMate 规则。

### 1.1 默认值的写法

默认值支持以下形式，解析时递归求值，检测循环引用：

| 形式 | 含义 | 例子 |
| --- | --- | --- |
| `#rrggbb`、`#rrggbbaa` | 固定颜色 | `#1f1d24` |
| `ref(id)` | 引用另一个颜色 | `ref(focusBorder)` |
| `transparent(id, a)` | 乘以透明度 | `transparent(button.background, 0.4)` |
| `mix(id1, id2, t)` | 在两种颜色间按比例混合（用于从强调色生成柔和背景） | `mix(accent, sideBar.background, 0.18)` |
| `lighten(id, f)`、`darken(id, f)` | 在 HSL 空间调整亮度 | `lighten(accent, 0.12)` |
| `none` | 不绘制（透明） | |

这些变换与 VS Code 的 `transparent`、`lighten`、`darken`、`oneOf` 等同类。新增变换要写进本文档。

### 1.2 主题类型

| 类型 | 用途 | 内置主题 |
| --- | --- | --- |
| `light` | 浅色 | Muxora Light（现有紫罗兰浅色） |
| `dark` | 深色 | Muxora Dark（紫罗兰深色）、Muxora Black（近纯黑） |
| `hcDark` | 深色高对比 | Muxora High Contrast（后续） |
| `hcLight` | 浅色高对比 | Muxora High Contrast Light（后续） |

主题类型决定使用哪一组默认值，也决定原生部分（窗口边框、滚动条、系统菜单）使用深色还是浅色。

## 2. 设置与切换逻辑（与 VS Code 相同）

| 设置 | 类型 | 默认 | 含义 |
| --- | --- | --- | --- |
| `workbench.colorTheme` | 字符串 | `Muxora Light` | 当前主题。`window.autoDetectColorScheme` 关闭时生效。 |
| `window.autoDetectColorScheme` | 布尔 | `false` | 跟随系统深浅色。 |
| `workbench.preferredLightColorTheme` | 字符串 | `Muxora Light` | 跟随系统、系统为浅色时使用。 |
| `workbench.preferredDarkColorTheme` | 字符串 | `Muxora Dark` | 跟随系统、系统为深色时使用。 |
| `window.autoDetectHighContrast` | 布尔 | `true` | 系统开启高对比时自动切换到高对比主题（优先于上一组）。 |
| `workbench.preferredHighContrastColorTheme` | 字符串 | `Muxora High Contrast` | 系统高对比且为深色时使用。 |
| `workbench.preferredHighContrastLightColorTheme` | 字符串 | `Muxora High Contrast Light` | 系统高对比且为浅色时使用。 |
| `workbench.colorCustomizations` | 对象 | `{}` | 用户颜色覆盖，见 2.2。 |

主题用显示名称引用（与 VS Code 一致），内部再映射到稳定 ID；名称及设置引用同步更新，不保留旧名称别名。

### 2.1 选择主题的交互

- **“首选项：颜色主题”**（命令 `workbench.action.selectTheme`，默认 `Ctrl+K Ctrl+T`）：打开快速选择列表（[界面规范 §4.1](ui-design-system.md#41-快速输入quick-input)），按“浅色主题”“深色主题”“高对比主题”分组，当前主题有标记。用上下键移动时立即预览；`Enter` 确认并写入设置；`Esc` 恢复打开前的主题。
- 跟随系统开启时，从列表选择主题会写入**当前系统深浅色对应的那个首选项**（系统为深色就写 `workbench.preferredDarkColorTheme`），而不是写 `workbench.colorTheme`；列表标题显示“选择深色时使用的主题”之类的说明，避免用户选了却“不生效”。
- **“切换浅色／深色主题”**（命令 `workbench.action.toggleLightDarkThemes`，默认不绑定）：在 `preferredLightColorTheme` 和 `preferredDarkColorTheme` 之间切换。跟随系统开启时，这个命令先关闭跟随，再切换，并在状态栏提示一次。
- 设置页“外观”分类显示上表全部设置；主题下拉框按类型分组。
- 系统切换深浅色或高对比时，在下一次事件循环中重新解析，所有窗口同时更新。

### 2.2 用户颜色覆盖

与 VS Code 的 `workbench.colorCustomizations` 写法相同：

```jsonc
"workbench.colorCustomizations": {
    "statusBar.background": "#3b2752",          // 对所有主题生效
    "[Muxora Dark]": {                           // 只对该主题生效
        "tab.activeBorderTop": "#c39bff"
    },
    "[Muxora *]": { "focusBorder": "#b48af0" }   // 名称通配
}
```

优先级：主题内的作用域块 > 全局覆盖 > 主题定义 > 继承主题 > 注册表默认值。未知颜色 ID 和非法颜色在设置诊断中提示，不阻止其他项生效。

### 2.3 主题文件

内置主题以资源形式打包；用户主题放在 `User/themes/*.json`（JSONC）：

```jsonc
{
    "name": "My Violet Night",
    "type": "dark",
    "include": "Muxora Dark",     // 可选，继承一个已有主题
    "colors": {
        "editorGroupHeader.tabsBackground": "#18161c",
        "tab.activeBackground": "#221f28"
    }
}
```

文件修改后自动重新加载（与设置文件相同的监听和去抖动）。继承深度上限 8，循环继承报错。

Lua 的 `muxora.theme.update` 改为写入 `workbench.colorCustomizations` 的当前主题作用域（脚本卸载时撤销），`muxora.theme.get` 返回颜色 ID 对应的解析结果；同步更新调用方。

## 3. 深色配色的设计方法

用户要求深色主题“用深色去配深色”：不是把浅色主题的颜色取反，而是按深色界面的规律单独设计。规则如下。

### 3.1 层次靠亮度阶梯，不靠彩色

深色界面用中性灰阶表达层次：越“靠前”的表面越亮。Muxora Dark 使用带极轻紫色倾向（色相约 270°，饱和度 6–10%）的中性阶梯，强调色只用于少量需要注意的地方（活动指示线、焦点框、选中项、主要按钮）。

**Muxora Dark 中性阶梯**（初稿，以 §6 的对比度测试为准）：

| 级别 | 颜色 | 主要用途 |
| --- | --- | --- |
| N0 | `#141218` | 窗口最外层背景、收纳条 |
| N1 | `#18161c` | 侧栏、固定分区、临时展开面板、状态栏 |
| N2 | `#1d1a22` | 内容区（编辑器、终端背景） |
| N3 | `#24212a` | 标题行、工具条、输入框、标签栏背景 |
| N4 | `#2c2833` | 悬停背景 |
| N5 | `#36313e` | 非焦点选中、活动标签背景 |
| N6 | `#433d4d` | 强边框、分隔条悬停 |
| N7 | `#5a5266` | 弱化图标 |
| N8 | `#8e8698` | 弱化文字、占位符 |
| N9 | `#c9c3d1` | 次要文字 |
| N10 | `#e8e4ee` | 主要文字（不用纯白） |

强调色：`accent` `#a47fe0`，悬停 `#b796ea`，按下 `#9168d4`；柔和强调 `accent` 24% 透明度。深色下的强调色比浅色主题的 `#7545a7` 更亮、饱和度更低，才能在深底上保证对比又不刺眼。

**Muxora Black 中性阶梯**：

| 级别 | 颜色 | 级别 | 颜色 |
| --- | --- | --- | --- |
| N0 | `#000000` | N6 | `#2e2e33` |
| N1 | `#0a0a0b` | N7 | `#4a4a52` |
| N2 | `#000000` | N8 | `#85858f` |
| N3 | `#111113` | N9 | `#bdbdc6` |
| N4 | `#18181b` | N10 | `#e4e4ea` |
| N5 | `#222226` | | |

Black 的内容区与最外层同为纯黑，层次只靠标题行、输入框等少数抬升表面和 1 像素边框体现；强调色 `#9d6cdf`。

### 3.2 必须重新设计的部位

浅色主题中有几处大面积使用饱和紫色，直接搬到深色会非常突兀：

| 部位 | 浅色主题现状 | 深色主题做法 |
| --- | --- | --- |
| 状态栏 | 深紫 `#51366b` 整条 | 与侧栏同色（N1），上边 1 像素边框；只有特殊状态项（远程连接、录制等）用强调色背景，与 VS Code Dark Modern 一致 |
| 收纳条标签 | 饱和紫 `#6A568C` 色块 | N3 背景、N9 文字，活动项用 2 像素强调色指示线 |
| 活动标签 | 浅紫 `#ead9fa` | N2（与内容同色，标签“连”进内容），顶部 1–2 像素强调色线；非活动标签 N3、文字 N8 |
| 标签颜色预设 | 浅色底 + 深色字 | 背景为颜色与 N2 混合 18–28%，文字 N10，颜色本身用于指示线；保证每个预设与文字对比 ≥ 4.5 |
| 拖拽投放指示 | 浅紫填充 + 深紫边 | 强调色 20% 透明填充 + 强调色边；说明标签 N4 背景 |
| 选中项 | 浅紫 `#e0cbf2` | 焦点选中：强调色 35% 透明叠在背景上；非焦点选中：N5 |
| 阴影 | 黑色 4% 多层 | 黑色 36–48%（深色背景上浅阴影看不见） |

### 3.3 每个组件的每个状态

所有交互组件必须为以下状态各注册颜色（不需要的状态可以引用其他状态，但必须显式声明）：正常、悬停、按下、选中、焦点、禁用、选中且失焦。注册表测试会检查每个组件的状态集合是否完整（§6）。

## 4. 颜色 ID 清单

下面是第一批颜色 ID，名称尽量沿用 VS Code 的同名 ID（便于用户迁移习惯和查文档）；Muxora 特有的部分用自己的前缀。每个 ID 的四类默认值在实现时写入注册表声明文件。

| 分组 | 颜色 ID |
| --- | --- |
| 基础 | `foreground`、`descriptionForeground`、`disabledForeground`、`errorForeground`、`focusBorder`、`contrastBorder`、`contrastActiveBorder`、`widget.border`、`widget.shadow`、`selection.background`、`icon.foreground`、`sash.hoverBorder` |
| 窗口与标题 | `window.background`、`window.activeBorder`、`window.inactiveBorder`、`titleBar.activeBackground`、`titleBar.activeForeground`、`titleBar.inactiveBackground`、`titleBar.inactiveForeground`、`titleBar.border` |
| 菜单 | `menubar.selectionBackground`、`menubar.selectionForeground`、`menu.background`、`menu.foreground`、`menu.selectionBackground`、`menu.selectionForeground`、`menu.separatorBackground`、`menu.border` |
| 工具条 | `toolbar.hoverBackground`、`toolbar.activeBackground`、`toolbar.hoverOutline`、`muxora.toolbarStrip.background`、`muxora.toolbarStrip.handle`、`muxora.toolbarStrip.dropIndicator` |
| 标签 | `editorGroupHeader.tabsBackground`、`editorGroupHeader.tabsBorder`、`tab.activeBackground`、`tab.activeForeground`、`tab.activeBorderTop`、`tab.inactiveBackground`、`tab.inactiveForeground`、`tab.hoverBackground`、`tab.hoverForeground`、`tab.unfocusedActiveBackground`、`tab.unfocusedActiveForeground`、`tab.border`、`tab.dragAndDropBorder`、`muxora.tab.scrollbar`、`muxora.tab.closeHoverBackground` |
| 分组与分割 | `editorGroup.border`、`editorGroup.dropBackground`、`muxora.surface.edge`（表面轮廓影，见 [表面与阴影设计](surface-elevation-design.md#21-不同主题)）、`muxora.paneSplitter.background`、`muxora.groupSplitter.background` |
| 侧栏与收纳条 | `sideBar.background`、`sideBar.foreground`、`sideBar.border`、`sideBarSectionHeader.background`、`muxora.rail.background`、`muxora.rail.tabBackground`、`muxora.rail.tabForeground`、`muxora.rail.tabHoverBackground`、`muxora.rail.activeIndicator`、`muxora.peek.background`、`muxora.peek.border`、`muxora.peek.headerBackground` |
| 窗格附加栏 | `muxora.paneBar.background`、`muxora.paneBar.foreground`、`muxora.paneBar.border` |
| 状态栏 | `statusBar.background`、`statusBar.foreground`、`statusBar.border`、`statusBarItem.hoverBackground`、`statusBarItem.prominentBackground`、`statusBarItem.prominentForeground`、`statusBarItem.errorBackground`、`statusBarItem.warningBackground` |
| 列表与树 | `list.activeSelectionBackground`、`list.activeSelectionForeground`、`list.inactiveSelectionBackground`、`list.inactiveSelectionForeground`、`list.hoverBackground`、`list.focusOutline`、`list.highlightForeground`、`list.disabledForeground`、`tree.indentGuidesStroke` |
| 输入控件 | `input.background`、`input.foreground`、`input.border`、`input.placeholderForeground`、`inputValidation.errorBackground`、`inputValidation.errorBorder`、`inputValidation.warningBackground`、`inputValidation.warningBorder`、`inputOption.activeBackground`、`inputOption.activeBorder` |
| 按钮 | `button.background`、`button.foreground`、`button.hoverBackground`、`button.secondaryBackground`、`button.secondaryForeground`、`button.secondaryHoverBackground`、`button.border` |
| 选择控件 | `checkbox.background`、`checkbox.foreground`、`checkbox.border`、`checkbox.selectBackground`、`dropdown.background`、`dropdown.foreground`、`dropdown.border`、`dropdown.listBackground` |
| 快速输入 | `quickInput.background`、`quickInput.foreground`、`quickInputTitle.background`、`quickInputList.focusBackground`、`quickInputList.focusForeground`、`pickerGroup.foreground`、`pickerGroup.border`、`keybindingLabel.background`、`keybindingLabel.foreground`、`keybindingLabel.border`、`keybindingLabel.bottomBorder` |
| 通知与提示 | `notifications.background`、`notifications.foreground`、`notifications.border`、`notificationsErrorIcon.foreground`、`notificationsWarningIcon.foreground`、`notificationsInfoIcon.foreground`、`editorHoverWidget.background`、`editorHoverWidget.border`（工具提示） |
| 徽标与进度 | `badge.background`、`badge.foreground`、`progressBar.background` |
| 滚动条 | `scrollbar.shadow`、`scrollbarSlider.background`、`scrollbarSlider.hoverBackground`、`scrollbarSlider.activeBackground` |
| 拖拽投放 | `muxora.dropTarget.background`、`muxora.dropTarget.border`、`muxora.dropGuide.background`、`muxora.dropGuide.border`、`muxora.dropGuide.selectedBackground`、`muxora.dropGuide.glyph`、`muxora.dropLabel.background`、`muxora.dropLabel.foreground` |
| 设置与快捷键页 | `settings.headerForeground`、`settings.modifiedItemIndicator`、`settings.rowHoverBackground`、`settings.focusedRowBackground`、`settings.focusedRowBorder`、`settings.sashBorder`、`keybindingTable.headerBackground`、`keybindingTable.rowsBackground` |
| 标签颜色 | `muxora.tabColor.<预设>.background`、`.foreground`、`.indicator`（每个预设一组，四类默认值） |
| 终端（阶段 7） | `terminal.background`、`terminal.foreground`、`terminal.selectionBackground`、`terminalCursor.foreground`、`terminal.ansiBlack` … `terminal.ansiBrightWhite` 共 16 色 |

## 5. 与现有 `Theme` 结构的关系

公开结构 `window_system::Theme`（`theme.hpp`）作为控件使用的类型化视图，由解析结果填充，各字段对应一个颜色 ID：

| 现有字段 | 对应颜色 ID |
| --- | --- |
| `background` | `window.background` |
| `surface` | `muxora.content.background`（新增：标签内容与窗格的背景） |
| `raisedSurface` | `muxora.raisedSurface` |
| `headerBackground` / `headerText` | `titleBar.activeBackground` / `titleBar.activeForeground` |
| `text` / `mutedText` | `foreground` / `descriptionForeground` |
| `border` | `widget.border` |
| `accent` / `accentHover` / `accentSoft` | `button.background` / `button.hoverBackground` / `muxora.accentSoft` |
| `buttonHover` | `toolbar.hoverBackground` |
| `activeTab` / `inactiveTab` | `tab.activeBackground` / `tab.inactiveBackground` |
| `edgeTabBackground` / `edgeTabHover` / `edgeTabText` | `muxora.rail.tabBackground` / `muxora.rail.tabHoverBackground` / `muxora.rail.tabForeground` |
| `statusBackground` / `statusText` | `statusBar.background` / `statusBar.foreground` |
| `selection` / `selectionText` | `list.activeSelectionBackground` / `list.activeSelectionForeground` |
| `focus` | `focusBorder` |
| `danger` | `errorForeground` |
| `checkedIndicatorText` | `checkbox.foreground` |

`setTheme(const Theme&)` 继续可用：传入的字段转换为一组临时颜色覆盖。宿主若需要更多颜色，使用新的按 ID 查询接口。

## 6. 保证配色协调的机制

1. **注册即完整**：注册一个颜色 ID 时必须提供四类默认值，否则注册失败。自动化测试遍历注册表，检查每个 ID 在四类主题下都能解析为有效颜色、无循环引用。
2. **对比度测试**：以下组合在每个内置主题下检查 WCAG 对比度：正文文字与所在背景 ≥ 4.5；大号或粗体标题 ≥ 3.0；图标、边框、焦点框等非文字元素与相邻背景 ≥ 3.0；高对比主题正文 ≥ 7.0。组合清单与颜色 ID 一起维护在测试数据中。
3. **状态完整性**：每个交互组件的状态集合（§3.3）在注册表中有声明，测试检查没有遗漏。
4. **组件画廊**：开发用页面（命令 `developer.showComponentGallery`）把所有组件的所有状态并排展示；命令 `developer.exportThemeGallery` 把每个内置主题下的画廊截图导出到 `out/diagnostics/theme-gallery/<主题>.png`，用于评审。任何新增组件都要先加入画廊。
5. **评审清单**：每个涉及颜色的改动，提交前对照截图检查：没有饱和色大面积铺底；深色下没有比周围亮很多的“补丁”；同一层级表面颜色一致；选中、悬停、焦点能区分且不刺眼；禁用状态看得出但不消失。

## 7. 原生部分

- **窗口边框**：主题类型为深色时，对每个顶层窗口设置 `DWMWA_USE_IMMERSIVE_DARK_MODE`；浮动面板、弹出菜单等自建顶层窗口同样处理。
- **系统高对比**：平台层通过 `SystemParametersInfo(SPI_GETHIGHCONTRAST)` 检测，并监听 `WM_SETTINGCHANGE`。
- **系统对话框**：原生文件对话框、颜色对话框跟随 Windows 的应用主题设置，不跟随 Muxora 主题，这是系统行为；能用应用内组件替代的场合（例如颜色选择）优先用自己的组件。
- **滚动条与菜单**：由 Muxora 自绘或样式化，跟随主题。

## 8. 性能

- 解析在主题或覆盖变化时进行一次，结果是按颜色索引排列的数组。组件在注册时拿到颜色索引，绘制时 O(1) 取色，不做字符串查找。
- 主题变化时只通知一次所有窗口；样式表从解析结果生成一次并复用。
- 主题文件和覆盖的解析在 GUI 线程完成即可（规模很小），但读文件用与设置文件相同的异步监听。
