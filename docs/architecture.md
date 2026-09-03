# Architecture

LILYGO UI Store 是独立的顶层 CMake 应用。平台 SDK 只通过
`find_package(LilyGoUI CONFIG REQUIRED)` 引入；应用不编译 Launcher、其他应用或 AppKit
私有源码。

## Boundaries

| 目录或模块 | 职责 | 依赖约束 |
| --- | --- | --- |
| `src/domain/` | 跨页面共享的商店状态、筛选、版本和操作结果 | 不依赖 LVGL |
| `src/registry/` | HTTPS 传输、JSON、snapshot 和 SHA-256 校验 | 不依赖 UI |
| `src/system/` | 子进程与 Debian 包查询、安装、卸载 | 不依赖 UI |
| `src/service/` | 将 Registry 和系统结果映射为 `StoreModel` 数据 | 不创建 widget |
| `src/pages/<page>/` | 页面 ViewModel 和 LVGL View | View 只转发命令和渲染状态 |
| `src/components/` | 共享控件、语义颜色和字体入口 | 在调用者传入的 parent 下创建对象 |
| `src/app.cpp`、`src/app_router.*` | 生命周期、异步结果投递和全局导航 | 页面不持有全局路由 |

主要数据流为：

```text
Registry / dpkg -> service -> StoreModel -> ViewModel Subject -> View
用户事件 -> ViewModel 命令 -> 后台操作 -> StoreModel 更新 -> Subject -> View 重绘
```

网络、图片处理和包操作在线程中执行。工作线程只向 mailbox 投递普通 C++ 数据，
`app.cpp` 的 LVGL timer 在 UI 线程读取结果并更新页面；工作线程不得调用 LVGL。
ViewModel 和 Subject 仅在 `app_open()` 完成 LVGL 初始化后创建。页面切换与 Subject 引发的
页面刷新使用可取消的 LVGL 异步回调，避免在事件或 observer 回调中删除事件源；关闭时先
取消这些回调并销毁 View，再销毁 ViewModel 和 Subject。

## Identity And Metadata

`lpm.toml` 是应用身份、版本、说明、许可证、资产、权限、兼容性、Launcher 顺序、构建
preset 和部署默认值的唯一维护入口。配置阶段由 `cmake/LpmMetadata.cmake` 调用 LPM，
生成 CMake 变量和 `app_identity.h`；生产代码需要包名或应用 ID 时必须使用生成宏。
Launcher、desktop、AppStream 和 Debian 字段均由这些变量派生。

## Permissions And Package Operations

发布元数据声明以下权限：

| 权限 | 范围 | 用途 |
| --- | --- | --- |
| `network` | 网络访问 | 读取 Registry，并下载图标、截图和待安装的 Debian 包 |
| `filesystem` | `read-write`, `app-data` | 在应用缓存目录保存 Registry 根文档、规范化后的图标和截图 |

Registry 根文档缓存位于 `$XDG_CACHE_HOME/<package>/registry/<url-hash>/root.json`，
图片缓存位于 `$XDG_CACHE_HOME/<package>/images`，未设置时使用 `$HOME/.cache` 下的
对应路径；包名来自生成身份宏。根文档按 Registry URL 隔离并通过临时文件原子替换。
Debian 包先写入权限受限的临时文件，校验声明大小和 SHA-256 后才通过 `pkexec dpkg`
安装。安装和卸载的系统变更由 PolicyKit 明确授权，不作为普通应用数据写入。

## Verification

主机变更至少运行：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

涉及 SDK、元数据、安装或打包时，还需运行 `cm0-cross` 构建并通过 CPack 生成 Debian
包。`build/`、`dist/`、`.cache/`、`.lpm/` 和 `publish.json` 都是派生数据，不提交。
