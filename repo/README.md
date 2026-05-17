# wlstatus Package Repositories

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

Add to `/etc/pacman.conf`:

```ini
[wlstatus]
SigLevel = Optional TrustAll
Server = https://steven66619.github.io/wlstatus-new/arch/x86_64
```

```bash
sudo pacman -Sy wlstatus
```

## Build from Source

```sh
make
sudo make install
```

Dependencies: `wayland-client`, `cairo`, `pangocairo`, `xkbcommon`, `lua5.4`.
