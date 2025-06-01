Name:           rpm-plugin-filechange
Version:        1.0
Release:        2%{?dist}
Summary:        RPM plugin for tracking file changes during package upgrades and downgrades

License:        GPL-3.0-or-later
URL:            https://github.com/fxzxmic/%{name}
Source:         https://github.com/fxzxmic/%{name}/archive/refs/tags/%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  gcc
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(rpm)

%description
This plugin tracks file changes during RPM package upgrades and downgrades.

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install

%files
%license LICENSE
%doc README.md
%{__plugindir}/*
%{_rpmmacrodir}/*

%changelog
* Sun Jun 01 2025 Fxzxmic <54622331+fxzxmic@users.noreply.github.com> - 1.0-2
- Packaging LICENSE and README

* Fri Apr 25 2025 Fxzxmic <54622331+fxzxmic@users.noreply.github.com> - 1.0-1
- Initial version of the rpm-plugin-filechange
