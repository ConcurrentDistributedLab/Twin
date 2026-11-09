CC	:= g++

TEST_SRCS := $(wildcard src/*.c src/core/*.c src/test_framework/*.c src/test_framework/tests/*.c Library/Pinning/*.c Library/Random/*.c Library/Timer/*.c)
TWIN_SRCS_RELATIVE := $(addprefix ../../../, $(TEST_SRCS))
CHROMATIC_SRCS_RELATIVE := $(addprefix ../../../, $(TEST_SRCS))
BST_SRCS_RELATIVE := $(addprefix ../../../, $(TEST_SRCS))
TSL_SRCS_RELATIVE := $(addprefix ../../, $(TEST_SRCS))
LJ_SRCS_RELATIVE := $(addprefix ../../, $(TEST_SRCS))

TWIN_LIST_PATH = Implementations/Twin/Twin_List

CHROMATIC_PATH = Implementations/Chromatic/Chromatic
CCCHROMATIC_PATH = Implementations/Chromatic/CCChromatic

BST_PATH = Implementations/Bst/Bst
CCBST_PATH = Implementations/Bst/CCBst

LJ_PATH = Implementations/LJ
TWIN_LJ_PATH = Implementations/Twin/Twin_LJ

TSL_PATH = Implementations/Tsl
TWIN_TSL_PATH = Implementations/Twin/Twin_Tsl

SDBG = false
SSTATS = true
SNUMA_SUPPORT = false
STEST = true

DEFINES = 
CFLAGS = -Wall -Wextra -pedantic -O3
LBLS = -lpthread -lm -lnuma -lrt
GDB =
#-g
STATS = -DCOMPUTE_STATS -DCOMPUTE_AVG -DCOMPUTE_HEIGHT -DCOMPUTE_ATOMIC 
# -DCOMPUTE_HW_EVENTS
PINNING = 
# -DPIN_TITAN

.PHONY = clean clean_folders rm

TWIN = Twin_List.out
CHROMATIC = Chromatic.out CCChromatic.out
BST = Bst.out CCBst.out
LINDEN = LJ.out Twin_LJ.out
TSL = Tsl.out Twin_Tsl.out

ALGS = $(TWIN) $(CHROMATIC) $(BST) $(LINDEN) $(TSL)

all: $(ALGS)

######################## ALGORITHMS ########################

Twin_List.out:
	$(MAKE) -C $(TWIN_LIST_PATH) all \
		COMPILER=$(CC) \
		SFLAGS='-DINTEL -Wall -DCACHE_LINE_SIZE=64' \
		DEFINES='-DCCSYNCH -DTWIN_LIST $(STATS) $(PINNING) -DMINIMAL_GC' \
		NUMA_SUPPORT='$(SNUMA_SUPPORT)' \
		GDB='$(GDB)' \
		TARGET=frx_l.out \
		OBJ_DIR='$(TWIN_LIST_PATH)' \
		EXTRA_SRCS='$(TWIN_SRCS_RELATIVE)'
	cp $(TWIN_LIST_PATH)/frx_l.out ./
	mv frx_l.out $@


Chromatic.out:
	$(MAKE) -C $(CHROMATIC_PATH) all \
		COMPILER=$(CC) \
		DEFINES='-DCHROMATIC -DNGC -DNO_FREE $(STATS) $(PINNING)' \
		NUMA_SUPPORT='$(SNUMA_SUPPORT)' \
		GDB='$(GDB)' \
		TARGET=chromatic.out \
		OBJ_DIR='$(CHROMATIC_PATH)' \
		EXTRA_SRCS='$(CHROMATIC_SRCS_RELATIVE)'
	cp $(CHROMATIC_PATH)/chromatic.out ./
	mv chromatic.out $@


CCChromatic.out:
	$(MAKE) -C $(CCCHROMATIC_PATH) all \
		COMPILER=$(CC) \
		SFLAGS='-DINTEL -Wall -DCACHE_LINE_SIZE=64' \
		DEFINES='-DCCSYNCH -DCCCHROMATIC $(STATS) $(PINNING) -DMINIMAL_GC' \
		NUMA_SUPPORT='$(SNUMA_SUPPORT)' \
		GDB='$(GDB)' \
		TARGET=chromatic.out \
		OBJ_DIR='$(CCCHROMATIC_PATH)' \
		EXTRA_SRCS='$(CHROMATIC_SRCS_RELATIVE)'
	cp $(CCCHROMATIC_PATH)/chromatic.out ./
	mv chromatic.out $@


Bst.out:
	$(MAKE) -C $(BST_PATH) all CC=$(CC) SRCS='$(BST_SRCS_RELATIVE)' DEFINES='-DBST -DNGC -DNO_FREE $(STATS) $(PINNING)' GDB='$(GDB)' ; \
	cp $(BST_PATH)/bst.out  ./ ; \
	mv bst.out $@


CCBst.out:
	$(MAKE) -C $(CCBST_PATH) all \
		COMPILER=$(CC) \
		SFLAGS='-DINTEL -Wall -DCACHE_LINE_SIZE=64' \
		DEFINES='-DCCSYNCH -DCCBST $(STATS) $(PINNING) -DMINIMAL_GC' \
		NUMA_SUPPORT='$(SNUMA_SUPPORT)' \
		GDB='$(GDB)' \
		TARGET=bst.out \
		OBJ_DIR='$(CCBST_PATH)' \
		EXTRA_SRCS='$(BST_SRCS_RELATIVE)'
	cp $(CCBST_PATH)/bst.out ./
	mv bst.out $@


LJ.out:
	$(MAKE) -C $(LJ_PATH) all CC=$(CC) EXTRA_SRCS='$(LJ_SRCS_RELATIVE)' DEFINES='-DLJ -DMINIMAL_GC $(STATS) $(PINNING)' GDB='$(GDB)' ; \
	cp $(LJ_PATH)/priorityq.out  ./ ; \
	mv priorityq.out $@


Twin_LJ.out:
	$(MAKE) -C $(TWIN_LJ_PATH) clean ; \
	$(MAKE) -C $(TWIN_LJ_PATH) all \
		COMPILER=$(CC) \
		SFLAGS='-DINTEL -Wall -DCACHE_LINE_SIZE=64' \
		DEFINES='-DCCSYNCH -DTWIN_LJ $(STATS) $(PINNING) -DMINIMAL_GC' \
		NUMA_SUPPORT='$(SNUMA_SUPPORT)' \
		GDB='$(GDB)' \
		TARGET=frx_linden.out \
		OBJ_DIR='$(TWIN_LJ_PATH)' \
		EXTRA_SRCS='$(TWIN_SRCS_RELATIVE)'
	cp $(TWIN_LJ_PATH)/frx_linden.out ./ ; \
	mv frx_linden.out $@


Tsl.out:
	$(MAKE) -C $(TSL_PATH) all CC=$(CC) EXTRA_SRCS='$(TSL_SRCS_RELATIVE)' DEFINES='-DTSL $(STATS) $(PINNING)' GDB='$(GDB)' ; \
	cp $(TSL_PATH)/tslqueue.out  ./ ; \
	mv tslqueue.out $@


Twin_Tsl.out:
	$(MAKE) -C $(TWIN_TSL_PATH) all \
		COMPILER=$(CC) \
		SFLAGS='-DINTEL -Wall -DCACHE_LINE_SIZE=64' \
		DEFINES='-DCCSYNCH -DTWIN_TSL $(STATS) $(PINNING) -DMINIMAL_GC' \
		NUMA_SUPPORT='$(SNUMA_SUPPORT)' \
		GDB='$(GDB)' \
		TARGET=tsl.out \
		OBJ_DIR='$(TWIN_TSL_PATH)' \
		EXTRA_SRCS='$(TWIN_SRCS_RELATIVE)'
	cp $(TWIN_TSL_PATH)/tsl.out ./
	mv tsl.out $@

######################## CLEAN ########################

clean:
	$(MAKE) clean_folders
	rm -f $(ALGS)

clean_folders:
	$(MAKE) -C $(TWIN_LIST_PATH) clean
	$(MAKE) -C $(CHROMATIC_PATH) clean
	$(MAKE) -C $(CCCHROMATIC_PATH) clean
	$(MAKE) -C $(BST_PATH) clean
	$(MAKE) -C $(CCBST_PATH) clean
	$(MAKE) -C $(LJ_PATH) clean
	$(MAKE) -C $(TWIN_LJ_PATH) clean
	$(MAKE) -C $(TSL_PATH) clean
	$(MAKE) -C $(TWIN_TSL_PATH) clean

rm:
	rm -f *.out
