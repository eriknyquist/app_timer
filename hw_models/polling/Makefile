OUTPUT_DIR := build
OBJ_DIR := $(OUTPUT_DIR)/obj

BIN_DIR := C:/MinGW/bin
GCC := $(BIN_DIR)/gcc
LD := $(BIN_DIR)/ld
MKDIR := mkdir -p
RMDIR := rm -rf

SRC_DIRS := ../.. ./hw_model .
VPATH := $(SRC_DIRS)

SRC_FILES := $(foreach DIR,$(SRC_DIRS),$(wildcard $(DIR)/*.c))
OBJ_FILES := $(patsubst %.c,%.o,$(addprefix $(OBJ_DIR)/,$(notdir $(SRC_FILES))))
PROGNAME := polling_app_timer
BUILD_OUTPUT := $(OUTPUT_DIR)/$(PROGNAME)

INCLUDES := $(addprefix -I, $(SRC_DIRS))
FLAGS := -Wall -O3 -DAPP_TIMER_RUNNING_COUNT_UINT64
CFLAGS := $(FLAGS) $(INCLUDES)

.PHONY: clean output_dir

all: $(BUILD_OUTPUT)

$(BUILD_OUTPUT): output_dir $(OBJ_FILES)
	$(GCC) $(LFLAGS) $(OBJ_FILES) -o $@

$(OBJ_DIR)/%.o: %.c
	$(GCC) $(CFLAGS) -c -o $@ $<

output_dir:
	@$(MKDIR) $(OUTPUT_DIR)
	@$(MKDIR) $(OBJ_DIR)

clean:
	@$(RMDIR) $(OUTPUT_DIR)
	@echo "Outputs removed"
