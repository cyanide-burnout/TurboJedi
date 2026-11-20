# Building TurboJedi

From the sources, and the packages of the distributions. Ready packages: [README](../README.md#installation).

## Dependencies

Debian packages: `build-essential cmake pkg-config git curl libsystemd-dev libncurses-dev libpcre2-dev liblua5.4-dev libcurl4-openssl-dev libsqlite3-dev`,
`libgpm-dev` for the mouse in the Linux console, and `grc` for its rule files if they are wanted. git clones the project
with its submodules and tells the version; curl fetches lsqlite3, which lives in Fossil: Fossil itself is not needed, the
file comes over HTTPS.

```
git submodule update --init
make
sudo make install
```

A clone is updated with its submodule, a plain `git pull` leaves tvision where it was until
`git submodule update --init`:

```
git pull --recurse-submodules
make
```

`make` builds tvision in `Import/tvision/build` first, and again what has changed in it after the submodule has
moved; lua-cjson of `Import/lua-cjson` is built with TurboJedi. A clone that lacks a submodule gets it at the first
`make`. lsqlite3 0.9.6, SQLite for the plugins, is kept in Fossil and not in git: every `make` but `make clean` that does
not find `Import/lsqlite3/lsqlite3.c` fetches it with curl from lua.sqlite.org by the address of its content and checks
its SHA-256, the file is never in the repository. `make prepare` fetches it and the submodules alone, for a tree to build
offline afterwards. `make install` puts the executable to `/usr/local/bin`, the manual page to `/usr/local/share/man/man1` and the
plugins to `/usr/local/share/turbojedi`, `PREFIX` and `DESTDIR` move them. Without libgpm the build goes on, with the mouse in terminal emulators only; the mouse
of the Linux console needs the `gpm` service running.
libcurl serves Check for updates and the requests of plugins, the SQLite of the system the bases of plugins, the
bookmarks first. Lua 5.4 is needed; distributions name its pkg-config module in
their own ways, `lua5.4`, `lua54` or `lua`, any of them of version 5.4 is taken.

About, `--help` and Check for updates link to the repository on GitHub the clone comes from, its `origin`, so a fork links
to itself and checks its own releases. A copy without git or a clone of another host has none, `make REPOSITORY=` leaves
it out on purpose and `make REPOSITORY=https://github.com/owner/name` names another one.

## Debian package

```
sudo apt install debhelper
make deb
```

`make deb` builds `turbojedi_<version>_<arch>.deb` and its `-dbgsym` next to the directory of the project, with
the files in `/usr`. The version comes from `git describe` the way the program shows it: `v0.1-3-gabcdef` becomes
`0.1+3+gabcdef`.

`make deb DISTRIBUTION=amd64` builds for another architecture, with the cross compilers and the libraries of
that architecture in place:

```
sudo dpkg --add-architecture amd64
sudo apt update
sudo apt install crossbuild-essential-amd64 libsystemd-dev:amd64 libncurses-dev:amd64 libpcre2-dev:amd64 libgpm-dev:amd64 libcurl4-openssl-dev:amd64 libsqlite3-dev:amd64 liblua5.4-dev:amd64
```

## RPM package

```
sudo dnf install rpm-build gcc-c++ make cmake git curl systemd-devel ncurses-devel pcre2-devel gpm-devel libcurl-devel sqlite-devel lua-devel
make rpm
```

`make rpm` builds `rpmbuild/RPMS/<arch>/turbojedi-<version>-1.<dist>.<arch>.rpm` for Fedora, RHEL and Amazon Linux,
with the version the same way. RHEL 9 and its rebuilds have `lua-devel` in CRB: `sudo dnf config-manager --set-enabled crb`. openSUSE takes the same
packages from `zypper` with `lua54-devel`, `sqlite3-devel` and `gcc-PIE`, gives no tag of its own
and gets it by `make rpm DIST=tumbleweed` or `DIST=leap16`.

## Arch Linux package

```
sudo pacman -S --needed base-devel cmake git pkgconf gpm curl sqlite lua54
make arch
```

`make arch` builds `arch/turbojedi-<version>-1-<arch>.pkg.tar.zst` with `makepkg`, as a user other than root.

## Packages of a release

A tag `v*` makes GitHub Actions build all the packages and put them to its release. The notes of the release are the
section of [CHANGELOG.md](../CHANGELOG.md) for its version, written before the tag.
