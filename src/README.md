# wlstatus-personal

Personal variant of [wlstatus](https://github.com/steven66619/wlstatus-new) — a lightweight Wayland status bar.

## Features

- **Auto-reload** — config changes are picked up instantly via inotify
- **Modular design** — enable/disable any section: clock, CPU, memory, disk, updates, volume, network, battery, power buttons
- **Custom command modules** — up to 4 user-defined pills that run any script/command
- **Tooltip popups** — hover over any pill to see extended info (top processes, pending updates, disk usage, etc.)
- **Icons** — customizable icons/prefixes per module (unicode symbols or text)
- **Color-by-state** — updates pill turns red when updates are pending, disk pill turns orange when above threshold
- **Configurable anchor** — bar can be placed at top or bottom
- **Click commands** — assign commands to any pill (e.g. `click_cpu = foot htop`)
- **Lightweight** — single C binary, no JavaScript, no CSS, no dependencies beyond Wayland + Cairo + Pango

## Package Repositories

Packages are hosted at `https://steven66619.github.io/wlstatus-new/`.

### DNF (Fedora / RHEL)

```bash
sudo dnf config-manager addrepo --from-repofile=https://steven66619.github.io/wlstatus-new/wlstatus-personal.repo
sudo dnf install wlstatus-personal
```

### APT (Debian / Ubuntu)

```bash
curl -fsSL https://steven66619.github.io/wlstatus-new/GPG-KEY | sudo gpg --dearmor -o /usr/share/keyrings/wlstatus.gpg
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/wlstatus.gpg] https://steven66619.github.io/wlstatus-new/apt stable main" | sudo tee /etc/apt/sources.list.d/wlstatus.list
sudo apt update && sudo apt install wlstatus-personal
```

### Arch Linux

Add to `/etc/pacman.conf`:

```ini
[wlstatus-personal]
SigLevel = Optional TrustAll
Server = https://steven66619.github.io/wlstatus-new/arch/x86_64
```

```bash
sudo pacman -Sy wlstatus-personal
```

### Post-Install

```bash
wlstatus-personal-setup
```

This copies config files to `~/.config/wlstatus/config` and dotfiles to `~/.config/`.

## Configuration

Config is at `~/.config/wlstatus/config` and reloads automatically on save.

### Module visibility

```ini
show_clock    = 1
show_cpu      = 1
show_mem      = 1
show_updates  = 1
show_disk     = 1
show_volume   = 1
show_network  = 1
show_battery  = 1
show_power    = 1
```

Set any to `0` to hide it.

### Icons / prefixes

```ini
cpu_icon    = 🖥
mem_icon    = ☰
updates_icon = ⬆
disk_icon   = 💾
volume_icon = 🔊
battery_icon = 🔋
```

Set to any text or unicode symbol.

### Date/time

```ini
date_format = %a %b %d  %H:%M
```

### Bar position

```ini
bar_anchor = top   # or "bottom"
```

### Update & disk intervals

```ini
update_interval = 30    # seconds between update checks
disk_interval   = 120   # seconds between disk usage checks
```

Custom update command (leave empty for auto-detect):

```ini
update_cmd = dnf check-update -q
```

### Color alerts

```ini
updates_alert_color = 1.0 0.2 0.2   # shown when updates > 0
disk_warn_color     = 1.0 0.6 0.0   # shown when disk >= disk_warn_threshold
disk_warn_threshold = 90
```

### Click commands per module

```ini
click_cpu     = foot htop
click_mem     = foot btop
click_updates = foot sh -c "pacman -Qu | less"
click_disk    = foot df -h
click_volume  = pavucontrol
click_network = foot nmtui
click_battery = foot powerprofilesctl
```

### Custom command modules

Add up to 4 custom pills that run any command and display the output:

```ini
show_custom_1 = 1
custom_1_cmd  = ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | grep -v 127.0.0.1 | head -1
custom_1_prefix = IP
custom_1_interval = 600        # seconds between updates
custom_1_color = 1.0 0.5 0.0   # border color
click_custom_1 = foot ip addr
```

### Pill sizing

```ini
pill_pad_h = 4
pill_pad_w = 8
pill_gap   = 6
```

## Tooltips

Hover over any data pill to see detailed information:
- **CPU** — top 5 processes by CPU usage
- **MEM** — top 5 processes by memory usage
- **UPD** — pending package updates
- **DSK** — filesystem usage
- **VOL** — current volume and mute status
- **NET** — WiFi link information
- **BAT** — battery status

## Build from Source

```sh
make
sudo make install
```

Or use the install script:

```sh
./install-personal.sh
```

## Signal / Auto-reload

The bar reloads automatically when the config file changes. You can also force a reload with:

```sh
kill -HUP $(pgrep wlstatus)
```
