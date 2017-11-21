CXX = clang++

SRCDIR := src
INCDIR := include
BUILDDIR := build
TARGETDIR := bin

SOURCES = rwte.cpp
OBJECTS = ${SOURCES:.cpp=.o}
EXECUTABLE = rwte
TARGET := $(TARGETDIR)/$(EXECUTABLE)

INSTALLBINDIR := x

# Code Lists
SRCEXT := cpp
HEADERS := $(shell find $(INCDIR) -type f -name *.h)
SOURCES := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.o))

# Folder Lists
# Note: Intentionally excludes the root of the include folder so the lists are clean
INCDIRS := $(shell find $(INCDIR)/**/* -name '*.h' -exec dirname {} \; | sort | uniq)
#INCLIST := $(patsubst include/%,-I include/%,$(INCDIRS))
BUILDLIST := $(patsubst include/%,$(BUILDDIR)/%,$(INCDIRS))

# Shared Compiler Flags
#INC := -I include $(INCLIST) -I /usr/local/include
INC := -I include -I /usr/local/include
CFLAGS = -c -Wall -pedantic -std=c++14 -O3 `pkg-config --cflags xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`
# eventually, -O2 rather than -g
LDFLAGS= -O3 -lev -lutil -lxdg-basedir `pkg-config --libs xcb xcb-util cairo pangocairo xkbcommon xkbcommon-x11 xcb-xkb lua`

$(TARGET): $(OBJECTS)
	@mkdir -p $(TARGETDIR)
	@echo "Linking..."
	@echo "  Linking $(TARGET)"; $(CXX) $^ -o $(TARGET) $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(BUILDLIST)
	@echo "Compiling $<..."; $(CXX) $(CFLAGS) $(INC) -c -o $@ $<

.PHONY: docs
docs: $(HEADERS) $(SOURCES)
	@ldoc .

clean:
	@echo "Cleaning $(TARGET)..."; $(RM) -r $(BUILDDIR) $(TARGET)

install:
	@echo "Installing $(EXECUTABLE)..."; cp $(TARGET) $(INSTALLBINDIR)

distclean:
	@echo "Removing $(EXECUTABLE)"; rm $(INSTALLBINDIR)/$(EXECUTABLE)

.PHONY: clean
all: $(OBJECTS) $(EXES)

