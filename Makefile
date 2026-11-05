EXECUTABLE  := turbojedi
SOURCE      := Source
TURBOVISION := Import/tvision
PREFIX      := $(DESTDIR)/usr/local

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

LIBRARIES := \
	ncursesw

DEPENDENCIES := \
	libsystemd \
	libpcre2-8

# dpkg-buildpackage -a gives the cross compilers and pkg-config, tvision has to be built with them as well
PKG_CONFIG ?= pkg-config
CMAKEFLAGS := -D CMAKE_C_COMPILER=$(CC) -D CMAKE_CXX_COMPILER=$(CXX)

ifneq ($(DEB_HOST_GNU_TYPE), $(DEB_BUILD_GNU_TYPE))
	CMAKEFLAGS += -D CMAKE_SYSTEM_NAME=Linux -D CMAKE_SYSTEM_PROCESSOR=$(DEB_HOST_GNU_CPU) -D CMAKE_LIBRARY_ARCHITECTURE=$(DEB_HOST_MULTIARCH)
endif

OBJECTS := \
	$(SOURCE)/TurboJedi.o \
	$(SOURCE)/JediApplication.o \
	$(SOURCE)/JediStatusLine.o \
	$(SOURCE)/JediClock.o \
	$(SOURCE)/JediClipboard.o \
	$(SOURCE)/JournalView.o \
	$(SOURCE)/JournalSource.o \
	$(SOURCE)/JournalWindow.o \
	$(SOURCE)/JournalHeader.o \
	$(SOURCE)/TimelineView.o \
	$(SOURCE)/TimelineWindow.o \
	$(SOURCE)/JournalTools.o \
	$(SOURCE)/JournalLayout.o \
	$(SOURCE)/SourceDialog.o \
	$(SOURCE)/PickDialog.o \
	$(SOURCE)/WindowsDialog.o \
	$(SOURCE)/TimeDialog.o \
	$(SOURCE)/TimePickDialog.o \
	$(SOURCE)/CalendarView.o \
	$(SOURCE)/PageView.o \
	$(SOURCE)/PageDialog.o \
	$(SOURCE)/JournalExpression.o \
	$(SOURCE)/JournalHighlighter.o \
	$(SOURCE)/JournalEscapes.o \
	$(SOURCE)/JournalMatcher.o \
	$(SOURCE)/TextDialog.o \
	$(SOURCE)/LevelsDialog.o \
	$(SOURCE)/ColumnsDialog.o \
	$(SOURCE)/JournalsDialog.o \
	$(SOURCE)/DirectoryDialog.o \
	$(TURBOVISION)/build/libtvision.a

# libcurl and the repository let the Help menu check for a newer release, without either the item is left out
ifneq ($(REPOSITORY), )
ifeq ($(shell $(PKG_CONFIG) --exists libcurl && echo yes), yes)
	DEPENDENCIES += libcurl
	OBJECTS      += $(SOURCE)/JediUpdate.o $(SOURCE)/UpdateDialog.o
	FLAGS        += -DUSE_UPDATE
endif
endif

FLAGS += \
	-Wno-unused-result -O2 -flto=auto -MMD -gdwarf \
	-I$(TURBOVISION)/include \
	$(shell $(PKG_CONFIG) --cflags $(DEPENDENCIES))

LIBS := \
	$(foreach library, $(LIBRARIES), -l$(library)) \
	$(shell $(PKG_CONFIG) --libs $(DEPENDENCIES))

CXXFLAGS += $(FLAGS) -std=gnu++17

# Libraries tvision has found on its own, told once it is configured: libgpm for the mouse of the Linux console
EXTRA = $(if $(shell grep -s '^GPM:FILEPATH=/' $(TURBOVISION)/build/CMakeCache.txt),-lgpm)

all: build

$(TURBOVISION)/build/libtvision.a:
	cmake -S $(TURBOVISION) -B $(TURBOVISION)/build $(CMAKEFLAGS) -D CMAKE_BUILD_TYPE=Release -D CMAKE_POSITION_INDEPENDENT_CODE=ON -D TV_BUILD_EXAMPLES=OFF -D TV_BUILD_USING_GPM=ON
	+make -C $(TURBOVISION)/build tvision

# Rewritten only when the version changes, so that only its users are built again
$(SOURCE)/Version.h: FORCE
	@printf '#define VERSION "%s"\n#define BUILT_FOR "%s"\n#define REPOSITORY "%s"\n' '$(VERSION)' '$(TARGET)' '$(REPOSITORY)' | cmp -s - $@ || \
	 printf '#define VERSION "%s"\n#define BUILT_FOR "%s"\n#define REPOSITORY "%s"\n' '$(VERSION)' '$(TARGET)' '$(REPOSITORY)' > $@

$(SOURCE)/TurboJedi.o $(SOURCE)/JediApplication.o $(SOURCE)/JediUpdate.o: $(SOURCE)/Version.h

# Internal headers of Turbo Vision include each other from there, only the event source needs them
$(SOURCE)/JournalSource.o: CXXFLAGS += -I$(TURBOVISION)/include/tvision

build: $(OBJECTS)
	$(CXX) $(OBJECTS) $(FLAGS) $(LDFLAGS) $(LIBS) $(EXTRA) -o $(EXECUTABLE)

install:
	install -D -m 755 $(EXECUTABLE) $(PREFIX)/bin/$(EXECUTABLE)
	install -D -m 644 $(EXECUTABLE).1 $(PREFIX)/share/man/man1/$(EXECUTABLE).1

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
	rm -f $(wildcard $(filter %.o,$(OBJECTS))) $(wildcard $(filter %.d,$(OBJECTS:.o=.d))) $(EXECUTABLE) $(SOURCE)/Version.h
	rm -rf $(TURBOVISION)/build

-include $(wildcard $(filter %.d,$(OBJECTS:.o=.d)))

.PHONY: all build install deb rpm arch clean FORCE
