# Joypad Tester — PC Engine
#
# Builds a single HuCard ROM (joypad-tester.pce) from src/joypad_tester.c
# using HuC. Designed to run inside the buildtools/ Docker image (which
# pins HuC to a specific uli/huc commit), but works against any HuC
# install that puts `huc` on PATH and headers in /usr/include/pce.

TARGET   := joypad-tester
SRCDIR   := src
BUILDDIR := build
SOURCES  := $(SRCDIR)/joypad_tester.c

ROM := $(BUILDDIR)/$(TARGET).pce

.PHONY: all clean

all: $(ROM)

# HuC writes its outputs alongside the source file regardless of where
# we'd like them, so build inside BUILDDIR with a copy of the source.
# Then rename the generated .pce to the canonical artifact name.
$(ROM): $(SOURCES) | $(BUILDDIR)
	cp $(SOURCES) $(BUILDDIR)/$(TARGET).c
	cd $(BUILDDIR) && huc -v $(TARGET).c
	@echo "Built $(ROM)"

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
