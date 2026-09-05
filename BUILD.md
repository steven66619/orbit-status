# Building orbit-status

A lightweight, information-dense status bar written in modern, type-safe C++17 and scriptable via modular Lua plugins. It renders natively on Wayland via the `wlr-layer-shell` protocol and reads workspace/window state directly from the Sway/i3 IPC socket — no X11, no XWayland, no external bar daemons.

## Dependencies

Ensure the following development packages are installed on your system before compiling. `orbit-status` is independent of systemd and specific init systems.

* **Compiler**: `g++` (supporting C++17) or `clang++`
* **Build System**: `make`
* **Core Libraries**: `lua` (5.4 recommended), `cairo`, `pango`, `librsvg`, `dbus-1`
* **Wayland**: `wayland-client`, `wayland-protocols`, `wayland-scanner`

### Distro Installation Commands

Choose the command matching your Linux distribution to install all necessary compiler toolchains and development headers:

* **Arch Linux / CachyOS**:
  ```bash
  sudo pacman -S base-devel lua cairo pango librsvg dbus wayland wayland-protocols pkgconf
  ```
* **Void Linux (glibc or musl)**:
  ```bash
  sudo xbps-install -S base-devel lua54-devel cairo-devel pango-devel librsvg-devel dbus-devel wayland-devel wayland-protocols pkg-config
  ```
* **Debian / Ubuntu**:
  ```bash
  sudo apt install build-essential liblua5.4-dev libcairo2-dev libpango1.0-dev librsvg2-dev libdbus-1-dev libwayland-dev wayland-protocols pkg-config
  ```
* **Fedora**:
  ```bash
  sudo dnf groupinstall "Development Tools" && sudo dnf install lua-devel cairo-devel pango-devel librsvg2-devel dbus-devel wayland-devel wayland-protocols-devel pkgconfig
  ```

## Building

The project builds a single native Wayland binary. The `Makefile` generates the `wlr-layer-shell` and `xdg-shell` client protocol stubs with `wayland-scanner` at build time.

```bash
make
```

This produces a standalone binary named `orbit-status`.

### Install

```bash
sudo make install
```

Installs the binary to `/usr/local/bin/orbit-status`, the update helper to `/usr/local/bin/orbit-status-update`, and the bundled Lua plugins to `/usr/local/share/orbit-status/plugins/`.

> **Install prefix matters for autostart.** The default `PREFIX=/usr/local` puts the binary in `/usr/local/bin/orbit-status`, while distro packages build with `PREFIX=/usr` (see `PKGBUILD`) and install to `/usr/bin/orbit-status`. When wiring up autostart in your sway config, point `exec` at the actual location — `command -v orbit-status` tells you where it landed. See the *Autostarting* section in [README.md](./README.md).

### Workspace Cleanup

To wipe out temporary object files and the generated protocol stubs before a fresh build:

```bash
make clean
```

### Releasing a New Version

```bash
./scripts/release          # patch bump of PKGBUILD's pkgver (e.g. 1.4 -> 1.5)
./scripts/release 2.0      # or an explicit version
```

One command runs the whole release: clean-tree and remote preflight checks, PKGBUILD `pkgver` bump, commit, `v<version>` tag, push (the PKGBUILD sources the tag tarball from GitHub, so it must land first), `makepkg` build with a `.PKGINFO` version check, refresh of `repo/arch/x86_64/` (new package in, old one out, `repo-add`, db/files restored as regular files for the raw-URL fallback), and a final commit + push that triggers the Pages deploy workflow. Requires an Arch machine with `makepkg`/`repo-add` and push access to the origin remote.

## Project Directory Structure

* **`main.cpp`** - Core C++ engine: Wayland setup, the `poll()` event loop, input handling, and the tray/DBus integration.
* **`bar.cpp` / `bar.hpp`** - Bar rendering (workspaces, clock, pills, power buttons) and clickable-region tracking.
* **`lua_plugin.cpp`** - Type-safe C++ wrapper for initializing and executing isolated Lua plugin states.
* **`sway_ipc.cpp`** - Minimal Sway/i3 IPC client (workspaces + focused window) with a small JSON parser.
* **`sni_tray.cpp`** - StatusNotifierItem/StatusNotifierWatcher system tray over DBus. Owns the watcher name with takeover/retry for stale instances, and falls back to host mode (registering against another app's watcher and syncing its item list) when a shell/panel owns the name.
* **`volume-sni.cpp`** - Standalone Wayland-native volume control that registers as a StatusNotifierItem (left-click toggles mute, scroll changes volume via `pactl`). Built and installed alongside `orbit-status` as `volume-sni`. `orbit-status` autostarts it once the SNI watcher is owned, and volume-sni re-registers if the watcher appears or restarts later; a single-instance DBus name lock prevents duplicate tray icons if it is also started from the session config.
* **`config.hpp`** - Simple `key = value` config parser.
* **`plugins/`** - Bundled `.lua` layout modules (e.g., `cpu.lua`, `mem.lua`) that feed string outputs to the bar pills.
