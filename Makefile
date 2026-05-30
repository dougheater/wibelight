#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

#-------------------------------------------------------------------------------
# APP_NAME sets the long name of the application
# APP_SHORTNAME sets the short name of the application
# APP_AUTHOR sets the author of the application
#-------------------------------------------------------------------------------
APP_NAME	:= wibelight
APP_SHORTNAME	:= wibelight
APP_AUTHOR	:= doughEATER

include $(DEVKITPRO)/wut/share/wut_rules

#-------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#-------------------------------------------------------------------------------
TARGET		:=	wibelight
VERSION		:=	1.0.1
BUILD		:=	build
SOURCES		:=	src \
				src/wiiu \
				src/libgamestream \
				third_party/cJSON \
				third_party/moonlight-common-c/src \
				third_party/moonlight-common-c/reedsolomon \
				third_party/moonlight-common-c/enet \
				third_party/h264bitstream \
				\
				
DATA		:=	data
INCLUDES	:=	src \
				src/wiiu \
				src/libgamestream \
				third_party/cJSON \
				third_party/moonlight-common-c/src \
				third_party/moonlight-common-c/reedsolomon \
				third_party/moonlight-common-c/enet/include \
				third_party/h264bitstream \
				\
				
SOURCE_FILES	:=	
CONTENT		:=
ICON		:=	resource/iconTex.png
TV_SPLASH	:=	resource/bootTvTex.png
DRC_SPLASH	:=	resource/bootDrcTex.png

#-------------------------------------------------------------------------------
# options for code generation
#-------------------------------------------------------------------------------
CFLAGS	:=	-O3 -ffunction-sections -fdata-sections \
			-Wall \
			$(MACHDEP)

CFLAGS	+=	$(INCLUDE) -D__WIIU__ -D__WUT__ -DBIGENDIAN -DUSE_MBEDTLS -DAPP_VERSION=\"$(VERSION)\"

CXXFLAGS	:= $(CFLAGS)

ASFLAGS	:=	$(ARCH)
LDFLAGS	=	$(ARCH) $(RPXSPECS) -Wl,-Map,$(TARGET).map

LIBS	:= -lfreetype -lpng -lbz2 -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lSDL2 -lopus -lexpat -lz -lbrotlidec -lbrotlicommon -lwut -lm

#-------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level
# containing include and lib
#-------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(WUT_ROOT)


#-------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#-------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#-------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/dist/wiiu/apps/wibelight/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(foreach sf,$(SOURCE_FILES),$(CURDIR)/$(dir $(sf)))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))) \
			$(foreach f,$(SOURCE_FILES),$(notdir $(f)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#-------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#-------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#-------------------------------------------------------------------------------
	export LD	:=	$(CC)
#-------------------------------------------------------------------------------
else
#-------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#-------------------------------------------------------------------------------
endif
#-------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) -I$(DEVKITPRO)/portlibs/ppc/include/freetype2

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)



ifneq (,$(strip $(CONTENT)))
	export APP_CONTENT := $(TOPDIR)/$(CONTENT)
endif

ifneq (,$(strip $(ICON)))
	export APP_ICON := $(TOPDIR)/$(ICON)
else ifneq (,$(wildcard $(TOPDIR)/$(TARGET).png))
	export APP_ICON := $(TOPDIR)/$(TARGET).png
else ifneq (,$(wildcard $(TOPDIR)/icon.png))
	export APP_ICON := $(TOPDIR)/icon.png
endif

ifneq (,$(strip $(TV_SPLASH)))
	export APP_TV_SPLASH := $(TOPDIR)/$(TV_SPLASH)
else ifneq (,$(wildcard $(TOPDIR)/tv-splash.png))
	export APP_TV_SPLASH := $(TOPDIR)/tv-splash.png
else ifneq (,$(wildcard $(TOPDIR)/splash.png))
	export APP_TV_SPLASH := $(TOPDIR)/splash.png
endif

ifneq (,$(strip $(DRC_SPLASH)))
	export APP_DRC_SPLASH := $(TOPDIR)/$(DRC_SPLASH)
else ifneq (,$(wildcard $(TOPDIR)/drc-splash.png))
	export APP_DRC_SPLASH := $(TOPDIR)/drc-splash.png
else ifneq (,$(wildcard $(TOPDIR)/splash.png))
	export APP_DRC_SPLASH := $(TOPDIR)/splash.png
endif

.PHONY: $(BUILD) clean all dist

#-------------------------------------------------------------------------------
all: $(BUILD)

dist: all
	@echo 'dist built ... done'

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@mkdir -p dist/wiiu/apps/wibelight/
	@sed -e "s/@VERSION@/$(VERSION)/g" -e "s/@RELEASE_DATE@/`date -u +%Y%m%d%H%M%S`/g" $(TOPDIR)/meta.xml.template > dist/wiiu/apps/wibelight/meta.xml
	@if [ ! -f $(CURDIR)/src/wiiu/logo_bin.h ]; then python3 $(CURDIR)/src/build_logo.py $(CURDIR)/resource/logoTex.png $(CURDIR)/src/wiiu/logo_bin.h; fi
	@if [ ! -f $(CURDIR)/src/wiiu/logo_sm_bin.h ]; then python3 $(CURDIR)/src/build_logo.py $(CURDIR)/resource/logoTex_sm.png $(CURDIR)/src/wiiu/logo_sm_bin.h.tmp && sed 's/logo_width/logo_sm_width/g; s/logo_height/logo_sm_height/g; s/logo_rgba/logo_sm_rgba/g; s/LOGO_WIDTH/LOGO_SM_WIDTH/g; s/LOGO_HEIGHT/LOGO_SM_HEIGHT/g' $(CURDIR)/src/wiiu/logo_sm_bin.h.tmp > $(CURDIR)/src/wiiu/logo_sm_bin.h && rm $(CURDIR)/src/wiiu/logo_sm_bin.h.tmp; fi
	@cp $(CURDIR)/resource/icon.png dist/wiiu/apps/wibelight/icon.png
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#-------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) dist/

#-------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#-------------------------------------------------------------------------------
# main targets
#-------------------------------------------------------------------------------
all	:	$(OUTPUT).wuhb

$(OUTPUT).wuhb	:	$(OUTPUT).rpx
$(OUTPUT).rpx	:	$(OUTPUT).elf
	@elf2rpl $< $@
	@rm -f $(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#-------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#-------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#-------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#-------------------------------------------------------------------------------
endif
#-------------------------------------------------------------------------------
