
#weilun@adv - end

#EXTRA_SRCVPATH += $(PROJECT_ROOT)/../codecs/wm8962/
#EXTRA_INCVPATH += $(PRODUCT_ROOT)/../codecs/wm8962/

EXTRA_SRCVPATH += $(PROJECT_ROOT)/../codecs/sgtl5000/
EXTRA_INCVPATH += $(PRODUCT_ROOT)/../codecs/sgtl5000/

#weilun@adv - end

SDMA_LIB=dma-sdma-imx6x
LIBS_mx += $(SDMA_LIB)

include ../../../../common.mk

#weilun@adv - begin
include ../../../../../../../../adv.mk
ifeq ($(CONFIG_ADV_BOARD), rom7420)
CCFLAGS += -DCONFIG_MACH_MX6Q_ROM_7420
endif
ifeq ($(CONFIG_ADV_BOARD), rom5420)
CCFLAGS += -DCONFIG_MACH_MX6Q_ROM_5420
endif
ifeq ($(CONFIG_ADV_BOARD), rom5420)
CCFLAGS += -DCONFIG_MACH_MX6Q_RSB_4410
endif
#weilun@adv - begin
