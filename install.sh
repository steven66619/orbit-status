#!/bin/sh
set -e

PREFIX="${PREFIX:-/usr/local}"
if [ "$(id -u)" -eq 0 ] && [ -n "$SUDO_USER" ]; then
    REAL_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
    CONFIG_DIR="${CONFIG_DIR:-$REAL_HOME/.config/orbit-status}"
else
    CONFIG_DIR="${CONFIG_DIR:-$HOME/.config/orbit-status}"
fi
BINARY="$PREFIX/bin/orbit-status"

usage() {
    cat <<'EOF'
Usage: ./install.sh [OPTIONS]

Options:
  -u, --uninstall    Remove orbit-status and exit
  -p, --prefix DIR   Install to DIR/bin (default: /usr/local)
  -h, --help         Show this help

Environment:
  PREFIX             Same as --prefix (default: /usr/local)
EOF
    exit 0
}

uninstall() {
    echo "==> Removing $BINARY"
    rm -f "$BINARY"
    rm -f "$PREFIX/bin/volume-sni"
    rm -f "$PREFIX/bin/orbit-status-update"
    rm -rf "$PREFIX/share/orbit-status/plugins"
    echo "==> Done"
    exit 0
}

for arg in "$@"; do
    case "$arg" in
        -h|--help) usage ;;
        -u|--uninstall) uninstall ;;
        --prefix=*) PREFIX="${arg#*=}" ;;
        -p) echo "use --prefix=PATH or PREFIX env var"; exit 1 ;;
    esac
done

echo "==> Checking dependencies"
for pkg in wayland-client wayland-protocols cairo pangocairo librsvg-2.0 dbus-1 lua; do
    if ! pkg-config --exists "$pkg" 2>/dev/null; then
        echo "ERROR: missing $pkg"
        exit 1
    fi
done
echo "    all found"

echo "==> Building"
make -s clean 2>/dev/null || true
make -s orbit-status volume-sni

echo "==> Installing to $BINARY"
install -Dm755 orbit-status "$BINARY"
install -Dm755 volume-sni "$PREFIX/bin/volume-sni"
install -Dm755 scripts/bar-update "$PREFIX/bin/orbit-status-update"
install -d "$PREFIX/share/orbit-status/plugins"
install -m644 plugins/*.lua "$PREFIX/share/orbit-status/plugins/" 2>/dev/null || true

if [ ! -f "$CONFIG_DIR/config" ]; then
    echo "==> Copying example config to $CONFIG_DIR/config"
    mkdir -p "$CONFIG_DIR"
    cp "$(dirname "$0")/orbit-status.conf.example" "$CONFIG_DIR/config"
else
    echo "==> Config exists at $CONFIG_DIR/config, skipping"
fi

echo ""
echo "==> Installation complete."
echo "    Run 'orbit-status' to start the bar."
echo "    Run '$0 --uninstall' to remove."
