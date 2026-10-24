#!/bin/sh

# TurboJedi, Turbo Journald Interface
# https://github.com/cyanide-burnout/TurboJedi
# Artem Prilutskiy, 2026

# Package of the distribution this runs on, as root in its container from the root of a clone with tags and
# the submodule: installs what the build needs, builds, and leaves the package in dist/ named after the distribution

set -e

DISTRO=$1

if command -v apt-get > /dev/null
then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -q
  apt-get install -y -q --no-install-recommends build-essential debhelper cmake pkgconf git ca-certificates libsystemd-dev libncurses-dev libpcre2-dev
else
  dnf install -y -q gcc-c++ make cmake pkgconf-pkg-config git rpm-build systemd-devel ncurses-devel pcre2-devel
fi

# The clone belongs to the user of the runner, git would not tell the version otherwise
git config --global --add safe.directory '*'

rm -rf dist
mkdir -p dist

if command -v dpkg-buildpackage > /dev/null
then
  make deb

  for FILE in ../turbojedi_*.deb
  do
    # Debian and Ubuntu give the same name to their packages of one version
    NAME=$(basename "${FILE}" | sed "s/_\([^_]*\)\.deb\$/_${DISTRO}_\1.deb/")
    mv "${FILE}" "dist/${NAME}"
  done

  rm -f ../turbojedi_* ../turbojedi-dbgsym_*
else
  make rpm

  # The release tag of the distribution tells the packages apart, debug information stays out
  find rpmbuild/RPMS -name 'turbojedi-[0-9]*.rpm' -exec cp {} dist/ \;
fi

ls -l dist
