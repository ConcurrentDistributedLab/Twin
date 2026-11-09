# TSL Queue with CCSynch Library Makefile

# ===== Compiler setup =====
CC ?= gcc
CXX ?= g++
CFLAGS ?= -pthread -std=c11 -O3 -Wall -Wextra -pedantic
CXXFLAGS ?= -pthread -std=c++11 -O3 -Wall -Wextra -pedantic
DEFINES ?= -DMINIMAL_GC -DMALLOC
LBLS ?= -lpthread -lm
GDB ?=
TARGET ?= tslqueue.out

# ===== Paths =====
TSL_PATH := .
CCSYNCH_PATH := CCSynchLibrary
CFILES_PATH := $(CCSYNCH_PATH)/CFiles
HEADER_PATH := $(CCSYNCH_PATH)/HeaderFiles

# ===== Sources =====
LOCAL_SRCS := $(wildcard $(TSL_PATH)/*.c)
LIB_SRCS := $(CFILES_PATH)/ccsynch.c $(CFILES_PATH)/uthreads.c
EXTRA_SRCS ?=
SRCS := $(LOCAL_SRCS) $(LIB_SRCS) $(EXTRA_SRCS)
HDRS := $(wildcard $(TSL_PATH)/*.h) $(wildcard $(HEADER_PATH)/*.h)

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

# ===== Rules =====
.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CCOMPILE) $(SRCS) -o $(TARGET) $(LBLS) $(LNUMA) $(GDB)

clean:
	rm -f $(TARGET) *.out *.o *.gch

run: $(TARGET)
	./$(TARGET)

debug:
	@echo "SRCS: $(SRCS)"
	@echo "HDRS: $(HDRS)"
	@echo "TARGET: $(TARGET)"
	@echo "INCLUDES: $(INCLUDES)"
	@echo "CCSYNCH_PATH: $(CCSYNCH_PATH)"

# ===== Additional compilation rules for individual files if needed =====
%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -c -o $@ $< $(GDB)