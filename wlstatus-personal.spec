%global _lto_cflags %{nil}

Name: wlstatus-personal
Version: 1.0
Release: 1%{?dist}
Summary: wlstatus + personal dotfiles (shell, hyprland, waybar, kitty, fish)

License: MIT
URL: https://github.com/steven66619/wlstatus
Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc
BuildRequires: pkgconfig
BuildRequires: pkgconfig(wayland-client)
BuildRequires: pkgconfig(cairo)
BuildRequires: pkgconfig(pangocairo)
BuildRequires: wayland-protocols-devel

%description
A lightweight status bar for Hyprland, Sway, and other wlr-layer-shell
compositors, bundled with personal dotfiles for shell, Hyprland, Waybar,
Kitty, Fish, and other tools.

%prep
%autosetup -n wlstatus-personal

%build
make PREFIX=/usr

%install
make install PREFIX=/usr DESTDIR=%{buildroot}

install -Dm755 wlstatus-personal-setup %{buildroot}%{_bindir}/wlstatus-personal-setup

for f in dotfiles/home/*; do
  install -Dm644 "$f" %{buildroot}%{_datadir}/wlstatus-personal/home/$(basename "$f")
done

for dir in dotfiles/config/*/; do
  name="$(basename "$dir")"
  mkdir -p %{buildroot}%{_datadir}/wlstatus-personal/config/$name
  cp -r "$dir"/* %{buildroot}%{_datadir}/wlstatus-personal/config/$name/
done

%files
%{_bindir}/wlstatus
%{_bindir}/wlstatus-personal-setup
%{_datadir}/wlstatus-personal/

%changelog
* Thu May 14 2026 steven66619 <ste@example.com> - 1.0-1
- Initial personal package
