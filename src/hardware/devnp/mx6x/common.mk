ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

include ../../../prodroot_pkt.mk
IOPKT_ROOT=../../../../..

EXTRA_INCVPATH+= $(IOPKT_ROOT)/sys $(IOPKT_ROOT)/sys/sys-nto $(IOPKT_ROOT)/lib/socket/public

LIBS = drvrS cacheS

NAME = devnp-$(PROJECT)

#treat warning as error
CCFLAGS+=	-Werror

USEFILE=$(PROJECT_ROOT)/$(NAME).use

define PINFO
PINFO DESCRIPTION=Freescale i.mx 6x ENET driver
endef

include $(MKFILES_ROOT)/qtargets.mk
-include $(PROJECT_ROOT)/roots.mk
#####AUTO-GENERATED by packaging script... do not checkin#####
   INSTALL_ROOT_nto = $(PROJECT_ROOT)/../../../../install
   USE_INSTALL_ROOT=1
##############################################################

