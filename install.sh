#!/bin/sh

# TurboJedi, Turbo Journald Interface
# https://github.com/cyanide-burnout/TurboJedi
# Artem Prilutskiy, 2026

# Installs the package of the latest release that fits the distribution and the architecture of this machine:
#
#   curl -fsSL https://raw.githubusercontent.com/cyanide-burnout/TurboJedi/master/install.sh | sh
#
# TURBOJEDI_VERSION=0.6 takes that release instead of the latest one. The package manager of the distribution
# brings the dependencies, sudo or doas is asked for when this does not run as root.

set -eu

REPOSITORY=cyanide-burnout/TurboJedi

Fail()
{
  echo "turbojedi: $*" >&2
  exit 1
}

Contains()
{
  case " $1 " in
    *" $2 "*) return 0 ;;
  esac
  return 1
}

# Name of the distribution in the names of the packages: debian-13, ubuntu-24.04, fc44, el9 or arch

GetTarget()
{
  ID=
  ID_LIKE=
  VERSION_ID=
  UBUNTU_CODENAME=

  [ -r /etc/os-release ] || Fail "/etc/os-release is missing, the distribution is unknown"
  . /etc/os-release

  case "$ID" in
    debian|raspbian)
      # Raspberry Pi OS is debian on arm64 and raspbian on armhf
      echo "debian-${VERSION_ID%%.*}"
      return ;;
    ubuntu)
      echo "ubuntu-$VERSION_ID"
      return ;;
    fedora)
      echo "fc$VERSION_ID"
      return ;;
    rhel|almalinux|rocky|centos|ol|eurolinux|circle|navy)
      echo "el${VERSION_ID%%.*}"
      return ;;
    arch)
      echo "arch"
      return ;;
  esac

  # Derivatives tell what they are made of
  if Contains "$ID_LIKE" ubuntu
  then
    case "$UBUNTU_CODENAME" in
      noble)    echo "ubuntu-24.04" ;;
      resolute) echo "ubuntu-26.04" ;;
      *)        echo "ubuntu-$UBUNTU_CODENAME" ;;
    esac
  elif Contains "$ID_LIKE" debian
  then
    # 13.1, or trixie/sid on testing
    VERSION_ID=$(cat /etc/debian_version 2> /dev/null || true)
    echo "debian-${VERSION_ID%%[./]*}"
  elif Contains "$ID_LIKE" rhel || Contains "$ID_LIKE" centos
  then
    echo "el${VERSION_ID%%.*}"
  elif Contains "$ID_LIKE" fedora
  then
    echo "fc$VERSION_ID"
  elif Contains "$ID_LIKE" arch
  then
    echo "arch"
  else
    echo "$ID-$VERSION_ID"
  fi
}

Main()
{
  TARGET=$(GetTarget)

  # Userland decides, not the kernel: Raspberry Pi OS of 32 bits runs a kernel of 64 bits on newer boards
  case "$TARGET" in
    debian-*|ubuntu-*)
      KIND=deb
      ARCH=$(dpkg --print-architecture)
      PATTERN="_${TARGET}_${ARCH}\.deb" ;;
    fc*|el*)
      KIND=rpm
      ARCH=$(rpm --eval '%{_arch}')
      PATTERN="\.${TARGET}\.${ARCH}\.rpm" ;;
    arch)
      KIND=pkg
      ARCH=$(uname -m)
      PATTERN="-${ARCH}\.pkg\.tar\.zst" ;;
    *)
      Fail "no packages for $TARGET, TurboJedi builds from the sources: https://github.com/$REPOSITORY#building" ;;
  esac

  if [ -n "${TURBOJEDI_VERSION:-}" ]
  then
    RELEASE="tags/v${TURBOJEDI_VERSION#v}"
  else
    RELEASE=latest
  fi

  JSON=$(curl -fsSL "https://api.github.com/repos/$REPOSITORY/releases/$RELEASE") ||
    Fail "cannot read the release $RELEASE of $REPOSITORY"

  # Links of the files of the release, JSON of the API is read without jq
  LINKS=$(echo "$JSON" | sed -n 's/.*"browser_download_url": *"\([^"]*\)".*/\1/p')

  LINK=$(echo "$LINKS" | grep -e "${PATTERN}\$" | head -n 1 || true)

  if [ -z "$LINK" ]
  then
    echo "turbojedi: no package for $TARGET on $ARCH in the release, there are:" >&2
    echo "$LINKS" | sed 's|.*/|  |' >&2

    if [ "$ARCH" = armhf ]
    then
      echo "Raspberry Pi OS of 64 bits has packages, on 32 bits TurboJedi builds from the sources:" >&2
    else
      echo "TurboJedi builds from the sources:" >&2
    fi

    Fail "https://github.com/$REPOSITORY#building"
  fi

  SUDO=

  if [ "$(id -u)" -ne 0 ]
  then
    if command -v sudo > /dev/null
    then
      SUDO=sudo
    elif command -v doas > /dev/null
    then
      SUDO=doas
    else
      Fail "run as root, there is neither sudo nor doas"
    fi
  fi

  DIRECTORY=$(mktemp -d)
  trap 'rm -rf "$DIRECTORY"' EXIT
  trap 'exit 1' INT TERM

  FILE="$DIRECTORY/${LINK##*/}"

  echo "Downloading ${LINK##*/}"
  curl -fL --progress-bar -o "$FILE" "$LINK" || Fail "cannot download $LINK"

  # apt reads the package as _apt
  chmod 755 "$DIRECTORY"
  chmod 644 "$FILE"

  # Nothing asks, the standard input may be the rest of this script
  case "$KIND" in
    deb)
      $SUDO env DEBIAN_FRONTEND=noninteractive apt-get update -q < /dev/null
      $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -q "$FILE" < /dev/null ;;
    rpm)
      $SUDO dnf install -y "$FILE" < /dev/null ;;
    pkg)
      $SUDO pacman -U --noconfirm --needed "$FILE" < /dev/null ;;
  esac

  echo "TurboJedi is installed: $(command -v turbojedi)"
}

# Read to the end before running, a shell reading a pipe would run a part of it otherwise
Main "$@"
