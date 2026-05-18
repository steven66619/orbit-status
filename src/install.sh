#!/bin/sh
set -e

PREFIX="${PREFIX:-/usr/local}"
if [ "$(id -u)" -eq 0 ] && [ -n "$SUDO_USER" ]; then
    REAL_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
    CONFIG_DIR="${CONFIG_DIR:-$REAL_HOME/.config/wlstatus}"
else
    CONFIG_DIR="${CONFIG_DIR:-$HOME/.config/wlstatus}"
fi
BINARY="$PREFIX/bin/wlstatus"

usage() {
    cat <<'EOF'
Usage: ./install.sh [OPTIONS]

Options:
  -u, --uninstall    Remove wlstatus and exit
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
for pkg in wayland-client cairo pangocairo; do
    if ! pkg-config --exists "$pkg" 2>/dev/null; then
        echo "ERROR: missing $pkg"
        exit 1
    fi
done
echo "    all found"

echo "==> Building"
make -s clean 2>/dev/null || true
make -s

echo "==> Installing to $BINARY"
install -Dm755 wlstatus "$BINARY"

if [ ! -f "$CONFIG_DIR/config" ]; then
    echo "==> Copying example config to $CONFIG_DIR/config"
    mkdir -p "$CONFIG_DIR"
    cp "$(dirname "$0")/wlstatus.conf.example" "$CONFIG_DIR/config"
else
    echo "==> Config exists at $CONFIG_DIR/config, skipping"
fi

echo ""
echo "==> Installation complete."
echo "    Run 'wlstatus' to start the bar."
echo "    Run '$0 --uninstall' to remove."
