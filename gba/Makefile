# joypad-tester GBA payloads — two multiboot ROMs built from one source
# tree:
#
#   joypad_mb.gba (VARIANT=joypad, default)
#     Doridian joybus controller + animated eyes overlay. Consumed by
#     joypad-os via submodule for its GBA-as-controller mode.
#
#   tester_mb.gba (VARIANT=tester)
#     Doridian joybus controller + on-GBA console button display.
#     Consumed by joypad-tester's GameCube/Wii host so users see test
#     feedback on the GBA itself; also usable standalone via flash cart
#     for a pure button tester.
#
# Build both:  make            (or `make all`)
# Build one:   make joypad     (or `make tester`)
# Clean:       make clean
#
# The TARGET uses the "_mb" suffix so devkitARM's stock %_mb.elf rule
# picks the multiboot specs file (loads at 0x02000000 instead of
# 0x08000000).

ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO. typically: export DEVKITPRO=/opt/devkitpro")
endif

include $(DEVKITPRO)/devkitARM/gba_rules

# devkitPro's stock rules invoke `$(LD)` for the link step but never
# define LD anywhere — make's default is plain `ld`, which doesn't
# understand `-specs=` or `-mthumb-interwork`. Force LD to the gcc
# frontend so these flags pass through.
export LD := $(CC)

VARIANT ?= joypad

ifeq ($(VARIANT),tester)
    TARGET := tester
    CFILES := main_tester.c
else
    TARGET := joypad
    CFILES := main.c display.c eyes_anim.c
endif

SOURCES  := source
INCLUDES :=
BUILD    := build/$(TARGET)

# SRCROOT is the original source-root dir. Captured before any -C
# recursion so inner-pass paths stay correct.
SRCROOT  ?= $(CURDIR)

LIBS     := -lgba -lm
LIBDIRS  := $(LIBGBA)

# Variant-dependent paths first, so CFLAGS' INCLUDE expansion below picks
# them up.
export OUTPUT   := $(SRCROOT)/$(BUILD)/$(TARGET)
export VPATH    := $(SRCROOT)/source
export DEPSDIR  := $(SRCROOT)/$(BUILD)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(SRCROOT)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(SRCROOT)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export OFILES   := $(CFILES:.c=.o)

ARCH     := -mthumb-interwork -mthumb
CFLAGS   := -g -Wall -O3 \
            -mcpu=arm7tdmi -mtune=arm7tdmi \
            -fomit-frame-pointer -ffast-math \
            $(ARCH) $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := $(ARCH)
LDFLAGS  := -g $(ARCH) -Wl,-Map,$(TARGET).map

ifneq ($(INNER_PASS),1)

# Outer pass: dispatch each requested variant via a recursive inner pass.
.PHONY: all joypad tester clean

all: joypad tester

joypad:
	@mkdir -p build/joypad
	@$(MAKE) --no-print-directory -C build/joypad -f $(SRCROOT)/Makefile \
	         VARIANT=joypad SRCROOT=$(SRCROOT) INNER_PASS=1

tester:
	@mkdir -p build/tester
	@$(MAKE) --no-print-directory -C build/tester -f $(SRCROOT)/Makefile \
	         VARIANT=tester SRCROOT=$(SRCROOT) INNER_PASS=1

clean:
	@echo cleaning ...
	@rm -rf build

else

# Inner pass: actually compile + link this variant. CURDIR is the build
# dir; gba_rules' implicit %.o and %_mb.elf rules find sources via VPATH
# and emit objects/binaries in CURDIR.
all: $(OUTPUT)_payload.c

$(OUTPUT)_payload.c: $(OUTPUT)_mb.gba
	@echo embedding $(notdir $<) -^> $(notdir $@)
	@python3 $(SRCROOT)/tools/bin2c.py $< $@ --source-name $(TARGET)_mb.gba

$(OUTPUT)_mb.gba: $(OUTPUT)_mb.elf
$(OUTPUT)_mb.elf: $(OFILES)

-include $(OFILES:.o=.d)

endif
