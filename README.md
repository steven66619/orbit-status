# wlstatus-personal

Personal variant of [wlstatus](https://github.com/steven66619/wlstatus) — a lightweight Wayland status bar bundled with dotfiles for Hyprland, Waybar, Kitty, Fish, and more.

## Package Repositories

Packages are hosted at `https://steven66619.github.io/wlstatus/`.

### DNF (Fedora / RHEL)

First enable the Hyprland COPR (not in Fedora 44+ main repos):

```bash
sudo dnf copr enable ashbuk/Hyprland-Fedora
```

Then add the personal repo:

```bash
sudo dnf config-manager addrepo --from-repofile=https://steven66619.github.io/wlstatus/wlstatus-personal.repo
sudo dnf install wlstatus-personal
```

Or manually:

```bash
sudo curl -L -o /etc/yum.repos.d/wlstatus-personal.repo \
  https://steven66619.github.io/wlstatus/wlstatus-personal.repo
sudo dnf install wlstatus-personal
```

### APT (Debian / Ubuntu)

```bash
curl -fsSL https://steven66619.github.io/wlstatus/GPG-KEY | sudo gpg --dearmor -o /usr/share/keyrings/wlstatus.gpg
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/wlstatus.gpg] https://steven66619.github.io/wlstatus/apt stable main" | sudo tee /etc/apt/sources.list.d/wlstatus.list
sudo apt update && sudo apt install wlstatus-personal
```

### Arch Linux

Add to `/etc/pacman.conf`:

```ini
[wlstatus-personal]
SigLevel = Optional TrustAll
Server = https://steven66619.github.io/wlstatus/arch/x86_64
```

```bash
sudo pacman -Sy wlstatus-personal
```

### Post-Install

After installing the package, deploy the dotfiles:

```bash
wlstatus-personal-setup
```

This copies config files to `~/.config/` (hypr, waybar, kitty, fish, etc.) and shell rc files to `~/.`.

## Build from Source

```sh
make
sudo make install
```

Or use the install script (builds wlstatus and deploys dotfiles):

```sh
./install-personal.sh
```

## What's Included

| Package | Contents |
|---|---|
| `wlstatus` | Status bar binary |
| `wlstatus-personal-setup` | Dotfile deployment script |
| `dotfiles/home/` | Shell rc files (.bashrc, .zshrc, .profile, etc.) |
| `dotfiles/config/` | App configs (hypr, waybar, kitty, fish, Thunar, etc.) |

## Dotfiles

| Path | Contents |
|---|---|
| `~/.bashrc` | Bash config with history, aliases, prompt |
| `~/.zshrc` | Zsh config |
| `~/.config/hypr/` | Hyprland, hyprpaper, hyprtoolkit configs + scripts |
| `~/.config/waybar/` | Waybar config, style, power menu, scripts |
| `~/.config/kitty/` | Kitty terminal config with themes |
| `~/.config/fish/` | Fish shell with tide prompt, abbr, functions |
| `~/.config/Thunar/` | Thunar file manager config |
| `~/.config/atuin/` | Atuin shell history config |
| `~/.config/matugen/` | Matugen theme templates |
| `~/.config/kate/` | Kate editor external tools |
| `~/.config/xfce4/` | Xfce (Thunar) settings |
