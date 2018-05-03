DIR_INC = ./include
DIR_LIB = ./lib
DIR_SRC = ./src
DIR_BIN = ./bin
BIN     = cpunode

EVAL_INC = ./include/libeval 
EVAL_LIB = ./lib/libeval/icc_libeval.a

INTEL_MKL_LIBS = /opt/intel/mkl/lib/intel64/libmkl_intel_lp64.a /opt/intel/mkl/lib/intel64/libmkl_sequential.a /opt/intel/mkl/lib/intel64/libmkl_core.a

include $(DIR_LIB)/inc/linux.inc

CC	= $(CROSS_PREFIX)icc
LD	= $(CROSS_PREFIX)ld
AR	= $(CROSS_PREFIX)ar

include $(DIR_LIB)/inc/color.inc

SRC  = $(wildcard ${DIR_SRC}/*.c)
OBJ  = $(patsubst %.c, %.o, ${SRC})

CFLAGS   = -g -I$(DIR_INC) -I${EVAL_INC} -Wno-deprecated-declarations
CFLAGS  += $(shell pkg-config --cflags jansson libevent)

LDFLAGS  = -L $(DIR_LIB) -lmacro -llog -lconfig -lpthread
LDFLAGS += ${EVAL_LIB} -Wl,--start-group $(INTEL_MKL_LIBS)
LDFLAGS += $(shell pkg-config --libs jansson libevent)

all : $(BIN)

%.o:%.c
	$(CC_V) -c $< $(CFLAGS) -o $@

%.o:%.cc
	$(CC_V) -c $< $(CFLAGS) -o $@

$(BIN) : $(OBJ)
	-$(CC_V) -o $(DIR_BIN)/$@ $^ $(CFLAGS) $(LDFLAGS)

.PHONY : clean libs
clean :
	-$(RM_V) -rf bin/* $(OBJ)

libs:
	cd lib/liblog && make clean all install
	cd lib/libconfig && make clean all install
	cd lib/libmacro && make clean all install

echo :
	@echo $(SRC)
	@echo $(OBJ)
