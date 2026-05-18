# wlstatus Package Repositories

All repositories are signed with [this GPG key](GPG-KEY).
Fingerprint: `908BA367E7E797F3F72B673710F1B1E3F275953D`

## APT (Debian / Ubuntu)

```bash
curl -fsSL https://steven66619.github.io/wlstatus-new/GPG-KEY | sudo gpg --dearmor -o /usr/share/keyrings/wlstatus.gpg
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/wlstatus.gpg] https://steven66619.github.io/wlstatus-new/apt stable main" | sudo tee /etc/apt/sources.list.d/wlstatus.list
sudo apt update && sudo apt install wlstatus
```

## YUM / DNF (Fedora / RHEL)

```bash
sudo dnf config-manager addrepo --from-repofile=https://steven66619.github.io/wlstatus-new/wlstatus.repo
sudo dnf install wlstatus
```

## Arch Linux

Import the signing key and add to `/etc/pacman.conf`:

```bash
# Add and trust the signing key:
curl -fsSL https://steven66619.github.io/wlstatus-new/GPG-KEY | sudo pacman-key --add -
sudo pacman-key --lsign-key 908BA367E7E797F3F72B673710F1B1E3F275953D

# Add to /etc/pacman.conf:
[wlstatus]
SigLevel = Required DatabaseOptional
Server = https://steven66619.github.io/wlstatus-new/arch/x86_64

# Install:
sudo pacman -Sy wlstatus
```

## Void Linux

```bash
# Add repo:
echo 'repository=https://steven66619.github.io/wlstatus-new/void' | sudo tee /etc/xbps.d/wlstatus.conf

# Sync and install (trust the key when prompted):
sudo xbps-install -S wlstatus
```

## Build from Source

```sh
make
sudo make install
```

Dependencies: `wayland-client`, `cairo`, `pangocairo`, `xkbcommon`, `lua5.4`.
