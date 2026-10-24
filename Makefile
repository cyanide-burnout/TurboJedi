EXECUTABLE  := turbojedi
TURBOVISION := Import/tvision
PREFIX      := $(DESTDIR)/usr/local

# A clone tells by git, an archive of git has VERSION filled in, anything else is unknown
VERSION := $(if $(wildcard .git),$(shell git describe --tags --always --dirty 2>/dev/null))
VERSION := $(or $(VERSION),$(shell grep -v '^\$$Format' VERSION 2>/dev/null),unknown)

# Versions of Debian start with a digit and keep - for the revision: v0.1-3-gabcdef becomes 0.1+3+gabcdef
PACKAGE := turbojedi
RELEASE := $(shell echo '$(VERSION)' | sed -e 's/^v//' -e 's/-/+/g' -e 's/^unknown$$/0~unknown/')
AUTHOR  := Artem Prilutskiy <cyanide.burnout@gmail.com>

LIBRARIES := \
	ncursesw

DEPENDENCIES := \
	libsystemd \
	libpcre2-8

OBJECTS := \
	TurboJedi.o \
	JediApplication.o \
	JediStatusLine.o \
	JediClock.o \
	JournalView.o \
	JournalWindow.o \
	JournalHeader.o \
	JournalTools.o \
	JournalLayout.o \
	SourceDialog.o \
	PickDialog.o \
	TimeDialog.o \
	TimePickDialog.o \
	CalendarView.o \
	PageView.o \
	PageDialog.o \
	JournalExpression.o \
	JournalHighlighter.o \
	JournalMatcher.o \
	TextDialog.o \
	LevelsDialog.o \
	ColumnsDialog.o \
	$(TURBOVISION)/build/libtvision.a

FLAGS += \
	-Wno-unused-result -O2 -MMD -gdwarf \
	-I$(TURBOVISION)/include \
	$(shell pkg-config --cflags $(DEPENDENCIES))

LIBS := \
	$(foreach library, $(LIBRARIES), -l$(library)) \
	$(shell pkg-config --libs $(DEPENDENCIES))

CXXFLAGS += $(FLAGS) -std=gnu++2c

all: build

$(TURBOVISION)/build/libtvision.a:
	cmake -S $(TURBOVISION) -B $(TURBOVISION)/build -D CMAKE_BUILD_TYPE=Release -D CMAKE_POSITION_INDEPENDENT_CODE=ON -D TV_BUILD_EXAMPLES=OFF -D TV_BUILD_USING_GPM=OFF
	+make -C $(TURBOVISION)/build tvision

# Rewritten only when the version changes, so that only its users are built again
Version.h: FORCE
	@echo '#define VERSION "$(VERSION)"' | cmp -s - $@ || echo '#define VERSION "$(VERSION)"' > $@

TurboJedi.o JediApplication.o: Version.h

build: $(OBJECTS)
	$(CXX) $(OBJECTS) $(FLAGS) $(LDFLAGS) $(LIBS) -o $(EXECUTABLE)

install:
	install -D -m 755 $(EXECUTABLE) $(PREFIX)/bin/$(EXECUTABLE)
	install -D -m 644 $(EXECUTABLE).1 $(PREFIX)/share/man/man1/$(EXECUTABLE).1

# The package goes next to the directory of the project, as dpkg-buildpackage puts it
package:
	sh debian/changelog.sh '$(PACKAGE)' '$(RELEASE)' '$(AUTHOR)' > debian/changelog
	dpkg-buildpackage -b -tc -us -uc -j4
	rm -f debian/changelog

clean:
	rm -f $(wildcard $(filter %.o,$(OBJECTS))) $(wildcard $(filter %.d,$(OBJECTS:.o=.d))) $(EXECUTABLE) Version.h
	rm -rf $(TURBOVISION)/build

-include $(wildcard $(filter %.d,$(OBJECTS:.o=.d)))

.PHONY: all build install package clean FORCE
