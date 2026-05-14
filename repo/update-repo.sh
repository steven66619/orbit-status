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

echo "==> Updating APT repo ($DIST/$ARCH)..."
mkdir -p "$APT_POOL" "$APT_BINARY" "$YUM_DIR"

# copy .deb from project root
if [ -f "$PROJECT_DIR/../wlstatus_${VERSION}_$ARCH.deb" ]; then
    cp "$PROJECT_DIR/../wlstatus_${VERSION}_$ARCH.deb" "$APT_POOL/"
elif [ -f "$PROJECT_DIR/../wlstatus_${VERSION}_any.deb" ]; then
    cp "$PROJECT_DIR/../wlstatus_${VERSION}_any.deb" "$APT_POOL/"
fi

dpkg-scanpackages --multiversion "$REPO_DIR/apt/pool/" > "$APT_BINARY/Packages"
gzip -kf "$APT_BINARY/Packages"

apt-ftparchive release "$APT_DIST" > "$APT_DIST/Release"

# sign if GPG key exists
if gpg --list-keys ste@example.com &>/dev/null; then
    gpg --detach-sign --armor -o "$APT_DIST/Release.gpg" "$APT_DIST/Release"
    gpg --clearsign -o "$APT_DIST/InRelease" "$APT_DIST/Release"
fi

echo "==> Updating YUM repo ($ARCH)..."
# copy .rpm from rpmbuild
RPM_SRC=$(ls /home/ste/rpmbuild/RPMS/$ARCH/wlstatus-*.rpm 2>/dev/null | head -1)
if [ -n "$RPM_SRC" ]; then
    cp "$RPM_SRC" "$YUM_DIR/"
fi

createrepo_c --update "$YUM_DIR"

echo "==> Done. Repo updated at $REPO_DIR"
echo "    APT:  file://$REPO_DIR/apt"
echo "    YUM:  file://$REPO_DIR/yum"
