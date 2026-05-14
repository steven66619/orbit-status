#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$REPO_DIR/.." && pwd)"

ARCH="${ARCH:-amd64}"
DIST="${DIST:-stable}"
VERSION="${VERSION:-1.0-1}"

# paths
APT_POOL="$REPO_DIR/apt/pool/main"
APT_DIST="$REPO_DIR/apt/dists/$DIST"
APT_BINARY="$APT_DIST/main/binary-$ARCH"
YUM_DIR="$REPO_DIR/yum/$ARCH"
ARCH_DIR="$REPO_DIR/arch/x86_64"

build_arch_pkg() {
    local pkgbuild="$1"
    local db_name="$2"
    echo "    Building with $pkgbuild..."
    BUILD_DIR=$(mktemp -d)
    cp "$PROJECT_DIR/$pkgbuild" "$BUILD_DIR/PKGBUILD"
    cp "$PROJECT_DIR"/*.c "$PROJECT_DIR"/*.h "$PROJECT_DIR/Makefile" "$BUILD_DIR/" 2>/dev/null || true
    cp "$PROJECT_DIR/"*.xml "$BUILD_DIR/" 2>/dev/null || true
    cp -r "$PROJECT_DIR/dotfiles" "$BUILD_DIR/" 2>/dev/null || true
    cp "$PROJECT_DIR/wlstatus-personal-setup" "$BUILD_DIR/" 2>/dev/null || true
    (cd "$BUILD_DIR" && makepkg -f --noconfirm)
    cp "$BUILD_DIR"/*.pkg.tar.zst "$ARCH_DIR/"
    rm -rf "$BUILD_DIR"
    cd "$ARCH_DIR"
    repo-add "$db_name.db.tar.zst" "$db_name"-*.pkg.tar.zst
}

echo "==> Updating APT repo ($DIST/$ARCH)..."
mkdir -p "$APT_POOL" "$APT_BINARY" "$YUM_DIR" "$ARCH_DIR"

# copy .deb from project root
for deb in "$PROJECT_DIR"/../wlstatus-personal_*.deb "$PROJECT_DIR"/../wlstatus_*.deb; do
    [ -f "$deb" ] && cp "$deb" "$APT_POOL/"
done

dpkg-scanpackages --multiversion "$REPO_DIR/apt/pool/" > "$APT_BINARY/Packages"
gzip -kf "$APT_BINARY/Packages"

apt-ftparchive release "$APT_DIST" > "$APT_DIST/Release"

# sign if GPG key exists
if gpg --list-keys ste@example.com &>/dev/null; then
    gpg --detach-sign --armor -o "$APT_DIST/Release.gpg" "$APT_DIST/Release"
    gpg --clearsign -o "$APT_DIST/InRelease" "$APT_DIST/Release"
fi

echo "==> Updating YUM repo ($ARCH)..."
for spec in wlstatus wlstatus-personal; do
    RPM_SRC=$(ls "/home/ste/rpmbuild/RPMS/$ARCH/$spec"*.rpm 2>/dev/null | head -1)
    if [ -n "$RPM_SRC" ]; then
        cp "$RPM_SRC" "$YUM_DIR/"
    fi
done
createrepo_c --update "$YUM_DIR" 2>/dev/null || echo "    (createrepo_c not available, skipping)"

echo "==> Updating Arch repo..."
if ! ls "$ARCH_DIR"/wlstatus-*.pkg.tar.zst &>/dev/null; then
    echo "    wlstatus not found, building..."
    build_arch_pkg PKGBUILD wlstatus
else
    echo "    wlstatus exists, reindexing..."
    cd "$ARCH_DIR" && repo-add wlstatus.db.tar.zst wlstatus-*.pkg.tar.zst
fi

if ! ls "$ARCH_DIR"/wlstatus-personal-*.pkg.tar.zst &>/dev/null; then
    echo "    wlstatus-personal not found, building..."
    build_arch_pkg PKGBUILD-personal wlstatus-personal
else
    echo "    wlstatus-personal exists, reindexing..."
    cd "$ARCH_DIR" && repo-add wlstatus-personal.db.tar.zst wlstatus-personal-*.pkg.tar.zst
fi

echo ""
echo "==> Done. Repo updated at $REPO_DIR"
echo "    APT:  file://$REPO_DIR/apt"
echo "    YUM:  file://$REPO_DIR/yum"
echo "    Arch: file://$REPO_DIR/arch"
