#
# Makefile
#

ifneq ($(wildcard ../config.mak),)
include ../config.mak
endif

obj-y := toip.o

VMAJOR = 1
VMINOR = 0
VRELEA = 0
NAME = toip
TYPE = exe
INCDIRS := -I. -I..
INCDIRS += -I../libzutil
INCDIRS += -I../libzts
INCDIRS += -I../libzlst
CFLAGS += $(INCDIRS)

LDFLAGS += -L../libzutil -lzutil
LDFLAGS += -L../libzbuddy -lzbuddy
LDFLAGS += -L../libzlst -lzlst
LDFLAGS += -L../libzts -lzts
ifeq ($(SYS),WINDOWS)
LDFLAGS += -lws2_32
endif

include ../common.mak
