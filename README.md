# orbit-status

An ultra-lightweight, high-performance, information-dense status bar built in pure, type-safe C++17 and fully scriptable via modular Lua plugins. 

Inspired by the clean, responsive aesthetic of the **Distro Tube Operating System (DTOS)**, `orbit-status` provides a modern alternative to traditional monolithic bars. It operates on a zero-fork architecture, querying performance metrics directly from the Linux kernel to ensure an ultra-low footprint across all Linux distributions.

## Key Features

* **Zero-Fork Statistics Engine**: Parses `/proc/stat` and `/proc/meminfo` continuously using static `std::ifstream` data streams. It never calls external system applications or subshells, guaranteeing near-zero CPU cycles are wasted on updating the bar itself.
* **Native Wayland Architecture**: Renders via the `wlr-layer-shell` protocol (Sway, Hyprland, river, Wayfire, and other wlroots-based compositors) with double-buffered shared-memory surfaces. Workspace state and the focused window are read directly from the compositor over the Sway/i3 IPC UNIX domain socket — no X11, no XWayland, no external bar daemons.
* **StatusNotifier System Tray**: Owns `org.kde.StatusNotifierWatcher` on the session DBus and renders StatusNotifierItem icons (nm-tray, CopyQ, Steam, etc.) with click support (Activate / SecondaryActivate / ContextMenu) and scroll support (Scroll). Ships with **`volume-sni`**, a Wayland-native volume control that registers as a StatusNotifierItem (left-click toggles mute, scroll changes volume) — a drop-in replacement for the abandoned X11-only `volumeicon`.
* **Isolated Lua Sandboxing**: Loads every discrete status pill into its own independent, sandboxed Lua engine state. Plugins execute safely in separate frames without risking memory access collisions or UI lock-ups.
* **Systemd-Independent Compatibility**: Retains absolute portability. Because data tracking bypasses systemd APIs entirely, `orbit-status` runs out of the box on alternative init systems including **OpenRC**, **runit**, and **s6**, making it a perfect fit for distributions like Void Linux, Artix, or Alpine.

## Architectural Layout

```text
                    ┌──► wlr-layer-shell surface (bar, popup, tooltip)
                    │
[orbit-status C++ Core]┤
                    │
                    ├──► Sway/i3 IPC socket ($SWAYSOCK) ──► workspaces + focused window
                    │
                    └──► DBus session bus ──► StatusNotifierWatcher + tray items
                            │
                            └─► Loads Isolated Lua States ─► [cpu.lua] [mem.lua]
```

## How It Works (Developer Documentation)

### 1. C++ Master Loop & Environment Isolation
The central execution hub in `main.cpp` drives a `poll()` loop over the Wayland display, a 1-second `timerfd` tick, an inotify watch on the config directory, the DBus tray file descriptor, and a SIGHUP reload handler. On each tick it queries the compositor over the Sway/i3 IPC socket (`$SWAYSOCK` or `$I3SOCK`) for the workspace list and focused window, then re-renders the bar into a double-buffered `wl_shm` surface. Clicking a workspace pill runs `workspace_switch_cmd` (default `swaymsg workspace number %d`).

### 2. Lua Plugin Interface (`.lua`)
Every configuration module placed inside your user configuration directory must implement a core execution function. By utilizing persistent scoped states, the C++ engine feeds information to Lua seamlessly:

```lua
-- Sample real-time CPU metric loop structure
interval = 1 -- Tells the engine core to update this block every 1 second

function tick()
    -- Your pure Lua string manipulation or system calculation logic here
    return "    12% "
end
```

## Building & Installation

For a comprehensive layout of required software headers, compilation instructions, and package manager execution strings across Arch, Debian, Fedora, and Void Linux, refer directly to our **[BUILD.md](./BUILD.md)** specification document.

