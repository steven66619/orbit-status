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
    echo "==> Creating example config at $CONFIG_DIR/config"
    mkdir -p "$CONFIG_DIR"
    cat > "$CONFIG_DIR/config" <<'CONFIG'
# wlstatus config
bar_height = 38
bar_padding = 8

poweroff_color = 1.0 0.2 0.3
reboot_color = 1.0 0.6 0.0
suspend_color = 0.6 0.2 1.0

icon_size = 24
glow_width = 8
glow_alpha_percent = 25
icon_alpha_percent = 90
icon_hover_alpha_percent = 100

show_hyperion = 1
show_hyperion_logo = 1
hyperion_color = 0.92 0.72 0.0

accent_color = 0.0 0.90 1.0
CONFIG
else
    echo "==> Config exists at $CONFIG_DIR/config, skipping"
fi

echo ""
echo "==> Installation complete."
echo "    Run 'wlstatus' to start the bar."
echo "    Run '$0 --uninstall' to remove."
