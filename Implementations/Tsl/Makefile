# TSL Queue Makefile

CC ?= g++
CXXFLAGS ?= -pthread -std=c++11 -O3
DEFINES ?=
LBLS ?= -lpthread -lm -lnuma
GDB ?=
TARGET ?= tslqueue.out

SRCS ?= $(wildcard *.c)
EXTRA_SRCS ?=
HDRS ?= $(wildcard *.h)

all: $(TARGET)

$(TARGET): $(SRCS) $(EXTRA_SRCS) $(HDRS)
	$(CC) $(CXXFLAGS) $(DEFINES) $(SRCS) $(EXTRA_SRCS) -o $(TARGET) $(LBLS) $(GDB)

clean:
	rm -f $(TARGET) *.gch

run: $(TARGET)
	./$(TARGET)

debug:
	@echo "SRCS: $(SRCS)"
	@echo "EXTRA_SRCS: $(EXTRA_SRCS)"
	@echo "HDRS: $(HDRS)"
	@echo "TARGET: $(TARGET)"

.PHONY: all clean run debug