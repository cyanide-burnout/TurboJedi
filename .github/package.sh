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
  apt-get install -y -q --no-install-recommends build-essential debhelper cmake pkgconf git ca-certificates libsystemd-dev libncurses-dev libpcre2-dev libgpm-dev
elif command -v pacman > /dev/null
then
  pacman -Syu --noconfirm --needed base-devel cmake git pkgconf systemd-libs ncurses pcre2 gpm
elif command -v zypper > /dev/null
then
  # Tumbleweed images carry tools of busybox in place of those rpm-build wants, they give way
  zypper -n -q install --force-resolution gcc-c++ make cmake pkgconf git rpm-build systemd-devel ncurses-devel pcre2-devel gpm-devel
else
  dnf install -y -q gcc-c++ make cmake pkgconf-pkg-config git rpm-build systemd-devel ncurses-devel pcre2-devel

  # Mouse of the Linux console, EL10 has no GPM and builds without it
  dnf install -y -q gpm-devel || true
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
elif command -v makepkg > /dev/null
then
  # makepkg refuses root, a user of its own builds in the tree it is given
  id builder > /dev/null 2>&1 || useradd -m builder
  chown -R builder .
  su builder -c "git config --global --add safe.directory '*' && make arch"

  # Debug information stays out
  find arch -maxdepth 1 -name 'turbojedi-[0-9]*.pkg.tar.*' -exec cp {} dist/ \;
else
  # openSUSE gives no tag of its distribution, the name of the build tells its packages apart
  make rpm DIST=$(rpm --eval '%{?dist}' | grep -q . || echo "${DISTRO}")

  # The release tag of the distribution tells the packages apart, debug information stays out
  find rpmbuild/RPMS -name 'turbojedi-[0-9]*.rpm' -exec cp {} dist/ \;
fi

ls -l dist
