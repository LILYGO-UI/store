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

## Runtime 更新

Registry v1 索引可附带 `runtimes` 数组，条目以 Debian 包名标识，引用同一 snapshot 的
`runtimes/<package>.json`，并包含详情 SHA-256。客户端同时校验摘要、路径、包名、标题、
摘要文案和已发布版本；runtime 与应用重复使用包名时拒绝整个快照。缺少 `runtimes`
字段的旧快照继续有效。缓存保存 runtime 详情，并沿用完整快照的原子发布规则。

解析结果在共享目录模型中标记 `runtime`，内部 ID 为 `runtime:<package>`，不伪造
Desktop application ID。`StoreApp.runtime_releases` 保留状态为 `published` 且架构匹配
当前设备或 `all` 的版本，按 Debian 版本降序排列；同一 Debian 版本优先 native 架构并
去重。yanked/revoked 和其他架构的版本不可选择；runtime 没有应用专用的
`min_appkit_version` 要求。单个 `release` 仍代表最新兼容版本，供列表快速安装或更新。

Store 同时通过 `dpkg-query` 枚举已安装的 `lilygo-*` 包和 `meshtasticd`，再查询包文件
清单，排除拥有 `/usr/share/launcher/apps/*.json` 或 `/usr/share/applications/*.desktop`
的应用及 Launcher 自身。本地补充仅包含完全安装的包，忽略仅残留配置及未完成配置的
包；未安装条目由 Registry 目录提供。文件清单查询失败或截断时也不将候选包误判为 runtime。一个枚举匹配式无结果时，保留另一个匹配式
返回的有效包。新 LILYGO 系统组件（包括板级 overlay）不需要新增包名白名单。
Registry 中没有的已安装条目以 `catalogued=false`、`available=false` 合并到结果中；
已有目录条目按包名去重，保留其发布元数据。缺少缓存或网络失败时也返回这些本机条目，
UI 在保留 Registry 错误状态的同时显示它们；版本未知不会被解释为无需更新。
包查询和安装 helper 按 Debian 包名语法校验，允许没有 `lilygo-ui-` 前缀的 runtime，
拒绝命令选项、路径、通配符和 shell 字符。卸载仍受应用名及系统组件保护限制。

Runtime 路由复用 `CatalogView`/`CatalogViewModel` 的列表、绑定和异步销毁逻辑，显示
全部已发布 runtime 目录条目并合并本地发现结果，不再以 `installed` 作为可见条件。
未安装且没有兼容版本的 runtime 仍保留为 `available=false`，供查看不兼容状态；普通
应用继续遵循原有目录筛选规则。普通目录及 Installed 的列表和计数排除 runtime。

详情页提供兼容版本选择，可首次安装最新或历史版本，并在确认目标版本后回退。
安装命令必须引用当前已验证目录内的具体发布版本，不能从任意 URL 构造。回退通过
显式 `allow_downgrade` 参数传递给已有安装 helper；普通安装入口不自动降级。下载摘要、
Debian 包身份、架构和 APT 依赖约束仍需校验。Runtime 不提供卸载操作。详情页不展示
应用预览或权限声明，从详情返回时恢复 Runtime 页及滚动位置。

Model 接收 helper 回读的实际安装版本，允许 runtime 首次安装和回退结果更新状态，
并根据实际版本是否低于最新兼容版本重算 `update_available`。因此安装历史版本或
回退后仍可提示有新版；普通应用的原有状态约束保留。成功后刷新包版本及应用兼容性。

可选版本来自已发布的 runtime 目录及其真实 release 资产。发布器保留历史 release
记录，客户端仅允许安装或回退到状态为 `published` 且兼容的版本。目录没有更旧的
兼容发布版本时不提供回退目标，也不能根据当前安装包伪造历史版本、摘要或下载地址。
板级 overlay 与其他 runtime 使用同一目录、版本选择和本地发现规则。

## 管理员认证

Store 在 Linux 包操作的工作线程中注册进程专属的 `PolkitAgentListener`，再启动
`pkexec`。`PolkitAgentSession` 负责系统 PAM 会话；独立 GLib 线程把提示和完成事件
投递到 Store mailbox，只有 LVGL 线程创建和更新认证界面。密码只传给当前请求，
提交或取消立即清空输入；请求 ID 防止旧响应进入新的认证会话。

认证页支持触屏键盘和硬件键盘，显示系统提供的管理员身份、提示和失败信息。原页面
在认证期间隐藏并保留状态；完成后恢复。关闭 Store 时先停止 mailbox 接收并销毁输入
界面，再取消会话、注销代理和回收 GLib 线程。注册失败会作为包操作错误返回。

Launcher 必须允许正常的系统提权流程。继承了 `NoNewPrivs=1` 的旧进程无法自行清除
该限制，需要升级并重启 Launcher 后重新打开 Store；此状态会给出明确错误。
Store 不设置免密规则，不修改 `pkexec` 权限，也不保存管理员密码。

Linux 构建需要 `polkit-agent-1` 和 `gio-2.0` 的 pkg-config 开发元数据。macOS 模拟器
使用无系统认证的桩实现，认证 UI 由四种视口的渲染测试独立覆盖。可显式构建
`store-auth-integration-tests`，在目标 Linux 普通用户的交互终端运行。该测试通过真实
polkit 会话执行 `/usr/bin/true`，不修改软件包；密码从 `/dev/tty` 无回显读取。

## Launcher 更新

Launcher 是受保护的系统组件，Store 不允许首次安装或卸载。更新时，Store 在完成相同
的下载、大小和 SHA-256 校验后，Store 的提权 helper 先通过 `apt-get satisfy` 安装新版
`Depends` 和 `Pre-Depends`，并在禁止删除软件包的前提下检查 `Conflicts` 和 `Breaks`。
目标包自己提供的迁移别名会从这个合成检查中排除，当前 Launcher 版本不会作为 APT 约束，
再调用 Launcher 软件包提供的 update helper。后者同步复制文件并启动独立 oneshot
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
