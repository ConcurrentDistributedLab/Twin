# ===== Compiler setup =====
CC      ?= gcc
CXX     ?= g++

CFLAGS  ?= -Wall -Wextra -pedantic -O3
CXXFLAGS?= -pthread -std=c++11 -O3
DEFINES ?=
LBLS    ?= -lpthread -lm -lrt
GDB     ?=

# ===== Paths =====
BST_PATH   := .

# ===== Sources =====
LOCAL_SRCS  := $(wildcard $(BST_PATH)/*.cpp)
EXTRA_SRCS  ?=
HDRS        := $(wildcard *.h config/*.h)

SRCS        := $(LOCAL_SRCS) $(EXTRA_SRCS) $(HDRS)

# ===== Targets =====
TARGET      ?= bst.out


# ===== Compile commands (abstractions) =====
ifeq ($(COMPILER),gcc)
    CCOMPILE := $(CC)  $(CFLAGS) $(DEFINES)
else
    CCOMPILE := $(CXX) $(CXXFLAGS) $(DEFINES)
endif

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CCOMPILE) $(SRCS) -o $(TARGET) $(LBLS) $(GDB)

clean:
	rm -f $(TARGET) *.gch config/*.gch *.o *.out

run: $(TARGET)
	./$(TARGET)

debug:
	@echo "SRCS: $(SRCS)"
	@echo "EXTRA_SRCS: $(EXTRA_SRCS)"
	@echo "HDRS: $(HDRS)"
	@echo "TARGET: $(TARGET)"
	@echo "COMPILER: $(COMPILER)"
	@echo "CCOMPILE: $(CCOMPILE)"

.PHONY: all clean run debug