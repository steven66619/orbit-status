#!/bin/sh
set -e

PREFIX="${PREFIX:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DOTFILES="$SCRIPT_DIR/dotfiles"

usage() {
    cat <<'EOF'
Usage: ./install-personal.sh [OPTIONS]

Personal dotfiles + wlstatus installer.

Options:
  -u, --uninstall    Remove installed files and exit
  -p, --prefix DIR   Install to DIR/bin (default: /usr/local)
  -h, --help         Show this help

Environment:
  PREFIX             Same as --prefix (default: /usr/local)
EOF
    exit 0
}

uninstall() {
    echo "==> Removing wlstatus"
    rm -f "$PREFIX/bin/wlstatus"

    echo "==> Removing dotfiles"
    for f in "$DOTFILES/home/"*; do
        name="$(basename "$f")"
        rm -f "$HOME/.$name"
    done

    for dir in "$DOTFILES/config/"*/; do
        name="$(basename "$dir")"
        rm -rf "$HOME/.config/$name"
    done

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

echo "==> Building wlstatus"
make -s clean 2>/dev/null || true
make -s

echo "==> Installing wlstatus to $PREFIX/bin"
install -Dm755 wlstatus "$PREFIX/bin/wlstatus"

echo "==> Deploying dotfiles"

for f in "$DOTFILES/home/"*; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"
    target="$HOME/.$name"
    if [ -f "$target" ] && [ ! -L "$target" ]; then
        cp "$target" "$target.bak.$(date +%s)"
        echo "    backed up $target -> $target.bak.*"
    fi
    cp "$f" "$target"
    echo "    .$name"
done

for dir in "$DOTFILES/config/"*/; do
    [ -d "$dir" ] || continue
    name="$(basename "$dir")"
    target="$HOME/.config/$name"
    if [ -d "$target" ] && [ ! -L "$target" ]; then
        cp -r "$target" "$target.bak.$(date +%s)"
        echo "    backed up $target -> $target.bak.*"
    fi
    mkdir -p "$HOME/.config"
    cp -r "$dir" "$target"
    echo "    .config/$name"
done

echo ""
echo "==> Installation complete."
echo "    Run 'wlstatus' to start the bar."
echo "    Run '$0 --uninstall' to remove."
