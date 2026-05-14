# wlstatus Package Repositories

## APT (Debian / Ubuntu)

```bash
# Import GPG key
curl -fsSL https://steven66619.github.io/wlstatus/GPG-KEY | sudo gpg --dearmor -o /usr/share/keyrings/wlstatus.gpg

# Add repository
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/wlstatus.gpg] https://steven66619.github.io/wlstatus/apt stable main" | sudo tee /etc/apt/sources.list.d/wlstatus.list

# Install
sudo apt update
sudo apt install wlstatus
```

Without GPG verification (not recommended):
```bash
echo "deb [trusted=yes] https://steven66619.github.io/wlstatus/apt stable main" | sudo tee /etc/apt/sources.list.d/wlstatus.list
sudo apt update && sudo apt install wlstatus
```

## YUM / DNF (Fedora / RHEL)

```bash
# Add repository
sudo dnf config-manager --add-repo https://steven66619.github.io/wlstatus/yum/

# Install
sudo dnf install wlstatus
```

Or manually:
```bash
sudo curl -L -o /etc/yum.repos.d/wlstatus.repo \
  https://steven66619.github.io/wlstatus/yum/wlstatus.repo
sudo dnf install wlstatus
```

## Arch Linux

Add to `/etc/pacman.conf`:

```ini
[wlstatus]
SigLevel = Optional TrustAll
Server = https://steven66619.github.io/wlstatus/arch/x86_64
```

Then install:

```bash
sudo pacman -Sy wlstatus
```

Or download the `.pkg.tar.zst` directly from the [Releases page](https://github.com/steven66619/wlstatus/releases).

## Manual download

Grab the latest `.deb`, `.rpm`, or `.pkg.tar.zst` from the [Releases page](https://github.com/steven66619/wlstatus/releases).
