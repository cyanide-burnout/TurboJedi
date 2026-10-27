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

# Turbo Vision of magiblot is linked in statically from the submodule
Provides:       bundled(tvision) = %{turbojedi_tvision}

BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  cmake
BuildRequires:  pkgconfig
BuildRequires:  systemd-devel
BuildRequires:  ncurses-devel
BuildRequires:  pcre2-devel

# Mouse of the Linux console, EL10 has no GPM
%if 0%{?rhel} < 10
BuildRequires:  gpm-devel
%endif

# Position independent executable, the build service of openSUSE has it in every build
%if 0%{?suse_version}
BuildRequires:  gcc-PIE
%endif

%description
Turbo Journald Interface: windows with filters of their own asked of the
journal, following the tail, history, find, highlighting by built-in rules
or rule files of grc, synchronized time across windows and saved layouts.

# Debug information goes to a package of its own and the executable is stripped, Fedora and RHEL do it by themselves,
# openSUSE leaves it to its build service
%if 0%{?suse_version}
%debug_package
%endif

%prep
# What %%setup would unpack: %%license and %%doc take the names from the build directory
cp -p "%{turbojedi_tree}/LICENSE" "%{turbojedi_tree}/README.md" .
cp -p "%{turbojedi_tree}/Import/tvision/COPYRIGHT" tvision-COPYRIGHT

%build
# Newer rpm builds in a directory of its own, the tree is where make rpm has been called
cd "%{turbojedi_tree}"

# Hardening flags of the distribution, rpm of EL9 and openSUSE does not set them by itself as Fedora does
%{?set_build_flags}
%make_build build

%install
cd "%{turbojedi_tree}"
make install PREFIX=%{buildroot}%{_prefix}

%files
%license LICENSE tvision-COPYRIGHT
%doc README.md
%{_bindir}/turbojedi
%{_mandir}/man1/turbojedi.1*
