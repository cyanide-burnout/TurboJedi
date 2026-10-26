EXECUTABLE  := turbojedi
SOURCE      := Source
TURBOVISION := Import/tvision
PREFIX      := $(DESTDIR)/usr/local

# A clone tells by git, an archive of git has VERSION filled in, anything else is unknown
VERSION := $(if $(wildcard .git),$(shell git describe --tags --always --dirty 2>/dev/null))
VERSION := $(or $(VERSION),$(shell grep -v '^\$$Format' VERSION 2>/dev/null),unknown)

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
	$(SOURCE)/JournalWindow.o \
	$(SOURCE)/JournalHeader.o \
	$(SOURCE)/TimelineView.o \
	$(SOURCE)/TimelineWindow.o \
	$(SOURCE)/JournalTools.o \
	$(SOURCE)/JournalLayout.o \
	$(SOURCE)/SourceDialog.o \
	$(SOURCE)/PickDialog.o \
	$(SOURCE)/TimeDialog.o \
	$(SOURCE)/TimePickDialog.o \
	$(SOURCE)/CalendarView.o \
	$(SOURCE)/PageView.o \
	$(SOURCE)/PageDialog.o \
	$(SOURCE)/JournalExpression.o \
	$(SOURCE)/JournalHighlighter.o \
	$(SOURCE)/JournalMatcher.o \
	$(SOURCE)/TextDialog.o \
	$(SOURCE)/LevelsDialog.o \
	$(SOURCE)/ColumnsDialog.o \
	$(SOURCE)/JournalsDialog.o \
	$(SOURCE)/DirectoryDialog.o \
	$(TURBOVISION)/build/libtvision.a

FLAGS += \
	-Wno-unused-result -O2 -MMD -gdwarf \
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
	@echo '#define VERSION "$(VERSION)"' | cmp -s - $@ || echo '#define VERSION "$(VERSION)"' > $@

$(SOURCE)/TurboJedi.o $(SOURCE)/JediApplication.o: $(SOURCE)/Version.h

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
	dpkg-buildpackage -b -a$(DISTRIBUTION) -tc -us -uc -j4
else
	dpkg-buildpackage -b -tc -us -uc -j4
endif
	rm -f debian/changelog

# Packages of Fedora and RHEL go to rpmbuild/RPMS
rpm:
	rpmbuild -bb --define '_topdir $(CURDIR)/rpmbuild' --define 'source_date_epoch_from_changelog 0' --define 'turbojedi_tree $(CURDIR)' --define 'turbojedi_version $(RELEASE)' --define 'turbojedi_tvision $(BUNDLED)' rpm/turbojedi.spec

# Package of Arch Linux goes next to arch/PKGBUILD, makepkg does not run as root
arch:
	cd arch && TURBOJEDI_VERSION='$(RELEASE)' makepkg -f

clean:
	rm -f $(wildcard $(filter %.o,$(OBJECTS))) $(wildcard $(filter %.d,$(OBJECTS:.o=.d))) $(EXECUTABLE) $(SOURCE)/Version.h
	rm -rf $(TURBOVISION)/build

-include $(wildcard $(filter %.d,$(OBJECTS:.o=.d)))

.PHONY: all build install deb rpm arch clean FORCE
