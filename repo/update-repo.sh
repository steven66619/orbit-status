#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$REPO_DIR/.." && pwd)"

ARCH="${ARCH:-amd64}"
DIST="${DIST:-stable}"
VERSION="${VERSION:-1.0-1}"

APT_POOL="$REPO_DIR/apt/pool/main"
APT_DIST="$REPO_DIR/apt/dists/$DIST"
APT_BINARY="$APT_DIST/main/binary-$ARCH"
YUM_DIR="$REPO_DIR/yum/$ARCH"
ARCH_DIR="$REPO_DIR/arch/x86_64"

build_arch_pkg() {
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

echo "==> Updating APT repo ($DIST/$ARCH)..."
mkdir -p "$APT_POOL" "$APT_BINARY" "$YUM_DIR" "$ARCH_DIR"

for deb in "$PROJECT_DIR"/../wlstatus_*.deb; do
    [ -f "$deb" ] && cp "$deb" "$APT_POOL/"
done

if command -v dpkg-scanpackages &>/dev/null; then
    dpkg-scanpackages --multiversion "$REPO_DIR/apt/pool/" > "$APT_BINARY/Packages"
    gzip -kf "$APT_BINARY/Packages"
fi

if command -v apt-ftparchive &>/dev/null; then
    apt-ftparchive release "$APT_DIST" > "$APT_DIST/Release"
    if gpg --list-keys wlstatus &>/dev/null; then
        gpg --detach-sign --armor -o "$APT_DIST/Release.gpg" "$APT_DIST/Release"
        gpg --clearsign -o "$APT_DIST/InRelease" "$APT_DIST/Release"
    fi
fi

echo "==> Updating YUM repo ($ARCH)..."
if command -v createrepo_c &>/dev/null; then
    createrepo_c --update "$YUM_DIR" 2>/dev/null || echo "    (createrepo_c update skipped)"
fi

echo "==> Updating Arch repo..."
if ! ls "$ARCH_DIR"/wlstatus-*.pkg.tar.zst &>/dev/null; then
    echo "    wlstatus not found, building..."
    build_arch_pkg PKGBUILD wlstatus
else
    echo "    wlstatus exists, reindexing..."
    cd "$ARCH_DIR" && repo-add wlstatus.db.tar.zst wlstatus-*.pkg.tar.zst
fi

echo ""
echo "==> Done. Repo updated at $REPO_DIR"
echo "    APT:  file://$REPO_DIR/apt"
echo "    YUM:  file://$REPO_DIR/yum"
echo "    Arch: file://$REPO_DIR/arch"

echo ""
echo "==> To deploy to GitHub Pages:"
echo "    git checkout gh-pages"
echo "    git rm -r ."
echo "    cp -r $REPO_DIR/* ."
echo "    git add -A && git commit -m 'deploy: \$(git rev-parse HEAD)'"
echo "    git push origin gh-pages"
