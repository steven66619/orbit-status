#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$REPO_DIR/.." && pwd)"
ARCH_DIR="$REPO_DIR/arch/x86_64"

build_and_update() {
    local pkgbuild="$1"
    local db_name="$2"
    echo "    Building $db_name from $pkgbuild..."
    BUILD_DIR=$(mktemp -d)
    cp "$PROJECT_DIR/$pkgbuild" "$BUILD_DIR/PKGBUILD"
    cp "$PROJECT_DIR"/*.c "$PROJECT_DIR"/*.h "$PROJECT_DIR/Makefile" "$BUILD_DIR/" 2>/dev/null || true
    cp "$PROJECT_DIR/"*.xml "$BUILD_DIR/" 2>/dev/null || true
    (cd "$BUILD_DIR" && makepkg -f --noconfirm)
    cp "$BUILD_DIR"/*.pkg.tar.zst "$ARCH_DIR/"
    rm -rf "$BUILD_DIR"
    cd "$ARCH_DIR"
    repo-add "$db_name.db.tar.zst" "$db_name"-*.pkg.tar.zst
}

echo "==> Updating Arch repo..."
mkdir -p "$ARCH_DIR"

if ! ls "$ARCH_DIR"/wlstatus-*.pkg.tar.zst &>/dev/null; then
    echo "    wlstatus not found, building..."
    build_and_update PKGBUILD wlstatus
else
    echo "    wlstatus exists, reindexing..."
    cd "$ARCH_DIR" && repo-add wlstatus.db.tar.zst wlstatus-*.pkg.tar.zst
fi

echo ""
echo "==> Done. Arch repo updated at $ARCH_DIR"
echo "    Arch: file://$ARCH_DIR"
