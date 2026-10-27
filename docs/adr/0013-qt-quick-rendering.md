# 0013：用 Qt Quick 迁移界面绘制，保持现有外观和交互

状态：采用，分步实施；当前预览仍使用 Widgets。

## 背景

窗口系统的模型和纯布局已经独立，但标签、卡片、菜单和宿主内容仍由 QWidget 绘制。GPU 合成 QWidget 的 backing store 不会把这些绘制变成保留的 GPU 场景，也不会自动消除布局、文字整形和固定计时器的成本。

用户要求保持目前的外观和交互完全不变，使用 Qt Quick 深度优化整个界面，以屏幕高刷新率为目标，争取达到 GPUI 同等级的性能。性能必须用实际场景测量，不能由框架名称推定。

## 决定

新增 Qt Quick 呈现层，最终用 `QQuickWindow` 承载窗口，使用公共 C++ 场景图接口绘制。布局模型、命令、布局计算、接合轮廓和交互状态共用，不建立另一套业务模型或停靠规则。现有视觉设计继续有效，本决定不改变尺寸、阴影、字体、颜色、动画曲线、命中或默认设置。

按 [阶段 10](../roadmap/implementation/phase-10-qt-quick.md) 分步替换呈现。迁移未通过完整的行为对照前，不替换默认入口，也不以缺少功能的 Quick 入口交付正式预览。继续使用现有 Muxora 入口，不另建演示产品。

动画从窗口帧时钟取样，空闲时不持续请求帧。GUI 线程维护交互状态并交付有界的不可变绘制数据；场景图线程不访问 QWidget，不执行宿主回调。GPU 资源按所属窗口和场景图生命周期释放。

只使用符合现有许可要求的 Qt Quick 模块，不引入 GPL-only 的 Qt Canvas Painter。Qt Quick 的许可见 [官方说明](https://doc.qt.io/qt-6/qtquick-index.html)。具体要求在 [Quick 呈现设计](../design/quick-rendering-design.md)。

## 后果

迁移包括宿主接口、原生窗框、输入法和可访问性，不能仅修改 `paintEvent()`。已有 QWidget 宿主接入在默认路径继续有效；Quick 内容接口必须表达真实的 `QQuickItem` 和 `QWindow` 类型，不使用原生子窗口覆盖 Quick 浮层。

短期保留两种呈现适配器以便定向比较，共用规则，完成迁移后清除被替换的实现。过渡适配不是旧布局或旧配置兼容层。

## 替代方案

- QWidget 的 RHI 合成只改变最终合成，不能完成本次要求。
- `QQuickWidget` 增加离屏绘制并禁用线程渲染循环，不作为最终窗口载体。见 [官方性能说明](https://doc.qt.io/qt-6/qquickwidget.html#performance-considerations)。
- `QQuickPaintedItem` 或整窗截图上传仍包含逐帧 CPU 栅格化，不作为整体迁移方案。字形和有界静态资源的冷生成可以保留，但须单独计量。
- 直接使用 Quick Controls 默认样式会改变现有外观；已有行为和样式应显式接入。

## 日期

决定及记录：2026-10-27，依据用户本轮明确要求。
