# TurboJedi, Turbo Journald Interface
# https://github.com/cyanide-burnout/TurboJedi
# Artem Prilutskiy, 2026

# Built in the tree of the project by make rpm, an archive of git would lack the submodule of tvision

Name:           turbojedi
Version:        %{turbojedi_version}
Release:        1%{?dist}
Summary:        Turbo Vision viewer of the systemd journal
License:        MIT
URL:            https://github.com/cyanide-burnout/TurboJedi

BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  cmake
BuildRequires:  pkgconfig
BuildRequires:  systemd-devel
BuildRequires:  ncurses-devel
BuildRequires:  pcre2-devel

%description
Turbo Journald Interface: windows with filters of their own asked of the
journal, following the tail, history, find, highlighting by built-in rules
or rule files of grc, synchronized time across windows and saved layouts.

%build
# Newer rpm builds in a directory of its own, the tree is where make rpm has been called
cd "%{turbojedi_tree}"
%make_build build

%install
cd "%{turbojedi_tree}"
make install PREFIX=%{buildroot}%{_prefix}
install -D -m 644 LICENSE %{buildroot}%{_licensedir}/%{name}/LICENSE
install -D -m 644 Import/tvision/COPYRIGHT %{buildroot}%{_licensedir}/%{name}/tvision-COPYRIGHT
install -D -m 644 README.md %{buildroot}%{_docdir}/%{name}/README.md

%files
%license %{_licensedir}/%{name}/LICENSE
%license %{_licensedir}/%{name}/tvision-COPYRIGHT
%doc %{_docdir}/%{name}/README.md
%{_bindir}/turbojedi
%{_mandir}/man1/turbojedi.1*
