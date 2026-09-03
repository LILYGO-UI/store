# UI Design

## Structure

页面使用 LVGL Flex 或 Grid，在调用者传入的 parent 下创建顶层对象。View 负责对象创建、
布局、事件转发和状态显示；ViewModel 提供页面命令和展示数据；业务规则保留在不依赖 LVGL
的 Model/Service 层。

页面主体使用百分比尺寸、flex grow、内容尺寸和纵向滚动分配空间。`app.cpp` 读取 surface
的实际内容宽度：小于 `720px` 时使用底部导航，达到 `720px` 时切换为侧边导航。尺寸事件
只重新应用布局，不重建页面或丢失业务状态。必须验证 `568x1232` 和 `1232x568`，当前测试
还覆盖 `320x568` 与 `1024x768`。

## Semantic Colors

颜色定义集中在 `src/components/components.hpp`。除明确的透明布局容器外，以下颜色均为
`100%` 不透明；页面不得使用渐变。

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

新增共享颜色前必须先定义语义角色、使用场景、色值和透明度，并集中到 components；
应用图标的回退配色可以作为组件内部的离散资源色保留。

## Typography And Content

所有可见文本通过 `<cm0/typography.h>` 的 `lilygo_ui_font_get()` 获取 AppKit 复合字体。
本应用使用 `14`、`22`、`28`、`36` 和 `48` 像素层级；不得引用
`lv_font_montserrat_*`，也不得修改或释放 AppKit 返回的字体。文本必须按产品要求换行或
截断，优先调整文案、容器和滚动行为，而不是按视口连续缩放字号。

## Interaction And Verification

主要按钮高度至少 `44px`，关键对象设置稳定的 LVGL name 供渲染测试定位。主要操作在横竖
屏均须可见、可达；长标题、说明、权限原因和字体 fallback 不得与相邻内容重叠。页面销毁
时由父对象回收子对象，并显式清理 timer、observer 和其他非对象绑定资源。

共享 `StoreViewModel` 通过 RAII 管理的整数 Subject 发布数据修订号。页面 View 使用绑定到
自身顶层对象的 observer 接收修订，并通过去重的 `lv_async_call()` 延迟重建其子对象；页面
切换也必须延迟到当前事件回调返回之后。关闭顺序固定为：禁止新异步结果、取消延迟回调、
删除 timer、销毁 View、删除 surface，最后销毁 ViewModel 和 Subject。重复关闭和重复
`destroy()` 必须无副作用。

UI 修改必须运行主机测试并人工检查各视口渲染产物，确认布局、文本、命中区域、颜色、
导航和尺寸切换后的状态保持正确。
