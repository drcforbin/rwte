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
ALL_SOURCES := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))

# sources without test main
SOURCES := $(filter-out $(SRCDIR)/rwte-test.cpp, $(ALL_SOURCES))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.o))
# sources with test main
TEST_SOURCES := $(filter-out $(SRCDIR)/rwte-main.cpp, $(ALL_SOURCES))
TEST_OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(TEST_SOURCES:.$(SRCEXT)=.o))

# Folder Lists
# Note: Intentionally excludes the root of the include folder so the lists are clean
INCDIRS := $(shell find $(INCDIR)/**/* -name '*.h' -exec dirname {} \; | sort | uniq)
#INCLIST := $(patsubst include/%,-I include/%,$(INCDIRS))
BUILDLIST := $(patsubst include/%,$(BUILDDIR)/%,$(INCDIRS))

# Shared Compiler Flags
#INC := -I include $(INCLIST) -I /usr/local/include
INC := -I include -I /usr/local/include
# eventually, -O3 rather than -g
CFLAGS = -c -Wall -pedantic -std=c++14 -O3 `pkg-config --cflags xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`
LDFLAGS= -O3 -lev -lutil -lxdg-basedir `pkg-config --libs xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`
#CFLAGS = -c -Wall -pedantic -std=c++14 -g `pkg-config --cflags xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`
#LDFLAGS= -g -lev -lutil -lxdg-basedir `pkg-config --libs xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`
TIDYFLAGS = -std=c++14 $(INC) `pkg-config --cflags xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`

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
	@echo "Compiling $<..."; $(CXX) $(CFLAGS) $(INC) -c -o $@ $<

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
		-checks=*,-*readability-braces-around-statements \
		-header-filter="/lua|/rwte" \
		$< \
		-- $(TIDYFLAGS) \
		2>/dev/null

	@touch $@

.PHONY: clang-tidy
clang-tidy: $(HEADERS:%=build/%.clang-tidy) $(SOURCES:%=build/%.clang-tidy)
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

