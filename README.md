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

Or use the install script (handles dependencies, build, install, and initial config):

```sh
./install.sh
```

### Package Repositories

Packages are available via GitHub Pages at `https://steven66619.github.io/wlstatus/`.

**Debian/Ubuntu** — add the APT repo:

```sh
curl -fsSL https://steven66619.github.io/wlstatus/GPG-KEY | sudo gpg --dearmor -o /usr/share/keyrings/wlstatus.gpg
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/wlstatus.gpg] https://steven66619.github.io/wlstatus/apt stable main" | sudo tee /etc/apt/sources.list.d/wlstatus.list
sudo apt update && sudo apt install wlstatus
```

**Fedora/RHEL** — add the YUM repo:

```sh
sudo dnf config-manager --add-repo https://steven66619.github.io/wlstatus/yum/wlstatus.repo
sudo dnf install wlstatus
```

**Arch Linux** — add to `/etc/pacman.conf`:

```ini
[wlstatus]
SigLevel = Optional TrustAll
Server = https://steven66619.github.io/wlstatus/arch/x86_64
```

```sh
sudo pacman -Sy wlstatus
```

### Build from Source

```sh
make
sudo make install
```

Or use the install script (handles dependencies, build, install, and initial config):

```sh
./install.sh
```

### Build Packages Locally

**Arch Linux**:

```sh
makepkg -si
```

**Debian/Ubuntu**:

```sh
sudo apt build-dep .
dpkg-buildpackage -us -uc -b
```

**Fedora/RHEL**:

```sh
rpmbuild -ba wlstatus.spec
```

**All distros** (uses `mkpkg` helper):

```sh
./mkpkg deb-native   # .deb on Arch
./mkpkg rpm-native   # .rpm on Arch
./mkpkg arch         # .pkg.tar.zst on Arch
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

poweroff_color = 1.0 0.2 0.3
reboot_color = 1.0 0.6 0.0
suspend_color = 0.6 0.2 1.0

show_hyperion = 1
show_hyperion_logo = 1
hyperion_color = 0.92 0.72 0.0
accent_color = 0.0 0.90 1.0
```

### Config options

| Option | Default | Description |
|---|---|---|
| `bar_height` | 38 | Bar height in pixels |
| `bar_padding` | 8 | Horizontal padding |
| `bar_bg_color` | *gradient* | Solid bar background color (r g b [a]), set `0 0 0 0` to remove |
| `icon_size` | 24 | Power icon size |
| `glow_width` | 8 | Hover glow width |
| `glow_alpha_percent` | 25 | Glow opacity |
| `icon_alpha_percent` | 90 | Default icon opacity |
| `icon_hover_alpha_percent` | 100 | Hovered icon opacity |
| `poweroff_color` | `1.0 0.2 0.3` | Poweroff icon color (RGB) |
| `reboot_color` | `1.0 0.6 0.0` | Reboot icon color (RGB) |
| `suspend_color` | `0.6 0.2 1.0` | Suspend icon color (RGB) |
| `show_hyperion` | `1` | Show/hide Hyprland workspace section |
| `show_hyperion_logo` | `1` | Show/hide Hyperion logo next to workspaces |
| `hyperion_color` | `0.92 0.72 0.0` | Workspace pill and logo color (RGB) |
| `accent_color` | `0.0 0.90 1.0` | Date/time, info pills, and bottom line accent (RGB) |

## Usage

```sh
wlstatus
```

Click a power icon to open a popup with confirm/cancel buttons. Click workspaces to switch.

## Adding Features

Key files and how they fit together:

| File | Role |
|---|---|
| `main.c` | Wayland event loop, pointer/click handling, popup management |
| `bar.c` | Rendering all bar elements (background, workspaces, power icons, info pills) |
| `bar.h` | Structs (`bar`, `clickable`, `workspace`) and enums (`click_action`) |
| `config.c` / `config.h` | Simple key=value config loading |

### To add a new info pill (e.g. network):

1. Add fields to `struct bar` in `bar.h` (e.g. `int net_percent`)
2. Fill those fields in a new `bar_update_net()` function in `bar.c` (call it from `on_timer` in `main.c`)
3. Draw the pill in `bar_render()` alongside the existing CPU/MEM/UPD/DSK pills
4. Add a `config_get_int` call if you want it configurable

### To add a new clickable action:

1. Add a new `CLICK_*` value to the `click_action` enum in `bar.h`
2. Register a `struct clickable` at the correct x/y/w/h during rendering in `bar.c`
3. Handle the action in `pointer_button()` in `main.c`

### To add a new popup:

1. Add fields to the `popup` anonymous struct inside `struct wl_status` in `main.c`
2. Use the existing `popup_create()` / `popup_destroy()` / `popup_render()` pattern
3. Handle hover via `pointer_motion()` and clicks via `pointer_button()` in `main.c`
