# ===== Compiler setup =====
CC      ?= gcc
CXX     ?= g++

CFLAGS  ?= -Wall -Wextra -pedantic -O3
CXXFLAGS?= -Wall -Wextra -pedantic -O3
SFLAGS  ?= -DINTEL -Wall
DEFINES ?= -DMINIMAL_GC -DMALLOC
LBLS    ?= -lpthread -lm
NUMA_SUPPORT ?= true
GDB     ?=

# ===== Paths =====
FRXL_PATH   := .
LPATH       := $(FRXL_PATH)/CCSynchLibrary

# ===== Sources =====
LOCAL_SRCS  := $(wildcard $(FRXL_PATH)/*.c)
EXTRA_SRCS  ?=
LIB_SRCS    := $(LPATH)/CFiles/ccsynch.c $(LPATH)/CFiles/uthreads.c

SRCS        := $(LOCAL_SRCS) $(EXTRA_SRCS) $(LIB_SRCS)
OBJS        := $(SRCS:.c=.o)

# ===== Targets =====
TARGET      ?= frx_linden.out
DEPS        := Makefile $(wildcard $(FRXL_PATH)/*.h $(LPATH)/HeaderFiles/*.h)

# ===== OS detection =====
OS := $(shell uname -s)
ifeq ($(OS),Linux)
    SFLAGS += -DCACHE_LINE_SIZE=`getconf LEVEL1_DCACHE_LINESIZE`
    LBLS   += -lrt -lnuma
endif
ifeq ($(OS),Darwin)
    SFLAGS += -DCACHE_LINE_SIZE=`sysctl -n hw.cachelinesize`
endif

# ===== NUMA support =====
ifeq ($(NUMA_SUPPORT),false)
    NUMA := -DDOES_NOT_SUPPORT_NUMA 
    $(info ***** NUMA SUPPORT DISABLED *****)
else
    LNUMA := -lnuma
    $(info ***** NUMA SUPPORT ENABLED *****)
endif

# ===== Compile commands (abstractions) =====
ifeq ($(COMPILER),gcc)
	CCOMPILE := $(CC)  $(CFLAGS) $(SFLAGS) $(DEFINES) $(NUMA)
else
	CCOMPILE := $(CXX) $(CXXFLAGS) $(SFLAGS) $(DEFINES) $(NUMA)
endif

# ===== VPATH =====
VPATH := $(FRXL_PATH) $(LPATH)/CFiles

# ===== Rules =====
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) prioq.o
	$(CCOMPILE) -o $@ *.o $(LBLS) $(LNUMA) $(GDB)

%.o: %.c $(DEPS)
	$(CCOMPILE) -c -o $@ $< $(GDB)

$(FRXL_PATH)/%.o: $(FRXL_PATH)/%.c $(DEPS)
	$(CCOMPILE) -c -o $(notdir $@) $< $(GDB)

# ===== Clean =====
clean:
	rm -f *.o *.out