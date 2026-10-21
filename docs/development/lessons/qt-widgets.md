# Qt Widgets 经验

适用于 Qt 6.12（Windows）。工具链要求见 [工程设计](../engineering-design.md)。

## 焦点

### 等窗口真正激活后再输入，模态登记不代表已经取得焦点

- **现象**：尺寸对话框已经进入 `activeModalWidget()`，测试输入后按 Tab，`height->hasFocus()` 仍为 false；稍后窗口自然激活，高度框才取得真实焦点。
- **原因**：Qt 6.12 的窗口激活是异步的。窗口未激活时，`setFocus()` 只记下焦点子控件，等窗口激活才更新实际键盘焦点；模态登记和显示事件都不保证这个过程已经结束。依据见 [QDialog 显示实现](https://github.com/qt/qtbase/blob/6.12/src/widgets/dialogs/qdialog.cpp#L733)、[QWidget 焦点契约与实现](https://github.com/qt/qtbase/blob/6.12/src/widgets/kernel/qwidget.cpp#L6019)。
- **做法**：快捷键测试先等待宿主窗口激活。对话框输入在原看门狗范围内等待自然激活及初始输入框取得焦点，不强制激活、不用补设焦点挽救失败；就绪后仍立即断言 Tab 的焦点结果。
- **验证**：`PeekSizeTest::shortcutOpensAKeyboardEditableSizeDialog` 在旧流程下失败。修正前提后，原断言和看门狗保留；初始焦点错误、Tab 顺序错误及显示不激活的反向条件仍分别失败。完整记录、评审及运行范围见 [验证日志](../verification-log/2026-10.md#a2-peek-size-夹具前提修正与反向验证2026-10-22)。

## 原生窗口

### 不要对还没有句柄的弹窗调用 `winId()`

- **现象**：把压缩状态的菜单条从下方区域拖到上方一行，再在溢出菜单里悬停被隐藏的菜单，之后整个窗口状态异常：状态栏只剩背景色、按钮没有悬停高亮、最大化按钮不再出现 Snap 浮窗，只能重启恢复。
- **原因**：`QWidget::winId()` 在控件还没有原生句柄时会给它设置 `Qt::WA_NativeWindow`。即使这个控件本身是顶层弹窗，Qt 也会对它的父控件调用 `enforceNativeChildren()`，把父控件的所有子控件标成原生窗口（除非设置了 `Qt::AA_DontCreateNativeWidgetSiblings`）。已经创建过的子控件会立刻得到原生句柄，并沿着父链向上、连同各级兄弟控件一起变成原生子窗口。原生子窗口会自己接收 `WM_NCHITTEST`、自己绘制，于是窗口的命中测试、绘制和悬停都乱了。
  本例中菜单的父控件是菜单栏。菜单栏在下方区域被压窄时显示过扩展按钮 `qt_menubar_ext_button`，这个按钮因此已经创建；`preparePopupFrame()` 在 `aboutToShow` 里调用 `winId()`，于是从扩展按钮开始，15 个控件变成了原生窗口。
- **做法**：
  - 设置原生窗口属性（DWM 圆角、边框颜色、窗口样式）时只用 `internalWinId()`；为 0 就跳过，等 `QEvent::Show` 或 `showEvent()` 再处理。Qt 在发送 Show 事件前已经创建好句柄，又还没有真正显示窗口，所以在这里设置不会闪。
  - 只有父控件为空或父控件本身是顶层窗口时，调用 `winId()` 才安全，例如 `PeekSizeDialog`。
  - 父控件是普通子控件的弹窗都要按上面处理：菜单栏的菜单、组合框的下拉列表、工具按钮的菜单。
- **验证**：`CaptionStripTest::compressedMenuStripMovedUpKeepsTheWindowAlien` 按用户的操作重现，检查窗口内没有任何控件带原生句柄或 `WA_NativeWindow` 标记；`ToolbarPopupTest::hoveredOverflowMenusKeepTheWindowAlien` 在小夹具里检查同一条件，并确认方角策略仍然作用到菜单句柄上。两个测试都确认过在修复前失败。
- **出处**：2026-10-20，`window-system/menus` 修复；排查过程见 [调试方法](debugging.md#用调试器抓某个事件的调用栈)。

### `QMenu` 关闭后句柄会被销毁，`aboutToShow` 比重新创建句柄更早

- **现象**：第一次打开菜单一切正常，第二次打开时才出问题，夹具里的测试只开一次菜单就测不出来。
- **原因**：Qt 6.12 在菜单关闭时销毁其原生句柄（`WA_WState_Created` 变回 false，`internalWinId()` 变为 0）。再次打开时先发 `aboutToShow`，之后才创建新句柄。
- **做法**：不要假设弹窗在 `aboutToShow` 时已有句柄；和弹窗句柄有关的测试至少开、关、再开一次。
- **验证**：上一条的两个测试都开两轮。

## 弹出菜单

### 菜单打开期间，`QMenuBar` 只按位置接收指针移动

- **现象**：压缩的菜单条把部分菜单裁掉后，先打开一个可见菜单，再把鼠标移到被裁掉的区域，看不见的菜单仍然会被打开。
- **原因**：菜单打开时，Qt 把指针移动按坐标转发给菜单栏，不检查那块区域是否可见。
- **做法**：在菜单栏上装事件过滤器，只吞掉落在 `visibleRegion()` 之外的鼠标事件；其他事件（包括右键菜单）一律放行，否则会吃掉窗口自己的上下文菜单。
- **验证**：`ToolbarPopupTest::clippedMenusDoNotOpenFromThePointer`。
- **出处**：`3f45c3d`。

### 从另一个菜单打开菜单栏的菜单时，按真正的打开者定位

- **现象**：在溢出菜单里打开被裁掉的菜单，它出现在菜单栏原来的位置，挡住溢出菜单。
- **原因**：`QMenu` 的父控件是菜单栏，Qt 按菜单栏项的位置放置它。
- **做法**：在 `showEvent()` 里通过 `menuAction()->associatedObjects()` 找到当前可见、且 `activeAction()` 正是这个菜单的那个 `QMenu`，按子菜单的方式放在它旁边。键盘导航到被裁掉的项时，在菜单栏可见部分的末端打开。
- **验证**：`ToolbarPopupTest::clippedMenusOpenBesideTheOverflowMenu`。
- **出处**：`3f45c3d`。

## 滚轮

### Qt 不会把合成的滚轮事件传给父控件

- **现象**：数值控件（组合框、数字框、日期时间框、滑块）不再响应滚轮后，鼠标停在它们上面时页面也滚不动了。
- **原因**：被忽略的 `QWheelEvent` 只有在是系统产生的事件时 Qt 才会向父控件传播；自己构造并 `sendEvent` 的事件不会传播。
- **做法**：在应用级事件过滤器里拦截落在数值控件上的滚轮事件，自己沿父链逐级发送，直到有控件接受；只在同一个窗口内查找。滚动条不算数值控件。
- **验证**：`PaneSettingsTest::wheelOverValueControlsScrollsThePageWithoutEditing`、`wheelNeverEditsAnyValueControl`。测试里的滚轮事件要通过 `QWindow` 发送，并标成系统事件，否则测不到真实路径。
- **出处**：`1b02f6b`。

### `Qt::WheelFocus` 会让滚轮把焦点交给控件

- **现象**：在时间框上滚动时，时间框被激活并显示光标，虽然页面仍在滚动。
- **原因**：这些控件默认的焦点策略是 `Qt::WheelFocus`，Qt 在投递滚轮事件前就把焦点给了它。
- **做法**：在 `QEvent::Polish` 时把 `WheelFocus` 改成 `StrongFocus`，这样对之后新建的控件也生效。
- **出处**：`fb5c1ab`。

## 工具提示

### `QTipLabel` 会被复用，每次显示都会重新计算尺寸

- **现象**：鼠标从一个按钮快速移到另一个按钮，提示框整体变小，主题样式也丢了。
- **原因**：提示框还在显示时，Qt 复用同一个 `QTipLabel` 并按单行重新设定尺寸；控件级样式表不会作用到复用的提示框上。
- **做法**：提示框样式放进应用级样式表中一段带标记的区块，只在内容变化时重新设置，避免整个应用重新 polish；换行在 `QEvent::Resize` 里处理，并用属性记住自己设定的尺寸，以区分 Qt 的调整和自己的调整。
- **验证**：`CaptionStripTest::reusedToolTipsKeepTheThemedSize`。
- **出处**：`bb69b0e`。

### 长提示按形状换行，而不是按固定宽度

- **现象**：固定 400 像素换行后，很长的文字变成一条又窄又高的竖条。
- **做法**：400 像素以内保持一行；更长的取最少的行数，使每行宽度不超过 400 像素，或者不超过高度的 8 倍。富文本和自带换行的文字不处理。用 `horizontalAdvance` 计算文字宽度，因为 Qt 可能已经替它换过行。
- **验证**：`CaptionStripTest::longToolTipsWrapAtAReadableWidth`。
- **出处**：`7991ec3`。
