OUTPUT      := $(shell pwd)/../../
BUILD_DIR	:= $(shell pwd)/../inc/

include $(BUILD_DIR)/linux.inc

CC	= $(CROSS_PREFIX)gcc
CXX	= $(CROSS_PREFIX)g++
LD	= $(CROSS_PREFIX)ld
AR	= $(CROSS_PREFIX)ar

include $(BUILD_DIR)/color.inc

LIBNAME		= libmacro
TGT_LIB_H	= $(LIBNAME).h
TGT_LIB_H	+= kernel_list.h
TGT_LIB_A	= $(LIBNAME).a

OBJS_LIB	= $(LIBNAME).o

CFLAGS	:= -g -Wall -Werror -fPIC
CFLAGS	+= -I$(OUTPUT)/include

LDFLAGS	+= -pthread

.PHONY : all clean

TGT	:= $(TGT_LIB_A)
OBJS	:= $(OBJS_LIB) 

all: $(TGT)

%.o:%.c
	$(CC_V) -c $(CFLAGS) $< -o $@

$(TGT_LIB_A): $(OBJS_LIB)
	$(AR_V) rcs $@ $^

clean:
	$(RM_V) -f $(OBJS)
	$(RM_V) -f $(TGT)

install:
	$(CP_V) -r $(TGT_LIB_H)  $(OUTPUT)/include
	$(CP_V) -r $(TGT_LIB_A)  $(OUTPUT)/lib

uninstall:
	$(RM_V) -f $(OUTPUT)/include/$(TGT_LIB_H)
	$(RM_V) -f $(OUTPUT)/lib/$(TGT_LIB_A)
