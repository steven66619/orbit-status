# Maintainer: steven66619 <ste@example.com>
pkgname=orbit-status
pkgver=1.3
pkgrel=1
pkgdesc="Lightweight Wayland status bar with Lua plugin support"
arch=('x86_64' 'aarch64')
url="https://github.com/steven66619/orbit-status"
license=('MIT')
depends=('wayland' 'cairo' 'pango' 'glib2' 'lua54' 'librsvg' 'dbus' 'libpulse')
makedepends=('wayland-protocols' 'lua')
source=("$pkgname-$pkgver.tar.gz::https://github.com/steven66619/orbit-status/archive/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
  cd "$srcdir/$pkgname-$pkgver"
  make PREFIX=/usr
}

package() {
  cd "$srcdir/$pkgname-$pkgver"
  make PREFIX=/usr DESTDIR="$pkgdir" install
}
