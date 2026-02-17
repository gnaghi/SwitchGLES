#---------------------------------------------------------------------------------
# SwitchGLES Makefile
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Version
#---------------------------------------------------------------------------------
export SWITCHGLES_MAJOR	:= 0
export SWITCHGLES_MINOR	:= 1
export SWITCHGLES_PATCH	:= 0

VERSION	:=	$(SWITCHGLES_MAJOR).$(SWITCHGLES_MINOR).$(SWITCHGLES_PATCH)

#---------------------------------------------------------------------------------
# Directories
#---------------------------------------------------------------------------------
SOURCES		:=	source source/util source/context source/backend source/backend/deko3d source/gl
INCLUDES	:=	include source

#---------------------------------------------------------------------------------
# Options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec

CFLAGS	:=	-g -Wall -Wextra -O0 \
			-ffunction-sections \
			-fdata-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -DSGL_DEBUG -DSGL_ENABLE_RUNTIME_COMPILER

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:=	-g $(ARCH)

#---------------------------------------------------------------------------------
# Library directories
#---------------------------------------------------------------------------------
LIBDIRS	:= $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 		:=	$(OFILES_SRC)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/../../libuam/source \
			-I.

.PHONY: clean all library shaders examples install uninstall

all: library

library: lib/libSwitchGLES.a

lib/libSwitchGLES.a: $(SOURCES) $(INCLUDES) | lib_dir build
	@$(MAKE) BUILD=build OUTPUT=$(CURDIR)/$@ \
	DEPSDIR=$(CURDIR)/build \
	--no-print-directory -C build \
	-f $(CURDIR)/Makefile

lib_dir:
	@mkdir -p lib

build:
	@mkdir -p build

shaders:
	@echo "Building FFP shaders..."
	@mkdir -p romfs/shaders
	@if command -v uam >/dev/null 2>&1; then \
		uam -s vert shaders/ffp_basic.vert.glsl -o romfs/shaders/ffp_basic.vert.dksh; \
		uam -s frag shaders/ffp_notex.frag.glsl -o romfs/shaders/ffp_notex.frag.dksh; \
		echo "Shaders compiled successfully!"; \
	else \
		echo "Warning: UAM not found. Please compile shaders manually."; \
	fi

examples: library
	@$(MAKE) -C examples/triangle

install: library
	@echo "Installing SwitchGLES to $(DEVKITPRO)/portlibs/switch..."
	@mkdir -p $(DEVKITPRO)/portlibs/switch/lib
	@mkdir -p $(DEVKITPRO)/portlibs/switch/include/switchgles/EGL
	@mkdir -p $(DEVKITPRO)/portlibs/switch/include/switchgles/GLES2
	@mkdir -p $(DEVKITPRO)/portlibs/switch/include/switchgles/KHR
	@cp lib/libSwitchGLES.a $(DEVKITPRO)/portlibs/switch/lib/
	@cp include/EGL/*.h $(DEVKITPRO)/portlibs/switch/include/switchgles/EGL/
	@cp include/GLES2/*.h $(DEVKITPRO)/portlibs/switch/include/switchgles/GLES2/
	@cp include/KHR/*.h $(DEVKITPRO)/portlibs/switch/include/switchgles/KHR/
	@echo "SwitchGLES installed successfully!"
	@echo "  Library: $(DEVKITPRO)/portlibs/switch/lib/libSwitchGLES.a"
	@echo "  Headers: $(DEVKITPRO)/portlibs/switch/include/switchgles/{EGL,GLES2,KHR}/"

uninstall:
	@echo "Uninstalling SwitchGLES from $(DEVKITPRO)/portlibs/switch..."
	@rm -f $(DEVKITPRO)/portlibs/switch/lib/libSwitchGLES.a
	@rm -rf $(DEVKITPRO)/portlibs/switch/include/switchgles
	@echo "SwitchGLES uninstalled."

clean:
	@echo "Cleaning..."
	@rm -rf build lib romfs/shaders
	@$(MAKE) -C examples/triangle clean 2>/dev/null || true

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
$(OUTPUT)	:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
