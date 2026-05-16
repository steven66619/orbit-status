#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
ARCH="${ARCH:-amd64}"
DIST="${DIST:-stable}"

APT_POOL="$REPO_DIR/apt/pool/main"
APT_DIST="$REPO_DIR/apt/dists/$DIST"
APT_BINARY="$APT_DIST/main/binary-$ARCH"
YUM_DIR="$REPO_DIR/yum/$ARCH"
ARCH_DIR="$REPO_DIR/arch/x86_64"

install_deps_ci() {
    echo "==> Installing CI dependencies..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq dpkg-dev apt-utils createrepo-c
}

generate_apt() {
    echo "==> Generating APT repo ($DIST/$ARCH)..."
    mkdir -p "$APT_POOL" "$APT_BINARY"
    if [ -z "$(ls "$APT_POOL"/*.deb 2>/dev/null)" ]; then
        echo "    No .deb packages found, skipping APT"
        return
    fi
    dpkg-scanpackages --multiversion "$REPO_DIR/apt/pool/" 2>/dev/null > "$APT_BINARY/Packages"
    gzip -kf "$APT_BINARY/Packages"
    apt-ftparchive release "$APT_DIST" > "$APT_DIST/Release"
    if gpg --list-keys ste@example.com &>/dev/null 2>&1; then
        gpg --detach-sign --armor -o "$APT_DIST/Release.gpg" "$APT_DIST/Release"
        gpg --clearsign -o "$APT_DIST/InRelease" "$APT_DIST/Release"
    fi
    echo "    APT repo ready"
}

generate_yum() {
    echo "==> Generating YUM repo ($ARCH)..."
    mkdir -p "$YUM_DIR"
    if [ -z "$(ls "$YUM_DIR"/*.rpm 2>/dev/null)" ]; then
        echo "    No .rpm packages found, skipping YUM"
        return
    fi
    if command -v createrepo_c &>/dev/null; then
        createrepo_c --update "$YUM_DIR" --no-database 2>/dev/null || \
        createrepo_c --update "$YUM_DIR" 2>/dev/null || \
        echo "    createrepo_c failed, skipping YUM metadata"
    else
        echo "    createrepo_c not available, skipping YUM metadata"
    fi
}

generate_arch() {
    echo "==> Generating Arch repo..."
    mkdir -p "$ARCH_DIR"
    local arch_arch="x86_64"
    pushd "$ARCH_DIR" >/dev/null
    for db in wlstatus wlstatus-personal; do
        local pattern
        case "$db" in
            wlstatus) pattern="wlstatus-*-${arch_arch}.pkg.tar.zst" ;;
            wlstatus-personal) pattern="wlstatus-personal-*-${arch_arch}.pkg.tar.zst" ;;
        esac
        if [ -z "$(ls $pattern 2>/dev/null)" ]; then
            echo "    No $db packages ($pattern) found, skipping"
            continue
        fi
        repo-add "$db.db.tar.zst" $pattern 2>/dev/null
    done
    popd >/dev/null
}

case "${1:-}" in
    ci-deps) install_deps_ci ;;
    apt) generate_apt ;;
    yum) generate_yum ;;
    arch) generate_arch ;;
    all|"")
        generate_apt
        generate_yum
        generate_arch
        echo ""
        echo "==> Done. Repo updated at $REPO_DIR"
        ;;
    *)
        echo "Usage: $0 [all|apt|yum|arch|ci-deps]"
        exit 1
        ;;
esac
