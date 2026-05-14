# Maintainer: steven66619 <ste@example.com>
pkgname=wlstatus
pkgver=1.0
pkgrel=1
pkgdesc="Lightweight Wayland status bar for wlr-layer-shell compositors"
arch=('x86_64' 'aarch64')
url="https://github.com/steven66619/wlstatus"
license=('MIT')
depends=('wayland' 'cairo' 'pango' 'glib2')
makedepends=('wayland-protocols')
source=("$pkgname-$pkgver.tar.gz::https://github.com/steven66619/wlstatus/archive/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
  cd "$srcdir/$pkgname-$pkgver"
  make PREFIX=/usr
}

package() {
  cd "$srcdir/$pkgname-$pkgver"
  make PREFIX=/usr DESTDIR="$pkgdir" install
}
