# LILYGO UI Store 项目总览

LILYGO UI Store 是可独立配置、构建、测试、安装和打包的应用商店。应用从 LILYGO
Registry 读取目录，展示应用详情，并管理设备上的应用安装、更新和卸载。

本文中“必须”和“不得”表示项目约束，“按需”表示只在实际功能需要时创建。
仓库级开发约定见 [AGENTS.md](../AGENTS.md)，快速开始见
[中文 README](../README.zh-CN.md) 或 [English README](../README.md)。

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [项目总览](00-overview.md) | 工程约束、环境、目录、构建和测试入口 |
| [UI 开发规范](01-user-interface.md) | 页面组织、颜色、字体、响应式布局和视觉验收 |
| [架构说明](02-architecture.md) | 模块依赖、异步生命周期、缓存、包管理和 APT 集成测试 |

总览和 UI 规范由 `lilygo-ui-template/docs/` 引入，现按 Store 的实际结构维护。
文档统一放在 `docs/` 根目录，按 `NN-topic.md` 编号；新增内容应优先补充对应主题，
避免重复维护同一规则。

## 稳定约束

- 应用源码和测试使用 C++17 或更新标准；仅在 AppKit 集成边界保留 C ABI。
- UI 使用 LVGL，版本和补丁由 LilyGoUI SDK 统一提供，应用不自行引入 LVGL 副本。
- 桌面预览使用 SDL2；设备默认使用 DRM/KMS。
- 标准测试视口为竖屏 `568x1232` 和横屏 `1232x568`。布局必须依据容器的
  实际几何尺寸响应，不得对外暴露应用展示模式。
- 顶层构建系统是 CMake 3.21 或更新版本，稳定预设名为 `host-simulator` 和
  `cm0-cross`。
- 应用只能通过 `find_package(LilyGoUI CONFIG REQUIRED)` 消费平台 SDK。不得引用
  Launcher 源码、其他应用仓库或 AppKit 私有文件。
- `/usr/share/launcher/apps` 下的 Launcher manifest 只是运行时发现协议，不是
  构建依赖。
- 公开接口不得使用 `cm0-app-*`、`lilygo-cm0-*`、`cc.lilygo.cm0.*` 或
  `org.cm0.*` 等旧命名。
- UI 必须使用 LVGL Flex 或 Grid 组织响应式布局。文本使用
  `<cm0/typography.h>` 中的 `lilygo_ui_font_get()` 统一获取 AppKit 字体。

## 开发环境

开发机必须提供以下工具：

- Git，用于获取仓库和初始化 Git submodule。
- CMake 3.21 或更新版本，并提供 `ctest` 和 `cpack`。
- 支持 C++17 的 C/C++ 编译器和 `make`。
- `pkg-config`、SDL2 和 libpng 开发包，以及 LilyGoUI SDK 所需依赖。
- 与当前 `lpm.toml` schema 兼容的 LPM，并且 `lpm` 在 `PATH` 中可用。
- 设备构建需要网络以首次下载已锁定且校验过 SHA-256 的 AArch64 BSP。

克隆后必须先初始化 SDK submodule：

```sh
git submodule update --init --recursive
```

`third_party/cm0-appkit` 提供 presets 使用的 SDK 配置包，并不改变应用的依赖
边界。应用 CMake 不得通过 `add_subdirectory()` 编译 AppKit，应用源码也不得使用
指向该子模块的相对包含路径。

## 配置与元数据

`lpm.toml` 是唯一由开发者维护的项目配置和产品元数据源，包含：

- 应用标识、版本、标题、描述、许可证和作者；
- 图标、预览图、权限、兼容性和 Launcher 排序；
- 主机、交叉构建 preset 和包输出目录；
- 部署主机、用户和端口默认值。

CMake 在配置阶段通过 LPM 读取并校验 `lpm.toml`，然后在构建目录中生成 CMake
变量、应用身份头文件、Launcher manifest、desktop 文件和 AppStream 元数据。
`data/*.in` 只能保留通用模板，不得重复写入专属于应用的值。
生产代码需要包名或应用 ID 时必须使用生成的身份宏。

公开身份必须满足以下命名约定，实际字段统一在 `lpm.toml` 维护：

```text
包名和可执行文件  lilygo-ui-<component>
应用 ID            cc.lilygo.ui.<Component>
Desktop/AppStream     cc.lilygo.ui.<Component>
```

`publish.json` 是 LPM 发布流程中的临时派生数据，不是项目配置入口，不得手工编辑或
提交，也不得生成到仓库根目录。项目源码、CMake 和文档不得依赖该临时文件
存在。

## 工作流程

LPM 是开发者的首选入口，CMake presets 是可独立调用的底层稳定接口。
本文中的命令均在仓库根目录执行。

| 目标 | LPM 入口 | CMake 底层入口 | 输出 |
| --- | --- | --- | --- |
| 启动桌面模拟器 | `lpm start` | 主机配置、构建后运行 `./build/host-simulator/lilygo-ui-store` | `build/host-simulator/` |
| 运行主机测试 | `lpm test` | `ctest --preset host-simulator` | 测试结果和渲染快照 |
| 交叉构建和打包 | `lpm pack` | 设备配置、构建后运行 CPack，命令见下文 | `dist/*.deb` |
| 部署到设备 | `lpm deploy` | 无 | 设备上已安装的 Debian 包 |

不得绕过 presets 依赖 `build/` 内部目录结构，也不得在开发机上进行源码内
构建。

## 目录结构

```text
lilygo-ui-store/
├── assets/                         # 应用图标和发布预览图
├── cmake/                          # 元数据转换、身份头文件和交叉工具链
├── data/                           # Launcher、desktop 和 AppStream 模板
├── docs/                           # 项目总览、UI 规范和架构说明
├── src/
│   ├── main.cpp                    # AppKit 运行时入口
│   ├── app.cpp/.hpp                # 生命周期、页面装配和异步任务协调
│   ├── app_router.cpp/.hpp         # 全局导航状态
│   ├── store_view_model.cpp/.hpp   # 跨页面展示状态和命令
│   ├── pages/
│   │   ├── catalog/                # 应用目录 View 和 ViewModel
│   │   ├── detail/                 # 应用详情 View 和 ViewModel
│   │   └── installed/              # 已安装应用 View 和 ViewModel
│   ├── domain/                     # 共享模型和 Debian 版本规则
│   ├── registry/                   # 传输、协议、摘要校验和目录快照缓存
│   ├── system/                     # 进程、包管理和提权安装 helper
│   ├── service/                    # Registry 与系统结果的业务映射
│   └── components/                 # LVGL 控件、语义颜色和 Subject RAII
├── tests/                          # 核心、APT、ViewModel 和 UI 测试
├── third_party/cm0-appkit/         # SDK 配置包
├── AGENTS.md                       # 仓库级开发约定
├── CMakeLists.txt                  # 顶层构建、安装和 CPack 规则
├── CMakePresets.json               # 主机和设备构建入口
└── lpm.toml                        # 项目配置和产品元数据
```

`assets/images/`、`assets/audio/` 和 `assets/fonts/` 按需创建，无资源时不保留空目录。应用
不得复制或再打包 AppKit 提供的 Inter、Source Han Sans CN 或 Font Awesome 完整
字体。专用字体的适用条件和许可要求见 [UI 开发规范](01-user-interface.md)。
发布预览图不进入 Debian 包。

模块依赖、共享 Model、ViewModel 和异步数据流统一记录在
[架构说明](02-architecture.md)。页面和组件的开发要求见 [UI 开发规范](01-user-interface.md)。

## 生成物与提交边界

以下目录是本地状态或构建产物，不属于源码目录，不得提交：

```text
.cache/    # 已校验的 BSP 等下载缓存
.lpm/      # LPM 运行状态和报告
.venv/     # 本地 Python 环境
build/     # CMake 构建树和测试渲染产物
dist/      # Debian 包及打包临时产物
```

项目不预设 `packaging/` 目录。Debian 安装和打包规则应优先保持在顶层
`CMakeLists.txt` 和 `data/` 模板中；只有出现 CPack 无法表达的真实打包文件时，
才按需创建该目录。

## 验收要求

代码变更至少完成主机配置、构建和测试：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

默认 CTest 测试如下，名称与目标定义以 [`CMakeLists.txt`](../CMakeLists.txt) 为准：

| 测试 | 范围 |
| --- | --- |
| `store-core` | 模型、路由、Registry、缓存、服务和包管理等核心行为 |
| `store-apt` | 使用模拟进程验证安装 helper 的校验、命令和失败路径 |
| `store-view-model` | 展示状态、用户命令和 Subject 发布 |
| `store-ui-contract` | 字体链、长文案、对象布局和生命周期约束 |
| `store-render-*` | 四个视口的无头渲染和交互，详见 [UI 验收](01-user-interface.md#验收) |

真实 APT 集成测试需要单独构建，并且只能在可丢弃的 Linux 容器中运行，具体要求和
命令见 [架构说明](02-architecture.md#apt-集成测试)。

修改 UI 时必须检查竖屏和横屏渲染结果，确认文本无缺字、重叠和裁切，主要
操作始终可见且可达。修改安装、SDK、工具链、元数据或打包逻辑时，还必须完成
设备构建和 Debian 打包：

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

新应用不得声明 Debian `Provides`、`Conflicts` 或 `Replaces`；只有在迁移真实已发布
软件包时才能添加这些关系。

工具链辅助文件见 [cmake 说明](../cmake/README.md)，代码格式化和提交 hook 的使用方式
见 [中文 README](../README.zh-CN.md#代码格式化)。
