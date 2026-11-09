# ===== Compiler setup =====
CC      ?= gcc
CXX     ?= g++

CFLAGS  ?= -Wall -Wextra -pedantic -O3
CXXFLAGS?= -Wall -Wextra -pedantic -O3
DEFINES ?=
LBLS    ?= -lpthread -lm -lrt
GDB     ?=

# ===== System-specific settings =====
OS := $(shell uname -s)
ifeq ($(OS),Linux)
    DEFINES += -DCACHE_LINE_SIZE=`getconf LEVEL1_DCACHE_LINESIZE`
endif
ifeq ($(OS),Darwin)
    DEFINES += -DCACHE_LINE_SIZE=`sysctl -n hw.cachelinesize`
endif

# ===== Paths =====
LINDEN_PATH   := .

# ===== Sources =====
LOCAL_SRCS  := $(wildcard $(LINDEN_PATH)/*.c)
EXTRA_SRCS  ?=
HDRS        := $(wildcard *.h)

SRCS        := $(LOCAL_SRCS) $(EXTRA_SRCS) $(HDRS)

# ===== Targets =====
TARGET      ?= priorityq.out

# ===== Compile commands =====
ifeq ($(COMPILER),gcc)
    CCOMPILE := $(CC) $(CFLAGS) $(DEFINES) -DINTEL
else
    CCOMPILE := $(CXX) $(CXXFLAGS) $(DEFINES) -DINTEL
endif

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CCOMPILE) $(SRCS) -o $(TARGET) $(LBLS) $(GDB)

clean:
	rm -f $(TARGET) *.gch *.o *.out

run: $(TARGET)
	./$(TARGET)

debug:
	@echo "SRCS: $(SRCS)"
	@echo "EXTRA_SRCS: $(EXTRA_SRCS)"
	@echo "HDRS: $(HDRS)"
	@echo "TARGET: $(TARGET)"
	@echo "COMPILER: $(COMPILER)"
	@echo "CCOMPILE: $(CCOMPILE)"
	@echo "DEFINES: $(DEFINES)"
	@echo "OS: $(OS)"

.PHONY: all clean run debug