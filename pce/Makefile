# Joypad Tester - PC Engine
#
# Builds a single HuCard ROM (joypad-tester.pce) from src/joypad_tester.c
# using HuC. Designed to run inside the buildtools/ Docker image (which
# pins HuC to a specific uli/huc commit), but works against any HuC
# install that puts `huc` on PATH and headers in /usr/include/pce.

TARGET   := joypad-tester
SRCDIR   := src
BUILDDIR := build
CFILES   := joypad_tester.c
HFILES   := gen_logo.h
SOURCES  := $(addprefix $(SRCDIR)/,$(CFILES) $(HFILES))

ROM := $(BUILDDIR)/$(TARGET).pce

.PHONY: all clean

all: $(ROM)

# HuC writes its outputs alongside the source file regardless of where
# we'd like them, so stage everything inside BUILDDIR (the .c renamed to
# the canonical target name, plus any local includes) and invoke huc
# from there. Output: $(TARGET).pce.
$(ROM): $(SOURCES) | $(BUILDDIR)
	cp $(SRCDIR)/$(CFILES) $(BUILDDIR)/$(TARGET).c
	cp $(SRCDIR)/$(HFILES) $(BUILDDIR)/
	cd $(BUILDDIR) && huc -v $(TARGET).c
	@echo "Built $(ROM)"

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
