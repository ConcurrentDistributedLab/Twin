CC = g++
CXXFLAGS = -pthread -std=c++11 -Wall -Wextra -pedantic -O3

# ===== Paths =====
BST_DS_PATH := .
CCSYNCH_PATH := CCSynchLibrary

# ===== Sources =====
BST_SRCS = $(wildcard *.cpp)
BST_HDRS = $(wildcard *.h config/*.h)
CCSYNCH_SRCS = $(CCSYNCH_PATH)/CFiles/ccsynch.c $(CCSYNCH_PATH)/CFiles/uthreads.c
CCSYNCH_HDRS = $(CCSYNCH_PATH)/HeaderFiles/*.h
 
LOCAL_SRCS  := $(BST_SRCS) $(BST_HDRS)
EXTRA_SRCS  ?=
LIB_SRCS    := $(CCSYNCH_SRCS) $(CCSYNCH_HDRS)

SRCS        := $(LOCAL_SRCS) $(EXTRA_SRCS) $(LIB_SRCS)
OBJS        := $(SRCS:.c=.o)

# ===== Compiler Flags =====
SFLAGS = -DINTEL
DEFINES = -DMINIMAL_GC
NUMA = 
LBLS = -lpthread -lm -lrt
GDB = 

# ===== OS detection =====
OS := $(shell uname -s)
ifeq ($(OS),Linux)
    SFLAGS += -DCACHE_LINE_SIZE=`getconf LEVEL1_DCACHE_LINESIZE`
endif
ifeq ($(OS),Darwin)
    SFLAGS += -DCACHE_LINE_SIZE=`sysctl -n hw.cachelinesize`
endif

# ===== NUMA support =====
NUMA_SUPPORT ?= true
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

TARGET = bst.out

# ===== VPATH =====
VPATH := $(BST_DS_PATH) $(LPATH)/CFiles

# ===== Rules =====
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CCOMPILE) -o $@ *.o $(LBLS) $(LNUMA) $(GDB)

%.o: %.c $(DEPS)
	$(CCOMPILE) -c -o $@ $< $(GDB)

$(BST_DS_PATH)/%.o: $(BST_DS_PATH)/%.c $(DEPS)
	$(CCOMPILE) -c -o $(notdir $@) $< $(GDB)

# ===== Clean =====
clean:
	rm -f *.o *.out