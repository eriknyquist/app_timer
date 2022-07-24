OUTPUT_DIR := build
OBJ_DIR := $(OUTPUT_DIR)/obj

ifeq ($(OS),Windows_NT)
# Windows-specific vars; if you did a custom install of MinGW, or if you are using some
# other compiler, then you might need to change some of these values
    #BIN_DIR := C:/MinGW/bin
    BIN_DIR := C:/msys64/mingw64/bin
    GCC := $(BIN_DIR)/gcc
    LD := $(BIN_DIR)/ld
    MKDIR := mkdir -p
    RMDIR := rm -rf
else
    ifeq ($(shell uname -s),Linux)
# Linux-specific vars
        GCC := gcc
        LD := ld
        MKDIR := mkdir -p
        RMDIR := rm -rf
    endif
endif

SRC_DIRS := ../.. ./hw_model
VPATH := $(SRC_DIRS) . examples

COMMON_SRC_FILES := $(foreach DIR,$(SRC_DIRS),$(wildcard $(DIR)/*.c))
TEST_SRC_FILES := $(COMMON_SRC_FILES) examples/test_main.c
EXAMPLE_SRC_FILES := $(COMMON_SRC_FILES) examples/example_main.c
TEST_OBJ_FILES := $(patsubst %.c,%.o,$(addprefix $(OBJ_DIR)/,$(notdir $(TEST_SRC_FILES))))
EXAMPLE_OBJ_FILES := $(patsubst %.c,%.o,$(addprefix $(OBJ_DIR)/,$(notdir $(EXAMPLE_SRC_FILES))))

INCLUDES := $(addprefix -I, $(SRC_DIRS))

# app_timer build options
OPTS := APP_TIMER_RUNNING_COUNT_UINT64
OPTS += APP_TIMER_STATS_ENABLE
OPTS += APP_TIMER_FREERUNNING_COUNTER
OPTS += APP_TIMER_RECONFIG_WITHOUT_STOPPING

FLAGS := -std=c99 -Wall $(addprefix -D,$(OPTS))
CFLAGS := $(FLAGS) $(INCLUDES)

EXAMPLE_PROG := $(OUTPUT_DIR)/example_main
TEST_PROG := $(OUTPUT_DIR)/test_main

.PHONY: clean output_dir

default: all

all: example test

example: CFLAGS += -O2
example: $(EXAMPLE_PROG)

test: CFLAGS += -O2
test: $(TEST_PROG)

debug: CFLAGS += -O0 -g
debug: $(TEST_PROG)

$(EXAMPLE_PROG): output_dir $(EXAMPLE_OBJ_FILES)
	$(GCC) $(LFLAGS) $(EXAMPLE_OBJ_FILES) -o $@

$(TEST_PROG): output_dir $(TEST_OBJ_FILES)
	$(GCC) $(LFLAGS) $(TEST_OBJ_FILES) -o $@

$(OBJ_DIR)/%.o: %.c
	$(GCC) $(CFLAGS) -c -o $@ $<

output_dir:
	@$(MKDIR) $(OUTPUT_DIR)
	@$(MKDIR) $(OBJ_DIR)

clean:
	@$(RMDIR) $(OUTPUT_DIR)
	@echo "Outputs removed"
