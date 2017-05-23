CXX = clang++

SRCDIR := src
BUILDDIR := build
TARGETDIR := bin

SOURCES = rwte.cpp
OBJECTS = ${SOURCES:.cpp=.o}
EXECUTABLE = rwte
TARGET := $(TARGETDIR)/$(EXECUTABLE)

INSTALLBINDIR := x

# Code Lists
SRCEXT := cpp
SOURCES := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.o))

# Folder Lists
# Note: Intentionally excludes the root of the include folder so the lists are clean
INCDIRS := $(shell find include/**/* -name '*.h' -exec dirname {} \; | sort | uniq)
#INCLIST := $(patsubst include/%,-I include/%,$(INCDIRS))
BUILDLIST := $(patsubst include/%,$(BUILDDIR)/%,$(INCDIRS))

# Shared Compiler Flags
#INC := -I include $(INCLIST) -I /usr/local/include
INC := -I include -I /usr/local/include
CFLAGS = -c -Wall -pedantic -std=c++14 -g `pkg-config --cflags xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`
# eventually, -O2 rather than -g
LDFLAGS= -lev -lutil `pkg-config --libs xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`

$(TARGET): $(OBJECTS)
	@mkdir -p $(TARGETDIR)
	@echo "Linking..."
	@echo "  Linking $(TARGET)"; $(CXX) $^ -o $(TARGET) $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(BUILDLIST)
	@echo "Compiling $<..."; $(CXX) $(CFLAGS) $(INC) -c -o $@ $<

clean:
	@echo "Cleaning $(TARGET)..."; $(RM) -r $(BUILDDIR) $(TARGET)

install:
	@echo "Installing $(EXECUTABLE)..."; cp $(TARGET) $(INSTALLBINDIR)

distclean:
	@echo "Removing $(EXECUTABLE)"; rm $(INSTALLBINDIR)/$(EXECUTABLE)

.PHONY: clean
all: $(OBJECTS) $(EXES)

