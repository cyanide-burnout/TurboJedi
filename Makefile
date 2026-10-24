EXECUTABLE  := turbojedi
TURBOVISION := Import/tvision
PREFIX      := $(DESTDIR)/usr/local

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

build: $(OBJECTS)
	$(CXX) $(OBJECTS) $(FLAGS) $(LIBS) -o $(EXECUTABLE)

install:
	install -D -m 755 $(EXECUTABLE) $(PREFIX)/bin/$(EXECUTABLE)
	install -D -m 644 $(EXECUTABLE).1 $(PREFIX)/share/man/man1/$(EXECUTABLE).1

clean:
	rm -f $(wildcard $(filter %.o,$(OBJECTS))) $(wildcard $(filter %.d,$(OBJECTS:.o=.d))) $(EXECUTABLE)
	rm -rf $(TURBOVISION)/build

-include $(wildcard $(filter %.d,$(OBJECTS:.o=.d)))

.PHONY: all build install clean
