# LILYGO UI Store 架构说明

本文说明 Store 的模块边界、异步生命周期和包管理流程。工程、元数据和构建入口见
[项目总览](00-overview.md)，页面、布局和字体要求见 [UI 开发规范](01-user-interface.md)。

## 模块边界

| 目录或模块 | 职责 | 依赖约束 |
| --- | --- | --- |
| `src/domain/` | 跨页面共享的商店状态、筛选、版本和操作结果 | 不依赖 LVGL |
| `src/registry/` | HTTPS 传输、JSON、snapshot 和 SHA-256 校验 | 不依赖 UI |
| `src/system/` | 子进程与 Debian 包查询、安装、卸载 | 不依赖 UI |
| `src/service/` | 将 Registry 和系统结果映射为 `StoreModel` 数据 | 不创建 widget |
| `src/pages/<page>/` | 页面 ViewModel 和 LVGL View | View 只转发命令和渲染状态 |
| `src/store_view_model.cpp/.hpp` | 跨页面展示状态、用户命令和修订号 Subject | 持有共享 `StoreModel`，不拥有页面 widget |
| `src/components/` | 共享控件、语义颜色和字体入口 | 在调用者传入的 parent 下创建对象 |
| `src/app.cpp`、`src/app_router.*` | 生命周期、异步结果投递和全局导航 | 页面不持有全局路由 |

`lilygo_ui_store_core` 编译模型、路由、Registry、服务和系统代码，不链接 LVGL。
`lilygo_ui_store_ui` 编译应用装配、ViewModel、页面和组件，依赖 core 与
`LilyGoUI::Runtime`。`store-package-install` 独立编译提权安装入口、APT 安装逻辑、
进程执行和 SHA-256 校验，不依赖 UI 或图片库。

Model 保存业务状态和领域规则，ViewModel 将 Model 转换为展示状态并提供命令，View
负责事件转发和状态绑定。主要数据流为：

```text
Registry / APT / dpkg -> service -> StoreModel -> ViewModel Subject -> View
用户事件 -> ViewModel 命令 -> 后台操作 -> StoreModel 更新 -> Subject -> View 重绘
```

## 异步任务与生命周期

网络、图片处理和包操作在线程中执行。工作线程只向 mailbox 投递普通 C++ 数据，
`app.cpp` 的 LVGL timer 在 UI 线程读取结果并更新页面；工作线程不得调用 LVGL。
AppKit 完成 LVGL 初始化后调用 `app_open()`，由后者创建 ViewModel、Subject 和 View。
共享 `StoreViewModel` 使用 `IntSubject` RAII 封装发布整数修订号，各页面 ViewModel
转发同一 Subject，View 使用绑定到自身顶层对象的 observer 接收变化。

页面切换与 Subject 引发的页面刷新使用可取消、去重的 LVGL 异步回调，避免在事件或
observer 回调中删除事件源。`app_close()` 的关闭顺序为：

1. 标记关闭状态，停止 mailbox 接收新结果，取消待处理的页面切换；
2. 删除接收工作线程结果的 timer；
3. 取消各 View 的延迟刷新并销毁 View；
4. 删除 surface，清空缓存的 LVGL 对象指针；
5. 销毁页面 ViewModel 和共享 `StoreViewModel`，最后释放 Subject 并重置会话状态。

重复关闭和重复销毁必须无副作用，工作线程不得访问已销毁的页面或 ViewModel。

## 权限与缓存

权限统一在 `lpm.toml` 声明，运行时用途如下：

| 权限 | 范围 | 用途 |
| --- | --- | --- |
| `network` | 网络访问 | 读取 Registry，下载图标、截图和 Debian 包，从已有 APT 源获取依赖 |
| `filesystem` | `read-write`, `app-data` | 在应用缓存目录保存 Registry 快照文档、规范化后的图标和截图 |

Registry 根文档缓存位于 `$XDG_CACHE_HOME/<package>/registry/<url-hash>/root.json`，
同一目录的 `documents/` 保存该根文档引用的索引与应用详情；图片缓存位于
`$XDG_CACHE_HOME/<package>/images`。未设置 `$XDG_CACHE_HOME` 时使用 `$HOME/.cache`
下的对应路径；包名来自生成身份宏。快照按 Registry URL 隔离，资源文档先原子写入，
`root.json` 最后替换，因此缓存根文档不会指向未完整提交的资源。

## 安装与卸载

Debian 包先写入权限受限的临时文件，校验声明大小和 SHA-256 后才通过 `pkexec` 调用
随 Store 安装的 `lilygo-ui-store-package-install`。提权 helper 将包复制到独立目录后
重新核对 SHA-256、包名、版本和架构，再刷新 APT 索引并安装本地 `.deb`。
依赖交给 APT 从已有系统源解析；Registry 仅提供用户选中的应用，不充当 APT 源。
APT 以非交互模式运行、保留已有配置，并禁止移除包；失败不会调用裸 `dpkg` 补救。
安装和卸载的系统变更由 PolicyKit 明确授权，不作为普通应用数据写入。

普通应用安装成功后通过 `dpkg-query` 回读目标版本。卸载通过 `pkexec dpkg --remove`
完成，再查询包状态确认结果；Store 禁止卸载自身。

## Launcher 更新

Launcher 是受保护的系统组件，Store 不允许首次安装或卸载。更新时，Store 在完成相同
的下载、大小和 SHA-256 校验后，Store 的提权 helper 先通过 `apt-get satisfy` 安装新版
`Depends` 和 `Pre-Depends`、检查 `Conflicts` 和 `Breaks`，约束 Launcher 保持当前已安装
版本，再调用 Launcher 软件包提供的 update helper。后者同步复制文件并启动独立 oneshot
服务后返回；Store 随即通过 AppKit 退出回调结束前台进程，使后续 `dpkg` 和 Launcher
重启不受原 service cgroup 停止影响。

## APT 集成测试

常规构建、测试和打包要求见 [项目总览](00-overview.md#验收要求)。

`store-apt` 使用模拟进程检查校验、命令参数与失败路径。真实 APT 集成测试放在
`tests/test_apt_integration.cpp`，通过 `store-apt-integration-tests` 目标显式构建，不加入
默认 CTest。该测试会临时替换 APT 源并安装测试包，因此仅允许在可丢弃的 Linux 容器中
以 root 执行，同时要求 `LILYGO_UI_STORE_APT_TEST_CONTAINER=1`。测试使用本地文件源，
可在禁用网络的容器中运行，覆盖版本约束、传递依赖、升级、缺失依赖、冲突拒绝和
Launcher 依赖准备与交接。

在容器中完成常规 CMake 配置和构建后运行：

```sh
cmake --build --preset host-simulator --target store-apt-integration-tests
LILYGO_UI_STORE_APT_TEST_CONTAINER=1 \
  ./build/host-simulator/store-apt-integration-tests \
  ./build/host-simulator/lilygo-ui-store-package-install
```
