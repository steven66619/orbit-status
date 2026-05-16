%global _lto_cflags %{nil}

Name: wlstatus
Version: 1.0
Release: 2%{?dist}
Summary: Lightweight Wayland status bar for wlr-layer-shell compositors

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
compositors. Features workspace switching, system info (CPU, memory, disk,
updates), power options with confirmation popup, and date/time display.

%prep
%autosetup

%build
make PREFIX=/usr

%install
make install PREFIX=/usr DESTDIR=%{buildroot}

%files
%{_bindir}/wlstatus
%{_bindir}/wlstatus-update

%changelog
* Sat May 16 2026 steven66619 <ste@example.com> - 1.0-2
- Remove application launcher

* Thu May 14 2026 steven66619 <ste@example.com> - 1.0-1
- Initial package
