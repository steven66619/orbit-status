# orbit-status

An ultra-lightweight, high-performance, information-dense status bar built in pure, type-safe C++17 and fully scriptable via modular Lua plugins. 

Inspired by the clean, responsive aesthetic of the **Distro Tube Operating System (DTOS)**, `orbit-status` provides a modern alternative to traditional monolithic bars. It operates on a zero-fork architecture, querying performance metrics directly from the Linux kernel to ensure an ultra-low footprint across all Linux distributions.

## Key Features

* **Zero-Fork Statistics Engine**: Parses `/proc/stat` and `/proc/meminfo` continuously using static `std::ifstream` data streams. It never calls external system applications or subshells, guaranteeing near-zero CPU cycles are wasted on updating the bar itself.
* **Native Wayland Architecture**: Renders via the `wlr-layer-shell` protocol (Sway, Hyprland, river, Wayfire, and other wlroots-based compositors) with double-buffered shared-memory surfaces. Workspace state and the focused window are read directly from the compositor over the Sway/i3 IPC UNIX domain socket — no X11, no XWayland, no external bar daemons.
* **StatusNotifier System Tray**: Owns `org.kde.StatusNotifierWatcher` on the session DBus and renders StatusNotifierItem icons (nm-tray, CopyQ, Steam, etc.) with click support (Activate / SecondaryActivate / ContextMenu) and scroll support (Scroll). Ships with **`volume-sni`**, a Wayland-native volume control that registers as a StatusNotifierItem (left-click toggles mute, scroll changes volume) — a drop-in replacement for the abandoned X11-only `volumeicon`. `orbit-status` autostarts `volume-sni` after the tray is up (no session config needed), and `volume-sni` re-registers if the watcher appears or restarts later, so the icon always shows regardless of start order.
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

## Autostarting

`orbit-status` does not register itself with your session — add one line to start it at login.

**Sway / i3** — add this to `~/.config/sway/config` (use `exec_always` instead if you also want the bar to restart on `swaymsg reload`):

```text
exec_always orbit-status
```

If `orbit-status` is not on your `PATH`, use the full path. It depends on how you installed it:

* `./install.sh` (default) installs to `/usr/local/bin/orbit-status`
* distro packages (`make PREFIX=/usr`, Arch PKGBUILD, etc.) install to `/usr/bin/orbit-status`

Check with `command -v orbit-status`. A stale path here is the classic "bar doesn't autostart" failure — `exec` of a nonexistent path fails silently.

### Tray ownership (why icons always show)

The tray needs to own `org.kde.StatusNotifierWatcher` on the session bus. `orbit-status` handles every conflict natively, with no helper scripts:

* **Another shell/panel owns the name** (plasmashell, xfce4-panel, lxqt-panel, ...): `orbit-status` registers as a StatusNotifierHost against that watcher instead, subscribes to its registration signals, and periodically pulls its item list — so every tray icon still renders in the bar even though the name is taken. (DBus only allows replacing a name whose owner opted in with `AllowReplacement`, which shells never do — so coexisting as a host is the only correct strategy.)
* **A stale/hung previous `orbit-status` owns the name**: the new instance replaces it immediately, because `orbit-status` requests the name with `AllowReplacement`.
* **The previous bar died just before restart** (name still registered asynchronously): the takeover request plus a rate-limited retry in the event loop recovers the name as soon as the bus releases it.
* **Items that registered before the bar came up** are adopted from the watcher's `RegisteredStatusNotifierItems` list, so icons are never missed due to start order.

**Any session with XDG autostart** (GNOME, KDE, or sway via `exec dex --autostart --environment sway`): create `~/.config/autostart/orbit-status.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=orbit-status
Comment=Ultra-lightweight Wayland status bar
Exec=/usr/bin/orbit-status
```

Note that `exec` lines in sway only run when the session starts — they are not re-run on `swaymsg reload`.

## Building & Installation

For a comprehensive layout of required software headers, compilation instructions, and package manager execution strings across Arch, Debian, Fedora, and Void Linux, refer directly to our **[BUILD.md](./BUILD.md)** specification document.

