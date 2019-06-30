CC := clang
CXX := clang++

SRCDIR := src
INCDIR := include
BUILDDIR := build
TARGETDIR := bin

EXECUTABLE := rwte
TARGET := $(TARGETDIR)/$(EXECUTABLE)
TEST_EXECUTABLE := rwte-test
TEST_TARGET := $(TARGETDIR)/$(TEST_EXECUTABLE)

INSTALLBINDIR := x

# Code Lists
SRCEXT := cpp
HEADERS := $(shell find $(INCDIR) -type f -name *.h)
ALL_SOURCES := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT)) $(shell find $(SRCDIR) -type f -name *.c)

# sources without test main
SOURCES := $(filter-out $(SRCDIR)/rwte-test.cpp, $(ALL_SOURCES))
SRC_TO_OBJ1 := $(SOURCES:.$(SRCEXT)=.o)
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SRC_TO_OBJ1:.c=.o))
# sources with test main
TEST_SOURCES := $(filter-out $(SRCDIR)/rwte-main.cpp, $(ALL_SOURCES))
SRC_TO_OBJ2 := $(TEST_SOURCES:.$(SRCEXT)=.o)
TEST_OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SRC_TO_OBJ2:.c=.o))

# Folder Lists
# Note: Intentionally excludes the root of the include folder so the lists are clean
INCDIRS := $(shell find $(INCDIR)/**/* -name '*.h' -exec dirname {} \; | sort | uniq)
#INCLIST := $(patsubst include/%,-I include/%,$(INCDIRS))
BUILDLIST := $(patsubst include/%,$(BUILDDIR)/%,$(INCDIRS))

PKGLIBS=libxdg-basedir xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb wayland-client wayland-cursor lua

# Shared Compiler Flags
#INC := -I include $(INCLIST) -I /usr/local/include
INC := -I include -I /usr/local/include

# todo: add -flto
CFLAGS = -c -O3 -Wall -pedantic `pkg-config --cflags $(PKGLIBS)`
CXXFLAGS = -c -O3 -Wall -pedantic -std=c++17 -O3 `pkg-config --cflags $(PKGLIBS)`
LDFLAGS= -O3 -lev -lutil -lrt `pkg-config --libs $(PKGLIBS)`
TIDYFLAGS = -std=c++17 $(INC) `pkg-config --cflags $(PKGLIBS)`

# todo: use some kinda debug flag to control this?
# CFLAGS = -c -g -Wall -pedantic `pkg-config --cflags $(PKGLIBS)`
# CXXFLAGS = -c -g -Wall -pedantic -std=c++17 `pkg-config --cflags $(PKGLIBS)`
# LDFLAGS= -g -lev -lutil -lrt `pkg-config --libs $(PKGLIBS)`

$(TARGET): $(OBJECTS)
	@mkdir -p $(TARGETDIR)
	@echo "Linking..."
	@echo "  Linking $(TARGET)"; $(CXX) $^ -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJECTS)
	@mkdir -p $(TARGETDIR)
	@echo "Linking..."
	@echo "  Linking $(TEST_TARGET)"; $(CXX) $^ -o $(TEST_TARGET) $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(BUILDLIST)
	@echo "Compiling $<..."; $(CXX) $(CXXFLAGS) $(INC) -c -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(BUILDLIST)
	@echo "Compiling $<..."; $(CC) $(CFLAGS) $(INC) -c -o $@ $<

#tidy:
#    find src/* src/**/* -name '*.cpp' -print0 | xargs -0 -I{} echo {} -- $(TIDYFLAGS)
#
#-checks=*,-google-*,-llvm-namespace-comment,
#-clang-analyzer-alpha*,-cppcoreguidelines*,
#-modernize-raw-string-literal,-misc-sizeof-expression,
#-cert-err58-cpp,-cert-err60-cpp

# note: /fmt is not included in header filter
build/%.clang-tidy: %
	@mkdir -p $(@D)
	@clang-tidy \
		-checks=*,-*readability-braces-around-statements,-fuchsia-default-arguments,-hicpp-braces-around-statements \
		-header-filter='.*' \
		$< \
		-- $(TIDYFLAGS) \
		2>/dev/null

	@touch $@

.PHONY: clang-tidy
clang-tidy:  $(SOURCES:%=build/%.clang-tidy)
	@echo "All $(words $(HEADERS) $(SOURCES)) clang-tidy tests passed."

.PHONY: docs
docs: $(HEADERS) $(SOURCES)
	@ldoc .

.PHONY: test
test: $(TEST_TARGET)
	$(TEST_TARGET)

clean:
	@echo "Cleaning $(TARGET)..."; $(RM) -r $(BUILDDIR) $(TARGET)

install:
	@echo "Installing $(EXECUTABLE)..."; cp $(TARGET) $(INSTALLBINDIR)

distclean:
	@echo "Removing $(EXECUTABLE)"; rm $(INSTALLBINDIR)/$(EXECUTABLE)

.PHONY: clean
all: $(OBJECTS) $(EXES)

