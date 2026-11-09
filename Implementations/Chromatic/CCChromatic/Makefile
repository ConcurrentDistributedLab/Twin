# ===== Compiler setup =====
CC = g++
CXXFLAGS = -pthread -std=c++11 -Wall -Wextra -pedantic -O3

# ===== Paths =====
CHROMATIC_PATH := .
CCSYNCH_PATH := CCSynchLibrary

# ===== Sources =====
CHROMATIC_SRCS = $(wildcard *.cpp)
CHROMATIC_HDRS = $(wildcard *.h config/*.h)
CCSYNCH_SRCS = $(CCSYNCH_PATH)/CFiles/ccsynch.c $(CCSYNCH_PATH)/CFiles/uthreads.c
CCSYNCH_HDRS = $(CCSYNCH_PATH)/HeaderFiles/*.h

LOCAL_SRCS  := $(CHROMATIC_SRCS) $(CHROMATIC_HDRS)
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

TARGET = chromatic.out

# ===== VPATH =====
VPATH := $(CHROMATIC_PATH) $(LPATH)/CFiles

# ===== Rules =====
.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CCOMPILE) -o $@ *.o $(LBLS) $(LNUMA) $(GDB)

%.o: %.c $(DEPS)
	$(CCOMPILE) -c -o $@ $< $(GDB)

$(CHROMATIC_PATH)/%.o: $(CHROMATIC_PATH)/%.c $(DEPS)
	$(CCOMPILE) -c -o $(notdir $@) $< $(GDB)

# ===== Clean =====
clean:
	rm -f $(TARGET) *.o *.gch config/*.gch *.out

run: $(TARGET)
	./$(TARGET)

debug:
	@echo "CHROMATIC_SRCS: $(CHROMATIC_SRCS)"
	@echo "CHROMATIC_HDRS: $(CHROMATIC_HDRS)"
	@echo "CCSYNCH_SRCS: $(CCSYNCH_SRCS)"
	@echo "CCSYNCH_HDRS: $(CCSYNCH_HDRS)"
	@echo "OBJS: $(OBJS)"
	@echo "TARGET: $(TARGET)"
	@echo "OS: $(OS)"
	@echo "NUMA_SUPPORT: $(NUMA_SUPPORT)"