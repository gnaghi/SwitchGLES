#---------------------------------------------------------------------------------
# FBO Test - Framebuffer Objects demo
# Target: SwitchGLES (deko3d backend)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source
INCLUDES	:=	include
ROMFS		:=	romfs

#---------------------------------------------------------------------------------
# Shader compilation
#---------------------------------------------------------------------------------
SHADERS_DIR	:=	source
SHADERS_OUT	:=	$(ROMFS)/shaders

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# SwitchGLES library - use absolute path
SWITCHGL_LIB	:=	$(CURDIR)/../../lib

LIBS	:= -lSwitchGLES -ldeko3d -lnx -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export SWITCHGL_LIB

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

export LD	:=	$(CC)

export OFILES_SRC	:=	$(CFILES:.c=.o)
export OFILES 	:=	$(OFILES_SRC)
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			-I$(CURDIR)/../../include \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) -L$(SWITCHGL_LIB)

# ROMFS flags for elf2nro - must use relative path for Windows compatibility
ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=../$(ROMFS)
endif

.PHONY: $(BUILD) clean all shaders

#---------------------------------------------------------------------------------
all: shaders $(BUILD)

shaders:
	@echo "Compiling shaders..."
	@mkdir -p $(SHADERS_OUT)
	@uam -s vert $(SHADERS_DIR)/triangle_vsh.glsl -o $(SHADERS_OUT)/triangle_vsh.dksh
	@uam -s frag $(SHADERS_DIR)/triangle_fsh.glsl -o $(SHADERS_OUT)/triangle_fsh.dksh
	@uam -s vert $(SHADERS_DIR)/quad_vsh.glsl -o $(SHADERS_OUT)/quad_vsh.dksh
	@uam -s frag $(SHADERS_DIR)/quad_fsh.glsl -o $(SHADERS_OUT)/quad_fsh.dksh

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf $(SHADERS_OUT)/*.dksh

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
	@cd $(TOPDIR) && elf2nro $(notdir $<) $(notdir $@) --romfsdir=$(ROMFS) --nacp=$(notdir $(OUTPUT).nacp)
	@echo built ... $(notdir $@)

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	:

#---------------------------------------------------------------------------------
%.o: %.c
	$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@

#---------------------------------------------------------------------------------
# Rules for linking
#---------------------------------------------------------------------------------
%.elf:
	$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@
	@echo built ... $(notdir $@)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
