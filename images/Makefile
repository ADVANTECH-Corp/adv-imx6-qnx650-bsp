ROOT_DIR := $(notdir $(CURDIR))
ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

#weilun@adv - begin
include ../adv.mk
#weilun@adv - end

HOST_MKIFS := mkifs

SUFFIXES := .build .ifs .raw

.PHONY: all clean

all: ifs-mx6q-sabresmart.raw

clean: 
	$(RM_HOST) ifs-mx6q-sabresmart.raw ipl-mx6q-sabresmart.bin 

#ifs-mx6q-sabresmart.raw: sabresmart.build
#	$(HOST_MKIFS) -vvv -r../install $(MKIFSFLAGS) $^ $@

#weilun@adv - begin
ifeq ($(CONFIG_ADV_BOARD),rom7420)
ifs-mx6q-sabresmart.raw: sabresmart.rom7420.build
endif
ifeq ($(CONFIG_ADV_BOARD),rom5420)
ifs-mx6q-sabresmart.raw: sabresmart.rom5420.build
endif
ifeq ($(CONFIG_ADV_BOARD),rsb4410)
ifs-mx6q-sabresmart.raw: sabresmart.rsb4410.build
endif
#weilun@adv - end
	$(HOST_MKIFS) -vvv -r../install $(MKIFSFLAGS) $^ $@

