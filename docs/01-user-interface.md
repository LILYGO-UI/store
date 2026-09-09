# LILYGO UI Store UI 开发规范

本文统一维护 Store 的 UI 开发规范。新增或修改页面、组件、布局、样式、文案或字体时
必须遵守本文和 [AGENTS.md](../AGENTS.md)。工程与构建入口见
[项目总览](00-overview.md)，模块依赖和异步协调见 [架构说明](02-architecture.md)。

本文中“必须”和“不得”是强制要求，“应”是默认实现方式。开始编辑前，
必须先检查当前页面、`src/components/` 中的现有能力和相关测试，不得假设
仓库中存在未实现的基类、主题或响应式辅助 API。

## 架构边界

现有页面位于 `src/pages/catalog/`、`src/pages/detail/` 和 `src/pages/installed/`。
各页面的 ViewModel 通过共享 `StoreViewModel` 读取展示状态并提交命令，View 负责
LVGL 对象创建、布局、事件转发和绑定，不得直接修改 Model 或执行网络、包管理操作。

页面私有状态按需与 ViewModel/View 放在同一目录；跨页面业务规则放在 `src/domain/`，
纯 Model 不得依赖 LVGL。全局导航由 `src/app_router.cpp/.hpp` 和 `src/app.cpp` 协调。
通用控件和绑定使用 `src/components/` 中的现有能力，明确所有权和销毁顺序。

## 视觉规范

颜色集中定义在 [`src/components/components.hpp`](../src/components/components.hpp)。
页面必须复用现有语义颜色，不得重复定义相同用途的颜色；除明确的透明布局容器外，
以下颜色均为 `100%` 不透明，页面不得使用渐变。

| 语义角色 | 色值 | 透明度 | 用途 |
| --- | --- | --- | --- |
| Page | `#F2F2F7` | 100% | 页面背景 |
| Surface | `#FFFFFF` | 100% | 头部、卡片和导航表面 |
| Title | `#1A1A1A` | 100% | 页面和分区标题 |
| Text | `#000000` | 100% | 正文和主要数值 |
| Secondary | `#6E6E73` | 100% | 辅助说明和元数据 |
| Separator | `#D8DDE3` | 100% | 边框和分隔线 |
| Action | `#20262D` | 100% | 安装、更新等主要按钮背景 |
| Accent / Accent Soft | `#0878D1` / `#E5F2FC` | 100% | 选中状态、来源验证、链接式操作及其弱背景 |
| Success / Success Soft | `#218739` / `#E4F4E8` | 100% | 成功和已安装状态 |
| Warning / Warning Soft | `#E97916` / `#FFEEDF` | 100% | 更新和提醒状态 |
| Danger / Danger Soft | `#C9342F` / `#FFE8E7` | 100% | 错误、警告和破坏性操作 |
| Danger Border | `#F0B7B4` | 100% | 破坏性次要按钮边框 |
| Control | `#E3E4E8` | 100% | 非主要的中性控件表面 |
| Disabled | `#EEEEEF` / `#8E8E93` | 100% | 禁用表面及其文本 |

新增颜色时必须先定义语义角色、使用场景、色值和透明度；共享颜色集中到
`src/components/`。应用图标的回退配色可以作为组件内部的离散资源色保留。

## 字体与文本

- 显示文本的 UI 源文件必须包含 `<cm0/typography.h>`。除下述应用专用字体场景外，
  LVGL 文本对象必须使用 `lilygo_ui_font_get(size)` 返回的 AppKit 字体。
- 必须在 `lv_init()` 完成后获取字体。本项目统一使用 `14`、`22`、`28`、`36` 和
  `48` 像素字号，与 `AGENTS.md` 和 UI 契约测试保持一致。
- AppKit 在首次请求某个字号时创建并缓存对应的完整字体链，后续相同字号复用该
  缓存。不得根据视口尺寸连续计算字号，也不得为相近文本层级引入大量不同字号；
  每个不同字号都会占用额外字体缓存，直至字体系统反初始化。
- 设置文本样式前必须确认字体指针非空。返回的字体对象及其 fallback 链由 AppKit
  共享和管理，应用不得修改或释放。
- 相同信息层级在同一应用中应使用一致的字号。缩小字号不得作为修复溢出的
  首选方案；应先精简文案、调整布局，或按产品要求换行和截断。
- 不得在公共 UI 中引用 `lv_font_montserrat_*`，也不得复制、改名或随应用重新打包
  AppKit 字体。

AppKit 在运行时通过 LVGL FreeType 加载共享字体文件。`lilygo_ui_font_get()` 返回
以 Inter 为主字体、Source Han Sans CN 为中文 fallback、Font Awesome 为图标字形
fallback 的字体链。中西文混排必须使用同一个字体链，不得为了切换字体拆分标签，
也不得依赖操作系统字体发现或默认字体回退。

### 应用专用字体

仅当 AppKit 运行时字体确实缺少应用所需字形时，才可在 `assets/fonts/` 提供
应用专用的 TTF、OTF 或 WOFF 字体。不得复制或重新打包 AppKit 已提供的 Inter、
Source Han Sans CN 或 Font Awesome 字体。应用专用字体必须满足：

- 通过 `lv_freetype_font_create()` 按当前 UI 字号在运行时加载；
- 将应用专用字体作为引用它的 LVGL 对象的主字体，并将其 `fallback` 设为同字号的
  `lilygo_ui_font_get(size)` 结果；
- 不得修改 `lilygo_ui_font_get()` 返回的共享字体对象或它的 fallback 链；
- 字体对象的生命周期必须不短于引用它的 LVGL 对象；移除所有引用后，必须通过
  `lv_freetype_font_delete()` 释放；
- 字体文件必须通过应用的安装和打包规则部署到稳定的运行时路径；
- 可通过可重复的子集化流程仅保留所需字形以减小安装体积，但不得手工修改生成的
  字体文件。

AppKit 的 Inter、Source Han Sans CN 和 Font Awesome 字体按 SIL Open Font License 1.1
分发。应用不得将它们描述为“无版权字体”或单独销售。包含字体的分发物必须
保留 `NOTICE.md` 和 `OFL-1.1.txt`；本项目通过 `lilygo_ui_install_font_licenses()`
安装共享字体的许可文件。新增应用专用字体时，必须同时记录字体来源，并补充该字体的
许可文件和打包规则。

## 响应式布局

`src/app.cpp` 根据 surface 的实际内容宽度切换导航：小于 `720px` 时使用底部导航，
达到 `720px` 时使用侧边导航。页面主体使用百分比尺寸、flex grow、内容尺寸和纵向
滚动分配空间，内容宽度最多 `720px`。尺寸事件只重新应用布局，保留当前页面对象和状态。
布局还必须避让 AppKit 的状态栏和 Home Indicator 安全区。

- 页面主体必须使用 LVGL Flex 或 Grid，并结合百分比尺寸、`LV_SIZE_CONTENT`、
  最小/最大尺寸、对齐、间距和滚动容器分配空间。
- 不得用大量绝对坐标组织页面主体，不得为横屏、竖屏或不同尺寸复制整套
  页面。
- 固定像素值只能用于有明确约束的尺寸，例如图标、边框、间距、控件下限和设计
  token。
- 连续尺寸变化应由 layout 自动处理。只有导航方向、栏数、内容优先级或交互
  方式发生结构变化时才设置断点。
- 断点必须依据父容器的实际可用宽高，不得依据 CPU 架构、设备型号或应用展示
  模式。可复用组件不得假设自己占满 display。
- 尺寸变化的布局更新必须可重复执行，不得把重建整个页面作为常规适配方式，
  并必须保留业务状态、焦点、滚动位置和用户输入。

响应式结果必须保证：

- 文本按产品要求换行或截断，不与相邻内容重叠；
- 主要操作始终可见、可达，主要按钮高度至少 `44px`；
- 内容增多、文案变长或字体回退时，容器可扩展、重排或滚动；
- 横竖屏和尺寸切换后业务状态和用户输入不丢失。

## LVGL 对象和生命周期

- View 必须在调用者传入的 `parent` 下创建顶层对象，不得假设 parent 是当前
  screen 或整个 display。
- 页面 View 必须保存自己的顶层 LVGL 对象，并提供可重复调用的销毁流程。
- LVGL 父对象负责删除子对象。销毁父对象后必须清空所有缓存的子对象指针，
  不得重复删除。
- 事件回调使用 `user_data` 时，其指向对象的生命周期必须覆盖事件注册期间。
- 优先使用 LVGL 对象绑定的 observer。全局 observer、输入监听器、timer 和其他非对象
  绑定资源必须在销毁时显式注销。
- ViewModel 必须比绑定其 Subject 的 View 存活更久。Subject 应使用现有 RAII 封装，
  不得复制或移动底层 `lv_subject_t`。
- 延迟页面切换时，不得在当前 LVGL observer 或事件回调中同步删除触发回调的
  页面。
- 需要被渲染测试定位的关键对象必须设置稳定、有语义的 `lv_obj_set_name_static()`
  名称。

Store 的页面 View 通过绑定到自身顶层对象的 observer 接收共享修订号，并使用去重的
`lv_async_call()` 延迟刷新子对象。销毁时必须取消待处理的刷新回调；完整关闭顺序见
[架构说明](02-architecture.md#异步任务与生命周期)。重复关闭和重复 `destroy()` 必须无副作用。

## 新增页面

新增页面前必须先检查 `src/pages/catalog/`、`src/pages/detail/`、`src/pages/installed/`
和相关测试，沿用现有 View、ViewModel、共享 Store 状态及回调组织方式。新增页面通常需要：

1. 按需增加页面私有 Model；已有共享业务状态通过 `StoreViewModel` 和 `StoreModel` 复用；
2. `src/pages/<page>/<page>_view_model.cpp/.hpp`：命令、展示状态和 Subject；
3. `src/pages/<page>/<page>_view.cpp/.hpp`：LVGL 对象、绑定、回调和响应式布局；
4. 在 `src/app.cpp` 中装配和销毁页面，并在 `src/app_router.cpp/.hpp` 中接入全局导航；
5. Model、ViewModel 和用户可见行为的测试；
6. 修改后的预览图和用户文档。

页面私有 Model 与 ViewModel/View 放在同一目录，但仍必须编译到不同依赖层。纯 Model
代码不得因为目录内聚而链接 LVGL。

## 新增通用组件

只有组件被多个页面使用，或它明确承担通用绑定、样式或资源生命周期时，才放入
`src/components/`。否则应保持在所属页面目录。

通用组件必须：

- 在调用者传入的 parent 下构建；
- 以父容器的可用尺寸进行响应式布局；
- 复用语义颜色和 AppKit 字体；
- 声明所有权、回调 `user_data`、observer、timer 和销毁责任；
- 在最长文案、字体回退和支持视口下不发生重叠、裁切或不可达操作。

## 验收

每次 UI 修改必须完成 [项目总览](00-overview.md#验收要求) 中的主机配置、构建和测试。
`store-view-model` 验证命令和 Subject 发布；`store-ui-contract` 验证字体链、长文案、
对象布局和生命周期；`tests/render_ui.cpp` 在以下视口执行无头渲染和交互测试：

| 测试 | 视口 | 主快照 |
| --- | --- | --- |
| `store-render-compact-portrait` | `320x568` | `store-compact-portrait.ppm` |
| `store-render-portrait` | `568x1232` | `store-portrait.ppm` |
| `store-render-landscape` | `1232x568` | `store-landscape.ppm` |
| `store-render-large-landscape` | `1024x768` | `store-large-landscape.ppm` |

快照位于 `build/host-simulator/`；同目录还包含各交互阶段的渲染产物。测试覆盖导航、
图标和截图加载、主要操作、关闭流程，以及详情页尺寸往返切换时对象、滚动位置和焦点的保持。

测试通过不能替代视觉检查。AI agent 在修改 UI 时还必须检查横竖屏渲染产物，
并确认：

- 布局不依赖单一固定分辨率；
- 文本无缺字、错误回退、重叠或裁切；
- 主要操作可见、可达，按钮文案和命中区域正常；
- 尺寸或方向变化后布局正确重排，业务状态、焦点、滚动位置和用户输入保留；
- 页面背景、容器、文本、边框和主操作使用正确的语义颜色；
- 关闭页面后无野指针、重复删除、未注销 observer、timer 或输入监听器。

修改 SDK、工具链、安装或打包相关 UI 资源时，还必须完成 `cm0-cross` 构建和
Debian 打包验证。
