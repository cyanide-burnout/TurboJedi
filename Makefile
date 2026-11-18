EXECUTABLE  := turbojedi
SOURCE      := Source
TURBOVISION := Import/tvision
LUACJSON    := Import/lua-cjson
PREFIX      := $(DESTDIR)/usr/local

# Components of the sources, each in a folder of its own: a header is included by its name alone from any of them
DIRECTORIES := Main Journal Timeline Dialog Script Service

# A clone made without --recursive, or pulled since a submodule has come, takes the submodules at the first make that
# builds, clean takes none; an archive without git has to bring them
ifeq ($(and $(wildcard $(TURBOVISION)/CMakeLists.txt),$(wildcard $(LUACJSON)/lua_cjson.c)),)
ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
ifeq ($(wildcard .git),)
$(error $(TURBOVISION) and $(LUACJSON) are needed: a clone made with --recursive has them, an archive of GitHub has not)
endif
ifneq ($(shell git submodule update --init >&2 && echo done),done)
$(error git submodule update --init has failed: $(TURBOVISION) and $(LUACJSON) are needed to build)
endif
endif
endif

# A clone tells by git, an archive of git has VERSION filled in, anything else is unknown
VERSION := $(if $(wildcard .git),$(shell git describe --tags --always --dirty 2>/dev/null))
VERSION := $(or $(VERSION),$(shell grep -v '^\$$Format' VERSION 2>/dev/null),unknown)

# Address of the repository of the project, told by the origin of the clone on GitHub, the only host whose releases and files
# Check for updates knows: About, --help and the check link there, so a fork links to itself. A copy without git or a clone
# of another host has none, nor the links and the check; REPOSITORY= leaves it out on purpose, REPOSITORY=<address> names another one
ifeq ($(origin REPOSITORY), undefined)
	REPOSITORY := $(if $(wildcard .git),$(shell git remote get-url origin 2>/dev/null | sed -n -E -e 's,/$$,,' -e 's,\.git$$,,' -e 's,^(https://([^@/]+@)?|ssh://git@|git@)github\.com[:/]([^/]+/[^/]+)$$,https://github.com/\3,p'))
endif

# Distribution a package is built for, the way the names of the packages and install.sh tell it: debian-13, ubuntu-24.04,
# fc44, el9, amzn2023, tumbleweed, leap16 or arch; a build of its own has none
TARGET := $(TURBOJEDI_TARGET)

# Versions of Debian start with a digit and keep - for the revision: v0.1-3-gabcdef becomes 0.1+3+gabcdef
PACKAGE := turbojedi
RELEASE := $(shell echo '$(VERSION)' | sed -e 's/^v//' -e 's/-/+/g' -e 's/^unknown$$/0~unknown/')
AUTHOR  := Artem Prilutskiy <cyanide.burnout@gmail.com>

# tvision has no releases, a snapshot is told by the date and the commit of the submodule the way Fedora writes it
BUNDLED := $(or $(shell git -C $(TURBOVISION) log -1 --format='0^%cdgit%h' --date=format:%Y%m%d 2>/dev/null),0)

# dpkg-buildpackage -a gives the cross compilers and pkg-config, tvision has to be built with them as well
PKG_CONFIG ?= pkg-config
CMAKEFLAGS := -D CMAKE_C_COMPILER=$(CC) -D CMAKE_CXX_COMPILER=$(CXX)

ifneq ($(DEB_HOST_GNU_TYPE), $(DEB_BUILD_GNU_TYPE))
	CMAKEFLAGS += -D CMAKE_SYSTEM_NAME=Linux -D CMAKE_SYSTEM_PROCESSOR=$(DEB_HOST_GNU_CPU) -D CMAKE_LIBRARY_ARCHITECTURE=$(DEB_HOST_MULTIARCH)
endif

# Lua 5.4 runs the plugins of the Actions menu, distributions name its pkg-config module in their own ways
LUA := $(firstword $(foreach module, lua5.4 lua-5.4 lua54 lua, $(shell $(PKG_CONFIG) --exists '$(module) >= 5.4' '$(module) < 5.5' && echo $(module))))

ifeq ($(filter clean,$(MAKECMDGOALS)), )
ifeq ($(LUA), )
$(error Lua 5.4 is needed: liblua5.4-dev, lua-devel, lua54-devel or lua54 of the distribution)
endif
ifneq ($(shell $(PKG_CONFIG) --exists libcurl && echo yes), yes)
$(error libcurl is needed: libcurl4-openssl-dev, libcurl-devel or curl of the distribution)
endif
endif

LIBRARIES := \
	ncursesw

DEPENDENCIES := \
	libsystemd \
	libpcre2-8 \
	libcurl \
	$(LUA)

OBJECTS := \
	$(SOURCE)/Main/TurboJedi.o \
	$(SOURCE)/Main/JediApplication.o \
	$(SOURCE)/Main/JediStatusLine.o \
	$(SOURCE)/Main/JediClock.o \
	$(SOURCE)/Main/JediClipboard.o \
	$(SOURCE)/Journal/JournalView.o \
	$(SOURCE)/Journal/JournalSource.o \
	$(SOURCE)/Journal/JournalWindow.o \
	$(SOURCE)/Journal/JournalHeader.o \
	$(SOURCE)/Timeline/TimelineView.o \
	$(SOURCE)/Timeline/TimelineWindow.o \
	$(SOURCE)/Journal/JournalTools.o \
	$(SOURCE)/Journal/JournalLayout.o \
	$(SOURCE)/Main/JediSettings.o \
	$(SOURCE)/Main/JediShadow.o \
	$(SOURCE)/Dialog/SourceDialog.o \
	$(SOURCE)/Dialog/PickDialog.o \
	$(SOURCE)/Dialog/WindowsDialog.o \
	$(SOURCE)/Dialog/TimeDialog.o \
	$(SOURCE)/Dialog/TimePickDialog.o \
	$(SOURCE)/Dialog/CalendarView.o \
	$(SOURCE)/Dialog/PageView.o \
	$(SOURCE)/Dialog/PageDialog.o \
	$(SOURCE)/Journal/JournalExpression.o \
	$(SOURCE)/Journal/JournalHighlighter.o \
	$(SOURCE)/Journal/JournalTinter.o \
	$(SOURCE)/Journal/JournalMatcher.o \
	$(SOURCE)/Dialog/TextDialog.o \
	$(SOURCE)/Dialog/LevelsDialog.o \
	$(SOURCE)/Dialog/ColumnsDialog.o \
	$(SOURCE)/Dialog/JournalsDialog.o \
	$(SOURCE)/Dialog/DirectoryDialog.o \
	$(SOURCE)/Script/JediScript.o \
	$(SOURCE)/Script/ScriptDBus.o \
	$(SOURCE)/Script/ScriptWatch.o \
	$(SOURCE)/Script/ScriptEvent.o \
	$(SOURCE)/Script/ScriptCommand.o \
	$(SOURCE)/Script/ScriptWindow.o \
	$(SOURCE)/Script/JediProcess.o \
	$(SOURCE)/Script/JediFetch.o \
	$(SOURCE)/Script/JediDBus.o \
	$(SOURCE)/Journal/JournalQuery.o \
	$(SOURCE)/Journal/JournalWatch.o \
	$(SOURCE)/Script/WatchWindow.o \
	$(SOURCE)/Script/ScriptNotation.o \
	$(SOURCE)/Service/JediService.o \
	$(SOURCE)/Script/ActionDialogs.o

# The repository lets the Help menu check for a newer release, without it the item is left out
ifneq ($(REPOSITORY), )
	OBJECTS += $(SOURCE)/Main/JediUpdate.o $(SOURCE)/Main/UpdateDialog.o
	FLAGS   += -DUSE_UPDATE
endif

# JSON for the plugins, require('cjson') of every one of them whatever the distribution has: lua-cjson of OpenResty,
# built with TurboJedi for its Lua
OBJECTS += $(LUACJSON)/lua_cjson.o $(LUACJSON)/strbuf.o $(LUACJSON)/fpconv.o

# The linker takes from an archive only what the objects in front of it need, it goes after all of them
OBJECTS += $(TURBOVISION)/build/libtvision.a

FLAGS += \
	-Wno-unused-result -O2 -flto=auto -MMD -MP -gdwarf \
	-I$(TURBOVISION)/include \
	$(foreach directory, $(DIRECTORIES), -I$(SOURCE)/$(directory)) \
	$(shell $(PKG_CONFIG) --cflags $(DEPENDENCIES))

LIBS := \
	$(foreach library, $(LIBRARIES), -l$(library)) \
	$(shell $(PKG_CONFIG) --libs $(DEPENDENCIES))

CXXFLAGS += $(FLAGS) -std=gnu++17
CFLAGS   += $(FLAGS)

# Libraries tvision has found on its own, told once it is configured: libgpm for the mouse of the Linux console
EXTRA = $(if $(shell grep -s '^GPM:FILEPATH=/' $(TURBOVISION)/build/CMakeCache.txt),-lgpm)

all: build

# Configured once, built by its own make every time: it knows what a move of the submodule has changed and does nothing
# otherwise
$(TURBOVISION)/build/libtvision.a: FORCE
	@[ -f $(TURBOVISION)/build/CMakeCache.txt ] || \
	 cmake -S $(TURBOVISION) -B $(TURBOVISION)/build $(CMAKEFLAGS) -D CMAKE_BUILD_TYPE=Release -D CMAKE_POSITION_INDEPENDENT_CODE=ON -D TV_BUILD_EXAMPLES=OFF -D TV_BUILD_USING_GPM=ON
	+make -C $(TURBOVISION)/build tvision

# Rewritten only when the version changes, so that only its users are built again
$(SOURCE)/Main/Version.h: FORCE
	@printf '#define VERSION "%s"\n#define BUILT_FOR "%s"\n#define REPOSITORY "%s"\n' '$(VERSION)' '$(TARGET)' '$(REPOSITORY)' | cmp -s - $@ || \
	 printf '#define VERSION "%s"\n#define BUILT_FOR "%s"\n#define REPOSITORY "%s"\n' '$(VERSION)' '$(TARGET)' '$(REPOSITORY)' > $@

$(SOURCE)/Main/TurboJedi.o $(SOURCE)/Main/JediApplication.o $(SOURCE)/Main/JediUpdate.o $(SOURCE)/Script/JediFetch.o $(SOURCE)/Script/JediScript.o $(SOURCE)/Script/ScriptDBus.o $(SOURCE)/Script/ScriptWatch.o $(SOURCE)/Script/ScriptEvent.o $(SOURCE)/Script/ScriptCommand.o $(SOURCE)/Script/ScriptWindow.o $(SOURCE)/Service/JediService.o: $(SOURCE)/Main/Version.h

# Internal headers of Turbo Vision include each other from there, only the event source needs them
$(SOURCE)/Journal/JournalSource.o: CXXFLAGS += -I$(TURBOVISION)/include/tvision

build: $(OBJECTS)
	$(CXX) $(OBJECTS) $(FLAGS) $(LDFLAGS) $(LIBS) $(EXTRA) -o $(EXECUTABLE)

# The policy of the bus goes to /usr/share of a package, dbus does not read /usr/local/share: an installation of its own
# puts it to /etc. POLICY= names another directory
POLICY ?= $(if $(filter %/usr,$(PREFIX)),$(PREFIX)/share/dbus-1/system.d,$(DESTDIR)/etc/dbus-1/system.d)

# dbus-daemon before 1.13.12 knows no send_destination_prefix and refuses the whole file: the one of compat goes in its
# place where the dbus-daemon of the build is older. dbus-broker knows it, a machine without dbus-daemon takes the full
# one. POLICY_FILE= names one
DBUS_VERSION := $(shell dbus-daemon --version 2> /dev/null | sed -n '1s/.* \([0-9][0-9.]*\)$$/\1/p')
POLICY_FILE  ?= $(if $(and $(DBUS_VERSION),$(shell printf '%s\n1.13.11\n' '$(DBUS_VERSION)' | sort -V -C && echo old)),me.burnaway.TurboJedi.compat.conf,me.burnaway.TurboJedi.conf)

install:
	install -D -m 755 $(EXECUTABLE) $(PREFIX)/bin/$(EXECUTABLE)
	install -D -m 644 $(EXECUTABLE).1 $(PREFIX)/share/man/man1/$(EXECUTABLE).1
	install -D -m 644 -t $(PREFIX)/share/$(EXECUTABLE)/plugins Plugins/*.lua
	install -D -m 644 -t $(PREFIX)/share/$(EXECUTABLE)/examples Plugins/Examples/*.lua
	install -D -m 644 $(POLICY_FILE) $(POLICY)/me.burnaway.TurboJedi.conf
	sed "s|^VERSION = 'unknown'|VERSION = '$(VERSION)'|" $(EXECUTABLE)-mcp.py > $(EXECUTABLE)-mcp.py.installed
	install -D -m 755 $(EXECUTABLE)-mcp.py.installed $(PREFIX)/bin/$(EXECUTABLE)-mcp.py
	rm -f $(EXECUTABLE)-mcp.py.installed

# The package goes next to the directory of the project, as dpkg-buildpackage puts it,
# DISTRIBUTION names another architecture of Debian to build for, such as amd64
deb:
	sh debian/changelog.sh '$(PACKAGE)' '$(RELEASE)' '$(AUTHOR)' > debian/changelog
ifdef DISTRIBUTION
	TURBOJEDI_TARGET='$(TARGET)' dpkg-buildpackage -b -a$(DISTRIBUTION) -tc -us -uc -j4
else
	TURBOJEDI_TARGET='$(TARGET)' dpkg-buildpackage -b -tc -us -uc -j4
endif
	rm -f debian/changelog

# Packages of Fedora, RHEL and openSUSE go to rpmbuild/RPMS, DIST names a distribution that gives no tag of its own, such as leap16
rpm:
	rpmbuild -bb --define '_topdir $(CURDIR)/rpmbuild' --define 'source_date_epoch_from_changelog 0' --define 'turbojedi_tree $(CURDIR)' --define 'turbojedi_version $(RELEASE)' --define 'turbojedi_tvision $(BUNDLED)' $(if $(DIST),--define 'dist .$(DIST)') rpm/turbojedi.spec

# Package of Arch Linux goes next to arch/PKGBUILD, makepkg does not run as root
arch:
	cd arch && TURBOJEDI_VERSION='$(RELEASE)' makepkg -f

clean:
	rm -f $(wildcard $(filter %.o,$(OBJECTS))) $(wildcard $(filter %.d,$(OBJECTS:.o=.d))) $(EXECUTABLE) $(SOURCE)/Main/Version.h
	rm -rf $(TURBOVISION)/build

# A header that has gone or has been renamed is no reason to stop: -MP gives the headers of the dependencies empty rules,
# this one serves the files written before it, the object is built again once a header it needs has changed
$(SOURCE)/%.h: ;

-include $(wildcard $(filter %.d,$(OBJECTS:.o=.d)))

.PHONY: all build install deb rpm arch clean FORCE
