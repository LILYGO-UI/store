# LILYGO UI Store

English | [简体中文](README.zh-CN.md)

`lilygo-ui-store` is the application store for LILYGO UI devices. It provides
a responsive on-device interface, a Registry v1 client, and an independent
store state model for browsing registry applications, viewing locally installed
applications, and installing, updating, or removing packages.

## Features

- Browse all applications and filter by Official or Community source
- View application author, version, size, and description
- Install applications and update those with newer versions
- Automatically install Debian dependencies from the device's configured APT sources
- View and remove locally installed applications
- Load immutable application catalog snapshots from LILYGO Registry Pages
- Verify Debian package size and SHA-256 before installation
- Adapt to AppKit's default 568x1232 portrait, 1232x568 landscape, and 320x568
  compact portrait displays
- Automatically respect the 96 px status bar and 38 px Home Indicator safe areas
  managed by AppKit

By default, the Store loads Registry v1 from this read-only Pages endpoint:

```text
https://lilygo-ui.github.io/packages/v1/root.json
```

The client first reads `root.json` and requires the index URL to reference the
same `snapshot_id`. After downloading the index, it verifies the SHA-256 declared
by the root document, then reads the details for each application in that
snapshot. The protocol version, snapshot, application identity, and digest fields
must agree across the root document, index, and details. If any validation fails,
the partial catalog is discarded.

The Store caches the verified `root.json`, index, and application detail
documents by registry root URL. On subsequent starts, the cached snapshot is
loaded entirely from local files while the remote snapshot refreshes
concurrently. The local result can therefore be displayed without waiting for
the network; a successful remote result then refreshes the page and atomically
publishes a new cache. If the remote result finishes first, an older cached
result cannot overwrite it. Missing or corrupt cache data is ignored without
affecting remote loading.

Install and update operations download the `.deb` referenced by the application
details from the central GitHub Release. For official content-addressed assets,
the Store first resolves the asset download endpoint through the GitHub API and
falls back to the original registry URL if the API is unavailable. After the
download completes, the Store strictly verifies the file size and SHA-256 and
invokes its packaged `lilygo-ui-store-package-install` helper through `pkexec`.
The helper stages a private copy, rechecks its digest and Debian identity,
refreshes APT indexes, and uses `apt-get install` to install the local package
with its required dependencies. Store reads the installed target version back
with `dpkg-query`. Removal uses `pkexec dpkg --remove`, and the Store prevents users
from removing the Store itself. Catalog refreshes and package operations run on
worker threads; the LVGL thread only receives results and updates the interface.

Dependencies are resolved by APT 2.2 or newer from the device's existing Debian/Raspberry Pi
sources. They must be declared in the package's `Depends` or `Pre-Depends` fields
and available in those sources (or already installed at a suitable version).
The GitHub `packages` application catalog is not an APT source; Store does not
add repositories or download other catalog applications as dependencies.
Installations are noninteractive, preserve existing configuration files, and
refuse transactions that remove packages. Index refresh or dependency resolution
failures are reported without falling back to `dpkg --install`.

`lilygo-ui-launcher` is handled as a protected system component. Store never
offers to remove it and does not install it as a new package. When an installed
Launcher has an update, Store verifies the Registry artifact normally. Its install
helper first uses `apt-get satisfy` to install the new package's required
dependencies and check `Conflicts`/`Breaks` while retaining the current Launcher
version, then hands the package
to `/usr/lib/lilygo-ui-launcher/lilygo-ui-launcher-update`. The Launcher helper
copies the artifact into root-owned storage and starts the independent update
service; after that handoff succeeds, Store exits so Launcher can be upgraded
outside the Launcher service cgroup and restarted. Devices whose Launcher does
not yet contain this helper require one administrator- or image-managed baseline
upgrade before Store-mediated updates are available.

The source is organized around clear boundaries: `registry` handles transport,
JSON, and snapshot validation; `system` wraps processes and Debian package
management; `service` maps protocol data into Store models; and `pages/<feature>`
contains each View and ViewModel. Core code does not depend on the Launcher or
another application repository.

For more engineering details, see the [project overview](docs/00-overview.md),
[UI guidelines](docs/01-user-interface.md), and [architecture notes](docs/02-architecture.md).

Project metadata such as the application ID, name, version, description, license,
icons, compatibility, and Launcher ordering is maintained exclusively in
`lpm.toml`. During CMake configuration, LPM reads and validates these fields, then
generates the Launcher, Desktop, AppStream, and Debian package metadata. Do not
edit generated output directly.

Override the registry root URL when testing or deploying a private mirror:

```sh
LILYGO_UI_STORE_REGISTRY_URL=https://mirror.example/v1/root.json \
  ./build/host-simulator/lilygo-ui-store
```

The override URL must still end with `/v1/root.json`, and production network
requests accept HTTPS only. The `file://` scheme is enabled only when the root URL
itself uses that scheme, for offline rendering and protocol fixture tests.

## Build and Test

Host builds require Git, CMake 3.21 or newer, a C++17-capable compiler, `make`,
`pkg-config`, and the SDL2 development package. The recommended workflow uses
**[LPM](https://github.com/LILYGO-UI/lpm)** to configure, build, test, run, and
package the application from the settings in `lpm.toml`. Install a version of
`lpm` compatible with the current schema, ensure it is available on `PATH`, then
initialize the SDK submodule after the first clone:

```sh
git submodule update --init --recursive
```

Build and run the host tests, then start the simulator:

```sh
lpm test
lpm start
```

Cross-compile and create the device package in `dist/`:

```sh
lpm pack
```

To invoke the underlying CMake workflow directly, use the stable presets:

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
./build/host-simulator/lilygo-ui-store
```

For a direct device build and package:

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

## Code Formatting

C/C++ sources use `clang-format` (Ruff supports Python only and cannot format
C/C++). Enable the repository's commit hook after the first clone:

```sh
git config core.hooksPath .githooks
```

Future `git commit` operations will automatically format and restage the C/C++
files included in the commit. To avoid accidentally committing unstaged content,
the hook aborts when it encounters a partially staged file; stage or stash the
remaining changes in that file before committing.
