# CMake helpers

本目录只保存项目配置、身份生成和交叉工具链所需的 CMake 辅助文件。
构建、测试和打包入口统一记录在仓库根目录的 [README](../README.md)；项目元数据统一
维护在 [`lpm.toml`](../lpm.toml)。

## 旧 CM0 BSP 缺少 Debian 依赖元数据

如果 `lpm pack` 已编译成功，却在 CPack 阶段报
`Target Debian package metadata is missing`，检查当前 sysroot 中的
`var/lib/dpkg/info/`。旧版 CM0 BSP 0.1.0 只保存了 `status`，缺少包文件清单、
`symbols` 和 `shlibs`，无法完成 AppKit 的自动依赖分析。

优先使用从相同系统镜像及开发包重新导出的完整 BSP。也可从该 BSP 对应的软件源获取
与 `var/lib/dpkg/status` 中**包名、架构和版本完全一致**的运行库 `.deb`，集中放入
一个目录，然后在 Store 根目录执行：

```sh
python3 tools/restore-bsp-metadata.py \
  --sysroot .cache/cm0-bsp/0.1.0/sysroot \
  --deb-dir .cache/cm0-bsp/0.1.0/metadata-debs
lpm pack
```

工具需要主机上的 `dpkg-deb`（macOS 可通过 `brew install dpkg` 安装）。它会先核对
整个批次的包身份，并通过 SHA-256 验证包内共享库与 sysroot 中已有文件一致，再恢复对应的
`.list`、`.symbols`、`.shlibs`。库文件、安装版本及维护脚本不会被安装或修改；不匹配的
包和已有但内容不同的元数据会被拒绝。修复可重复执行，CPack 的依赖分析仍然保持启用。

当前 Store 的 CM0 构建需要 `libbrotli1`、`libbz2-1.0`、`libc6`、`libdrm2`、
`libgcc-s1`、`libpng16-16t64`、`libstdc++6`、`zlib1g`、`libglib2.0-0t64`、
`libpolkit-agent-1-0` 和 `libpolkit-gobject-1-0` 的元数据；具体版本以当前
sysroot 的 `status` 为准。清理 `.cache` 后，如果重新下载的仍是旧 BSP，需要再次恢复，
或改用包含完整元数据的 BSP。不要关闭依赖分析来绕过此错误。

工具测试：

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/tests -v
```

## Linux 认证开发依赖

Store 使用系统 polkit 的认证代理及 GLib/GIO。Linux 原生构建需安装
`pkg-config`、`libpolkit-agent-1-dev` 和 `libglib2.0-dev`（Debian 13 的实际
GIO 开发文件由依赖包 `libgio-2.0-dev` 提供）。交叉编译时必须将目标架构、
与 BSP 运行库版本匹配的开发包及其 pkg-config 依赖解包进 sysroot；不要链接主机库。
旧 CM0 BSP 缺少这些头文件，需要补齐 `libpolkit-gobject-1-dev`、GLib/GIO、
`libsysprof-capture-4-dev` 和 zlib 的开发元数据，并按上节恢复精确匹配的运行库元数据。
