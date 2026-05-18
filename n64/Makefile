# Joypad Tester - Nintendo 64
#
# Builds a single joypad-tester.z64 from src/main.c, linking against
# LibDragon's bundled joypad subsystem (the upstream-merged version of
# meeq/JoypadTest-N64's library). Runs inside the buildtools/ Docker
# container via build_docker.sh.

TARGET    := joypad-tester
SRCDIR    := src
BUILD_DIR := build

SRC := $(SRCDIR)/main.c        \
       $(SRCDIR)/gba.c         \
       $(SRCDIR)/gba_payload.c \
       $(SRCDIR)/mouse.c       \
       $(SRCDIR)/screensaver.c \
       $(SRCDIR)/tpak_info.c

OBJS := $(SRC:%.c=$(BUILD_DIR)/%.o)
DEPS := $(SRC:%.c=$(BUILD_DIR)/%.d)

include $(N64_INST)/include/n64.mk

all: $(TARGET).z64

$(TARGET).z64: N64_ROM_TITLE = JoypadTester
$(BUILD_DIR)/$(TARGET).elf: $(OBJS)

# n64.mk writes the .z64 next to the Makefile; build_docker.sh
# mirrors it into build/ post-make so other consoles' artifact paths
# stay consistent. We deliberately do NOT add a $(BUILD_DIR)/%.z64
# target here -- it would match n64.mk's %.z64 pattern and cause its
# `LDFLAGS+=$(N64_LDFLAGS)` target-specific assignment to fire twice
# (once for the copy target, once for its prerequisite), which puts
# `-Tn64.ld` on the link command line twice and ld blows up with
# "linker script file 'n64.ld' appears multiple times".

clean:
	rm -rf $(BUILD_DIR) $(TARGET).z64

-include $(DEPS)

.PHONY: all clean
