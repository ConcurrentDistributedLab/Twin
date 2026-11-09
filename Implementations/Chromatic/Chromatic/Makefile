# ===== Compiler setup =====
CC      ?= gcc
CXX     ?= g++

CFLAGS  ?= -Wall -Wextra -pedantic -O3
CXXFLAGS?= -Wall -std=c++11 -O3
DEFINES ?=
LBLS    ?= -lpthread -lm
GDB     ?=

# ===== Paths =====
CHROMATIC_PATH   := .

# ===== Sources =====
LOCAL_SRCS  := $(wildcard $(CHROMATIC_PATH)/*.cpp)
EXTRA_SRCS  ?=
HDRS 		:= $(wildcard *.h config/*.h)

SRCS        := $(LOCAL_SRCS) $(EXTRA_SRCS) $(HDRS)

# ===== Targets =====
TARGET      ?= chromatic.out


# ===== Compile commands (abstractions) =====
ifeq ($(COMPILER),gcc)
	CCOMPILE := $(CC)  $(CFLAGS) $(DEFINES)
else
	CCOMPILE := $(CXX) $(CXXFLAGS) $(DEFINES)
endif

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CCOMPILE) $(DEFINES) $(SRCS) -o $(TARGET) $(LBLS) $(GDB)

clean:
	rm -f $(TARGET) *.gch config/*.gch *.out

run: $(TARGET)
	./$(TARGET)

debug:
	@echo "SRCS: $(SRCS)"
	@echo "EXTRA_SRCS: $(EXTRA_SRCS)"
	@echo "HDRS: $(HDRS)"
	@echo "TARGET: $(TARGET)"

.PHONY: all clean run debug