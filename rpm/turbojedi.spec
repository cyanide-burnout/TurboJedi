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

# Turbo Vision of magiblot and lua-cjson of OpenResty are linked in statically from the submodules
Provides:       bundled(tvision) = %{turbojedi_tvision}
Provides:       bundled(lua-cjson) = 2.1.0.20

# turbojedi-mcp.py gives the D-Bus interface to AI agents, TurboJedi itself needs no Python: the interpreter its line #!
# names is not required
%global __requires_exclude ^/usr/bin/python3$
Recommends:     python3
%if 0%{?suse_version}
Recommends:     python3-dbus-python
%else
Recommends:     python3-dbus
%endif

BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  cmake
BuildRequires:  pkgconfig
BuildRequires:  systemd-devel
BuildRequires:  ncurses-devel
BuildRequires:  pcre2-devel
BuildRequires:  libcurl-devel

# Plugins of the Actions menu, Lua 5.4 under its own name in openSUSE
%if 0%{?suse_version}
BuildRequires:  lua54-devel
%else
BuildRequires:  lua-devel
%endif

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
cp -p "%{turbojedi_tree}/LICENSE" "%{turbojedi_tree}/README.md" "%{turbojedi_tree}/CHANGELOG.md" .
cp -pr "%{turbojedi_tree}/Documentation" .
# The screenshot is for README on GitHub, a package has no use for it
rm -f Documentation/screenshot.png
cp -p "%{turbojedi_tree}/Import/tvision/COPYRIGHT" tvision-COPYRIGHT
cp -p "%{turbojedi_tree}/Import/lua-cjson/LICENSE" lua-cjson-LICENSE

%build
# Newer rpm builds in a directory of its own, the tree is where make rpm has been called
cd "%{turbojedi_tree}"

# Hardening flags of the distribution, rpm of EL9 and openSUSE does not set them by itself as Fedora does
%{?set_build_flags}

# The tag of the distribution in the release is what TurboJedi tells the system by, fc44 or leap16
%make_build build TURBOJEDI_TARGET="$(echo '%{?dist}' | sed 's/^\.//')"

%install
cd "%{turbojedi_tree}"
make install PREFIX=%{buildroot}%{_prefix}

%files
%license LICENSE tvision-COPYRIGHT lua-cjson-LICENSE
%doc README.md CHANGELOG.md Documentation
%{_bindir}/turbojedi
%{_bindir}/turbojedi-mcp.py
%{_datadir}/dbus-1/system.d/me.burnaway.TurboJedi.conf
%{_mandir}/man1/turbojedi.1*
%{_datadir}/turbojedi
