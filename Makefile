#---------------------------------------------------------------------------------
# gcsec-dump — libnx homebrew (template standard devkitPro)
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

export DEVKITA64 := $(DEVKITPRO)/devkitA64

TOPDIR ?= $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

include $(DEVKITPRO)/libnx/switch_rules

#-----------------------------------------------------------------------
APP_TITLE   := gcsec-dump
APP_AUTHOR  := metroid-pipeline
APP_VERSION := 1.0.0
ICON        := icon.png

TARGET   := $(notdir $(CURDIR))
BUILD    := build
SOURCES  := source
DATA     := data
INCLUDES := include
ROMFS    := romfs

#-----------------------------------------------------------------------
ARCH     := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS   := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) $(INCLUDE) -D__SWITCH__
CFLAGS   += -Wno-unused-variable -Wno-unused-function

CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lnx

LIBDIRS  := $(PORTLIBS) $(LIBNX)

#-----------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(TOPDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(ROMFS),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

# exports toolchain (sinon le linker hote x86 est utilise — bug corrigé)
export CC   := $(DEVKITA64)/bin/aarch64-none-elf-gcc
export CXX  := $(DEVKITA64)/bin/aarch64-none-elf-g++
export AS   := $(DEVKITA64)/bin/aarch64-none-elf-as
export AR   := $(DEVKITA64)/bin/aarch64-none-elf-ar
export OBJCOPY := $(DEVKITA64)/bin/aarch64-none-elf-objcopy
export OBJDUMP := $(DEVKITA64)/bin/aarch64-none-elf-objdump
export LD   := $(CC)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq ($(strip $(ICON)),)
export NROFLAGS += --icon=$(ICON)
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES_BIN)

%-keyboard.bin: %_keyboard.png
	@bin2s -a 4 $< | $(AS) -o $(@)

-include $(DEPSDIR)/*.d

endif
