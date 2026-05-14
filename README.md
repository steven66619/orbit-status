# wlstatus

A lightweight Wayland status bar for wlr-layer-shell compositors (Hyprland, Sway, etc.).

## Features

- Workspace switching (Hyprland)
- System info: CPU, memory, disk usage, available updates
- Power options with popup (power off, reboot, suspend)
- Date/time display
- Configurable colors and sizes

## Dependencies

- wayland-client
- cairo
- pangocairo
- wayland-scanner (build)
- wayland-protocols (build)

## Build

```sh
make
sudo make install
```

## Configuration

Create `~/.config/wlstatus/config`. Example:

```
bar_height = 38
bar_padding = 8
icon_size = 24
glow_width = 8
glow_alpha_percent = 25
icon_alpha_percent = 90
icon_hover_alpha_percent = 100

poweroff_color = 1.0 0.2 0.3 1.0
reboot_color = 1.0 0.6 0.0 1.0
suspend_color = 0.6 0.2 1.0 1.0
```

### Config options

| Option | Default | Description |
|---|---|---|
| `bar_height` | 38 | Bar height in pixels |
| `bar_padding` | 8 | Horizontal padding |
| `icon_size` | 24 | Power icon size |
| `glow_width` | 8 | Hover glow width |
| `glow_alpha_percent` | 25 | Glow opacity |
| `icon_alpha_percent` | 90 | Default icon opacity |
| `icon_hover_alpha_percent` | 100 | Hovered icon opacity |
| `poweroff_color` | `1.0 0.2 0.3 1.0` | Poweroff icon color (RGBA) |
| `reboot_color` | `1.0 0.6 0.0 1.0` | Reboot icon color (RGBA) |
| `suspend_color` | `0.6 0.2 1.0 1.0` | Suspend icon color (RGBA) |

## Usage

```sh
wlstatus
```

Click a power icon to open a popup with confirm/cancel buttons. Click workspaces to switch.
