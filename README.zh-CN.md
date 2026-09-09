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
- 从 LILYGO Registry Pages 读取不可变应用目录 snapshot
- 安装前校验 Debian 包大小和 SHA-256
- 适配 AppKit 默认的 568×1232 竖屏、1232×568 横屏和 320×568 紧凑竖屏
- 自动避让 AppKit 管理的 96px 状态栏和 38px Home Indicator 安全区

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

安装和更新会从详情中的中央 GitHub Release 下载 `.deb`；官方内容寻址资产优先通过
GitHub API 解析到资产下载端点，并在 API 不可用时回退 Registry 中的原始 URL。下载完成后
严格核对文件大小和 SHA-256，再通过 `pkexec` 调用随包安装的
`lilygo-ui-store-package-install` helper。helper 复制并重新校验包的 SHA-256 与 Debian
身份，刷新 APT 索引，通过 `apt-get install` 安装本地包并自动补齐所需依赖，再由
Store 使用 `dpkg-query` 回读目标版本。卸载通过 `pkexec dpkg --remove` 完成，商店禁止卸载自身。目录刷新和包操作
均在工作线程执行，LVGL 线程只接收结果和更新页面。

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
helper 先通过 `apt-get satisfy` 补齐新版的必需依赖、检查 `Conflicts`/`Breaks`，并保持当前 Launcher 版本，再交给
`/usr/lib/lilygo-ui-launcher/lilygo-ui-launcher-update`。helper 将软件包复制到 root
管理的目录并启动独立更新服务；交接成功后 Store 退出，使升级脱离 Launcher service
cgroup 完成，并由更新服务重新启动 Launcher。尚未包含该 helper 的旧版本需要由系统
镜像或管理员先完成一次基线升级。

进一步的工程约定见 [项目总览](docs/00-overview.md)、
[UI 开发规范](docs/01-user-interface.md) 和 [架构说明](docs/02-architecture.md)。

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
