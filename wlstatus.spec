%global _lto_cflags %{nil}

Name: wlstatus
Version: 1.2
Release: 1%{?dist}
Summary: Lightweight Wayland status bar with Lua plugin support

License: MIT
URL: https://github.com/steven66619/wlstatus-new
Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc
BuildRequires: pkgconfig
BuildRequires: pkgconfig(wayland-client)
BuildRequires: pkgconfig(cairo)
BuildRequires: pkgconfig(pangocairo)
BuildRequires: pkgconfig(lua5.4)
BuildRequires: wayland-protocols-devel

%description
A lightweight status bar for Hyprland, Sway, and other wlr-layer-shell
compositors. Features Lua plugins for CPU, memory, disk, volume,
network, battery, and updates, plus workspace switching and power
options with confirmation popup.

%prep
%autosetup

%build
make PREFIX=/usr

%install
make install PREFIX=/usr DESTDIR=%{buildroot}

%files
%{_bindir}/wlstatus
%{_bindir}/wlstatus-update
%{_datadir}/wlstatus/plugins/*.lua

%changelog
* Tue May 19 2026 steven66619 <ste@example.com> - 1.2-1
- Hyprland IPC, window tracking, inotify plugin watches, active window title

* Sun May 17 2026 steven66619 <ste@example.com> - 1.1-2
- Add weather Lua plugin

* Sun May 17 2026 steven66619 <ste@example.com> - 1.1-1
- Lua plugin architecture — all modules are now Lua scripts
- New config keys for Lua plugins
- Bundled plugins for CPU, memory, disk, volume, network, battery, updates

* Sat May 16 2026 steven66619 <ste@example.com> - 1.0-2
- Remove application launcher

* Thu May 14 2026 steven66619 <ste@example.com> - 1.0-1
- Initial package
