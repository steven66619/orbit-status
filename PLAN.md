# orbit-status Development Plan

## Current Architecture

`orbit-status` is a native Wayland status bar. It renders via the `wlr-layer-shell`
protocol and reads workspace/window state from the Sway/i3 IPC socket. It also
implements a StatusNotifierItem system tray over the session DBus.

### Components

- **`main.cpp`** — Wayland setup, `poll()` event loop, input handling, tray/DBus wiring.
- **`bar.cpp` / `bar.hpp`** — Rendering (workspaces, clock, pills, power buttons) and clickable regions.
- **`lua_plugin.cpp`** — Isolated Lua plugin states for status pills.
- **`sway_ipc.cpp`** — Minimal Sway/i3 IPC client + JSON parser.
- **`sni_tray.cpp`** — StatusNotifierWatcher/StatusNotifierItem tray over DBus.
- **`config.hpp`** — `key = value` config parser.

## Roadmap

### 1. Performance
- [ ] Persistent Sway IPC connection (avoid reconnect on every tick).
- [ ] Non-blocking tooltip/tray property fetches (avoid blocking the event loop).

### 2. Tray
- [ ] Tooltip support for tray items (hover text).
- [ ] Handle item `NewTitle` / `NewToolTip` signals more completely.
- [ ] Configurable tray ordering / hidden items.

### 3. Wayland
- [ ] Multi-monitor support (one bar per output).
- [ ] Optional `xdg-shell` fallback for non-wlroots compositors.

### 4. Config
- [ ] Hot-reload of tray icon size without reconnecting DBus.
- [ ] Per-plugin click/scroll command configuration.

## Notes

- The X11/Xorg backend was dropped in the native Wayland port (`588e703`).
- The tray uses a private DBus connection (shared connections must not be closed).
