# LILYGO UI Store

[English](README.md) | 简体中文

`lilygo-ui-store` 是面向 LILYGO UI 设备的应用商店。当前实现提供响应式设备端界面、
Registry v1 客户端和独立的商店状态模型，可浏览 Registry 应用、查看本机已安装应用，
并完成安装、更新和删除操作。

## 功能

- 浏览全部应用，并按 Official 或 Community 来源筛选
- 查看应用作者、版本、大小和说明
- 安装未安装应用，更新有新版本的应用
- 查看并删除本机已安装应用
- 在 Runtime 页面检查并更新已安装的共享运行库，查看当前版本、可用版本及更新结果
- 从 LILYGO Registry Pages 读取不可变应用目录 snapshot
- 安装前校验 Debian 包大小和 SHA-256
- 适配 AppKit 默认的 568×1232 竖屏、1232×568 横屏和 320×568 紧凑竖屏
- 自动避让 AppKit 管理的 96px 状态栏；竖屏底部导航预留 38px Home Indicator 安全区，
  横屏侧栏铺满状态栏以下的高度

Store 默认从以下只读 Pages 端点加载 Registry v1：

```text
https://lilygo-ui.github.io/packages/v1/root.json
```

客户端先读取 `root.json`，要求索引 URL 指向同一 `snapshot_id`，下载索引后验证根文档
声明的 SHA-256，再读取该 snapshot 中每个应用的详情。根文档、索引和详情的协议版本、
snapshot、应用身份与摘要字段必须一致；任一检查失败时不会采用部分目录。

Store 会按 Registry 根地址缓存已验证的 `root.json`、索引和应用详情文档。后续启动时，
缓存快照完全从本地文件加载，同时并行刷新线上快照，因此页面无需等待网络即可显示本地
目录；线上结果成功后再更新页面并原子发布新缓存。如果线上结果先完成，较旧的缓存结果
不会覆盖页面。缓存不存在或损坏时直接忽略，不影响线上加载。

目录按设备的 Debian 架构和 AppKit 版本选择最新的已发布兼容包：CM0 使用 `arm64`，
K230 使用 `riscv64`，`all` 表示架构无关的通用包。版本按 Debian 规则比较，不直接采用
可能属于其他架构的索引全局最新版本。没有兼容包且未安装的应用会被隐藏；已经安装的
应用仍可管理和卸载，但不能安装或更新。下载前会再次检查包架构，提权安装程序还会
核对实际 DEB 中的架构。

安装和更新会从详情中的中央 GitHub Release 下载 `.deb`；官方内容寻址资产优先通过
GitHub API 解析到资产下载端点，并在 API 不可用时回退 Registry 中的原始 URL。下载完成后
严格核对文件大小和 SHA-256，再通过 `pkexec` 调用随包安装的
`lilygo-ui-store-package-install` helper。helper 复制并重新校验包的 SHA-256 与 Debian
身份，刷新 APT 索引，通过 `apt-get install` 安装本地包并自动补齐所需依赖，再由
Store 使用 `dpkg-query` 回读目标版本。卸载通过 `pkexec dpkg --remove` 完成，商店禁止卸载自身。目录刷新和包操作
均在工作线程执行，LVGL 线程只接收结果和更新页面。

需要管理员权限时，Store 通过系统 polkit 显示密码输入页，支持硬件键盘、触屏键盘
及取消操作，密码不会保存。若旧 Launcher 启动的应用继承了 `NoNewPrivs` 限制，
请先升级并重启 Launcher，再重新打开 Store；应用本身无法清除此限制。

依赖由 APT 2.2 或更新版本从设备已配置的 Debian/Raspberry Pi 软件源解析，必须在包的 `Depends` 或
`Pre-Depends` 中声明，且软件源提供所需版本，或设备已经安装兼容版本。
GitHub `packages` 应用目录不是 APT 源；Store 不会自动添加软件源，也不会将目录里的
其他应用作为依赖下载。安装无需终端交互，保留已有配置文件，并拒绝需要移除软件包的
事务。索引刷新或依赖解析失败会显示错误，不会回退到 `dpkg --install`。

代码按边界组织：`registry` 处理传输、JSON 和 snapshot 校验，`system` 封装进程及 Debian
包管理，`service` 将协议数据映射为 Store 模型，`pages/<feature>` 包含 View 与 ViewModel。
核心代码不依赖 Launcher 或其他应用仓库。

`lilygo-ui-launcher` 被视为受保护的系统组件：Store 不允许卸载它，也不提供首次安装。
当已安装的 Launcher 有更新时，Store 仍按 Registry 元数据下载并校验软件包，安装
helper 先通过 `apt-get satisfy` 补齐新版的必需依赖，并在禁止删除软件包的前提下检查
`Conflicts`/`Breaks`；目标包自己提供的迁移别名会从这个合成检查中排除，当前 Launcher
版本也不会作为 APT 约束（因为镜像或本地部署的版本可能不在已配置的软件源中），再交给
`/usr/lib/lilygo-ui-launcher/lilygo-ui-launcher-update`。helper 将软件包复制到 root
管理的目录并启动独立更新服务；交接成功后 Store 退出，使升级脱离 Launcher service
cgroup 完成，并由更新服务重新启动 Launcher。尚未包含该 helper 的旧版本需要由系统
镜像或管理员先完成一次基线升级。

进一步的工程约定见 [项目总览](docs/00-overview.md)、
[UI 开发规范](docs/01-user-interface.md) 和 [架构说明](docs/02-architecture.md)。

## Runtime 包管理

底部导航（横屏为侧栏）中的 **Runtime** 显示 Registry 已发布目录中的全部 runtime，
包括未安装及没有当前设备兼容版本的包；不兼容的包仍可查看，但不能安装。Store 同时通过
`dpkg-query` 合并本机已安装的 `lilygo-*` 系统组件和 `meshtasticd`，排除 Launcher 自身
及带 Launcher 或 Desktop 入口的应用。旧目录或首次启动离线时，本机组件仍能显示。

点击 **Check updates** 刷新目录。未安装的组件可以安装最新兼容版本，也可以在详情页
选择历史版本安装；已安装的组件可以更新，或在确认目标版本后回退。可选版本必须保持
`published` 状态，且架构匹配当前设备或为 `all`；yanked、revoked 及不兼容版本不能安装。
版本按 Debian 规则排序，同一版本优先选择当前架构的包。安装历史版本或回退后，如果
目录中有更新版本，仍会提示可更新。Runtime 继续不提供卸载操作。

安装、更新和已确认的回退复用下载校验及提权 APT 流程，失败后保留错误说明并允许重试。
成功后回读实际安装版本，刷新 runtime 与应用兼容性。本机发现但没有已验证目录记录的
组件仅显示已安装版本及更新信息不可用，不提供未经验证的安装或回退目标。

在线版本管理从已发布的 Registry v1 目录读取 `runtimes` 数组，以及同一 snapshot 下的
runtime 详情和详情 SHA-256。旧版 Store 仍只读取应用目录，Runtime 不混入普通应用
列表或 Installed 应用计数。

可安装及回退的版本必须对应真实的 published release 记录和经过校验的 Debian 资产。
本机发现已安装的包不会使它自动成为可下载包；没有更旧且兼容的历史发布版本时，不提供
或虚构回退目标。板级 overlay（例如 `lilygo-t-display-cm0-overlay`）同样作为 runtime
组件展示和管理。

预览：[竖屏](assets/previews/store-runtime-portrait.png) ·
[横屏](assets/previews/store-runtime-landscape.png)。

应用 ID、名称、版本、说明、许可证、图标、兼容性和 Launcher 排序等项目元数据统一维护在
`lpm.toml`。CMake 配置时通过 LPM 读取并校验这些字段，再生成 Launcher、Desktop、
AppStream 和 Debian 包元数据；不要直接编辑生成结果。

测试或部署私有镜像时可覆盖根地址：

```sh
LILYGO_UI_STORE_REGISTRY_URL=https://mirror.example/v1/root.json \
  ./build/host-simulator/lilygo-ui-store
```

覆盖地址仍必须以 `/v1/root.json` 结尾，且生产网络请求只接受 HTTPS。`file://` 仅在根地址
本身使用该协议时启用，用于离线渲染与协议 fixture 测试。

模拟器可通过 `LILYGO_UI_STORE_ARCHITECTURE=arm64` 或 `riscv64` 预览对应设备的目录。
设备默认通过 `dpkg --print-architecture` 识别架构，查询失败时回退到编译目标 CPU 架构。

## 构建和测试

主机构建需要 Git、CMake 3.21 或更新版本、支持 C++17 的编译器、`make`、
`pkg-config` 和 SDL2 开发包。推荐使用 **[LPM](https://github.com/LILYGO-UI/lpm)**，
它会根据 `lpm.toml` 中的配置完成应用的配置、构建、测试、运行和打包。请安装与当前
schema 兼容的 `lpm` 版本，确保 `lpm` 可从 `PATH` 找到，并在首次克隆后初始化 SDK
submodule：

```sh
git submodule update --init --recursive
```

构建并运行主机测试，然后启动模拟器：

```sh
lpm test
lpm start
```

交叉编译并在 `dist/` 中生成设备安装包：

```sh
lpm pack
```

如需直接调用底层 CMake 工作流，可使用稳定的 presets：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
./build/host-simulator/lilygo-ui-store
```

直接执行设备构建与打包：

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

## 代码格式化

C/C++ 源码使用 `clang-format`（Ruff 仅支持 Python，不能格式化 C/C++）。首次克隆后启用
仓库内置的提交 hook：

```sh
git config core.hooksPath .githooks
```

此后 `git commit` 会自动格式化并重新暂存本次提交中的 C/C++ 文件。为避免意外提交未暂存
内容，hook 遇到部分暂存的文件时会终止提交；先暂存或储藏该文件的剩余改动后再提交。

### 使用 LPM 构建 K230 Debian riscv64

Linux 主机安装 `gcc-riscv64-linux-gnu` 和 `g++-riscv64-linux-gnu`，macOS 主机用 `brew install llvm lld dpkg` 安装交叉编译和打包工具。在应用仓库根目录运行打包命令。

```sh
lpm pack --arch riscv64
```

`k230-debian` CMake 预设首次配置时从 `LILYGO-UI/canaan-gen` Release 下载固定版本的 Debian BSP，校验 SHA-256，并将压缩包和 `sysroot/` 保存在当前项目的 `.cache/k230-bsp/20261116T104343Z/`。之后可以离线打包，重新生成 `build/` 也会复用缓存。生成的 `.deb` 在 `dist/` 目录。

执行一次 `lpm set-target k230` 后，后续直接运行 `lpm pack`。如需自定义 Debian sysroot，在首次配置前设置 `LILYGO_UI_DEBIAN_SYSROOT`，或向 CMake 传入 `-DLILYGO_UI_DEBIAN_SYSROOT=/path/to/sysroot`。AppKit 默认使用 `third_party/cm0-appkit`，可用 `-DLilyGoUI_DIR=/path/to/appkit` 指定其他版本。
