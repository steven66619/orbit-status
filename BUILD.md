# Building wlstatus packages on any distro

## Arch Linux (native)

sudo pacman -S wayland wayland-protocols cairo pango
make
sudo make install

or with PKGBUILD:
makepkg -si

## Debian/Ubuntu package (from any distro via container)

podman run --rm -v $PWD:/build -w /build debian:bookworm \
  sh -c "apt update && apt build-dep -y . && dpkg-buildpackage -us -uc -b"

## Fedora/RHEL RPM (from any distro via container)

podman run --rm -v $PWD:/build -w /build fedora:latest \
  sh -c "dnf install -y rpm-build && rpmbuild -ba wlstatus.spec"

## Debian package on Arch (native tools)

sudo pacman -S dpkg
tar czf ../wlstatus_1.0.tar.gz --transform 's/^wlstatus/wlstatus-1.0/' *
dpkg-source -b .
dpkg-buildpackage -us -uc -b

## Fedora RPM on Arch (native tools)

sudo pacman -S rpm-tools
mkdir -p ~/rpmbuild/{SOURCES,SPECS}
git archive --format=tar.gz -o ~/rpmbuild/SOURCES/wlstatus-1.0.tar.gz HEAD
cp wlstatus.spec ~/rpmbuild/SPECS/
rpmbuild -ba ~/rpmbuild/SPECS/wlstatus.spec
