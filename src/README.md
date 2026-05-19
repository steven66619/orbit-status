# wlstatus

A lightweight Wayland status bar for `wlr-layer-shell` compositors (Hyprland, Sway, etc.) with **Lua plugin** support.

## Features

- **Lua plugins** — every module is a Lua script. System info (CPU, memory, disk, network, battery, updates) are bundled Lua plugins. Write your own in Lua — no C compilation needed.
- **Auto-reload** — config changes are picked up instantly via inotify
- **Tooltip popups** — hover over any pill to see extended info (top processes, pending updates, etc.)
- **Color-by-plugin** — each plugin pill gets its own configurable color
- **Click commands** — assign shell commands to any plugin pill
- **Lightweight** — single C binary (~70KB), no JavaScript, no CSS
- **Wayland native** — layer-shell protocol, SHM buffers, cairo/Pango rendering

## Quick Start

```bash
make
sudo make install
```

Then add Lua plugin paths to `~/.config/wlstatus/config` (see example config in repo).

## Lua Plugin API

Each `.lua` file placed in the plugin path is loaded into its own Lua state.

### Required

```lua
function tick()
    -- Return a string to display in the bar pill
    return "my output"
end
```

### Optional

```lua
-- Update interval in seconds (default: 5)
interval = 10

-- Called when the pill is clicked
function on_click()
    -- e.g., os.execute("foot htop")
end

-- Return tooltip text shown on hover
function on_tooltip()
    return "detailed info here"
end
```

### Standard library available

All of Lua 5.4's standard library is available, including `io.popen()`, `io.open()`, `os.execute()`, string matching, etc. Each plugin runs in its own Lua state — they cannot interfere with each other.

## Bundled Plugins

Installed to `/usr/local/share/wlstatus/plugins/`:

| Plugin     | Description              | Interval |
|------------|--------------------------|----------|
| `cpu.lua`  | CPU usage %              | 2s       |
| `mem.lua`  | Memory usage %           | 5s       |
| `disk.lua` | Disk usage %             | 120s     |
| `volume.lua` | Audio volume %         | 3s       |
| `network.lua` | WiFi SSID             | 10s      |
| `battery.lua` | Battery % + charging  | 10s      |
| `updates.lua` | Package update count  | 30s      |

## Configuration

Config is at `~/.config/wlstatus/config` and reloads automatically on save.

### Lua plugin configuration

```ini
# Enable a Lua plugin and point it to its .lua file
lua_plugin_1_path = /usr/local/share/wlstatus/plugins/cpu.lua
show_lua_plugin_1 = 1
lua_plugin_1_prefix = CPU
lua_plugin_1_color = 0.0 0.9 1.0 1.0
click_lua_plugin_1 = foot htop
```

- `lua_plugin_N_path` — path to the Lua script (required)
- `show_lua_plugin_N` — set to `0` to hide (default: `1`)
- `lua_plugin_N_prefix` — text shown before the tick output
- `lua_plugin_N_color` — pill border/fill color as `r g b a`
- `click_lua_plugin_N` — shell command to run on click

### UI elements

```ini
show_clock  = 1
show_power  = 1
show_hyperion = 1
```

### Clock

```ini
date_format = %a %b %d  %H:%M
```

### Pill styling

```ini
pill_pad_h = 4
pill_pad_w = 8
pill_gap   = 6
```

### Bar position

```ini
bar_anchor = top   # or "bottom"
```

## Package Repositories

### DNF (Fedora / RHEL)

```bash
sudo dnf config-manager addrepo --from-repofile=https://steven66619.github.io/wlstatus-new/wlstatus.repo
sudo dnf install wlstatus
```

### APT (Debian / Ubuntu)

```bash
curl -fsSL https://steven66619.github.io/wlstatus-new/GPG-KEY | sudo gpg --dearmor -o /usr/share/keyrings/wlstatus.gpg
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/wlstatus.gpg] https://steven66619.github.io/wlstatus-new/apt stable main" | sudo tee /etc/apt/sources.list.d/wlstatus.list
sudo apt update && sudo apt install wlstatus
```

### Arch Linux

Add to `/etc/pacman.conf`:

```ini
[wlstatus]
SigLevel = Optional TrustAll
Server = https://steven66619.github.io/wlstatus-new/arch/x86_64
```

```bash
sudo pacman -Sy wlstatus
```

### Build from source

```sh
make
sudo make install
```

Dependencies: `wayland-client`, `cairo`, `pangocairo`, `xkbcommon`, `lua5.4`.

## Signal / Auto-reload

The bar reloads automatically when the config file changes. Force a reload with:

```sh
kill -HUP $(pgrep wlstatus)
```
