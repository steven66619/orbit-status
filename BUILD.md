# Building wlstatus

A lightweight, information-dense status bar written in modern, type-safe C++17 and scriptable via modular Lua plugins. It features a zero-fork architecture that parses hardware statistics directly from the virtual Linux `/proc` filesystem with near-zero overhead.

## Dependencies

Ensure the following development packages are installed on your system before compiling. Because `wlstatus` relies strictly on core kernel streams and raw protocol sockets, it is completely independent of systemd or specific init systems.

* **Compiler**: `g++` (supporting C++17) or `clang++`
* **Build System**: `make`
* **Core Libraries**: `lua` (5.4 recommended), `cairo`
* **Xorg Backend Only**: `libX11`

### Distro Installation Commands

Choose the command matching your Linux distribution to install all necessary compiler toolchains and development headers:

* **Arch Linux / CachyOS**: 
  ```bash
  sudo pacman -S base-devel lua cairo libx11 pkgconf
  ```
* **Void Linux (glibc or musl)**: 
  ```bash
  sudo xbps-install -S base-devel lua54-devel cairo-devel libX11-devel pkg-config
  ```
* **Debian / Ubuntu**: 
  ```bash
  sudo apt install build-essential liblua5.4-dev libcairo2-dev libx11-dev pkg-config
  ```
* **Fedora**: 
  ```bash
  sudo dnf groupinstall "Development Tools" && sudo dnf install lua-devel cairo-devel libX11-devel pkgconfig
  ```

## Compilation Targets

The project utilizes C++ preprocessor flags (`#ifdef`) inside the `Makefile` to isolate window manager backends. This eliminates technical debt by ensuring your binary only compiles the exact protocol code your environment needs.

### 1. Wayland Target (Hyprland IPC)
Compiles the bar to listen directly to the native Hyprland UNIX domain socket (`.socket2.sock`). 
```bash
make wayland
```
*This generates a standalone binary named `wlstatus` optimized for Wayland.*

### 2. Xorg Target (XMonad Property Atom)
Compiles the bar to hook directly into the X11 Root Window via `Xlib`, using an efficient event-driven listener that blocks until the `_XMONAD_LOG` atom mutates.
```bash
make xorg
```
*This generates a standalone binary named `wlstatus-x11` optimized for Xorg environments.*

### 3. Workspace Cleanup
To wipe out temporary object tracking binaries and safely reset your workspace development environment before a new compilation pass:
```bash
make clean
```

## Project Directory Structure

* **`main.cpp`** - Core C++ engine initialization, compile-time backend routing, and the master window draw cycle.
* **`lua_plugin.cpp`** - Type-safe C++ wrapper responsible for initializing, scoping, and executing isolated Lua plugin states.
* **`plugins/`** - User configuration folder containing the dynamic `.lua` layout modules (e.g., `cpu.lua`, `mem.lua`) that feed string outputs to the bar pills.

