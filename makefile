.PHONY: all run image compile compile_utils clean
.DEFAULT_GOAL = all

# Directories
BUILD_DIR      	:= bin
INC_DIR			:= include
SRC_DIR        	:= src
UTILS_DIR		:= utils
UTILS_OUT_DIR	:= $(UTILS_DIR)/out
OBJ_DIR        	:= $(BUILD_DIR)/obj
EFI_SOURCE_DIR 	:= $(BUILD_DIR)/source_efi
FIRM_DIR		:= firmware

# Source and Target Files 
MOUNT_POINT    	:= $(BUILD_DIR)/mount_point
SOURCES        	:= $(wildcard $(SRC_DIR)/*.c)
OBJECTS        	:= $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
TARGET_EFI     	:= $(EFI_SOURCE_DIR)/EFI/BOOT/BOOTX64.EFI
TARGET_IMAGE   	:= $(BUILD_DIR)/efi.img
UTILS_SOURCES	:= $(wildcard $(UTILS_DIR)/*.c)
UTILS			:= $(UTILS_SOURCES:$(UTILS_DIR)/%.c=$(UTILS_OUT_DIR)/%)

# Image Properties
IMG_SCRIPT		:= $(UTILS_DIR)/make_efi_img.sh
IMG_SIZE_MB    	:= 128

# Compiler and Flags
CC             	:= clang
TARGET_TRIPLE  	:= x86_64-unknown-windows
CFLAGS         	:= -target $(TARGET_TRIPLE) \
					-std=c17 \
					-Wall \
					-Wextra \
					-Wpedantic \
					-mno-red-zone \
					-ffreestanding \
					-nostdlib
COMMON_FLAGS	:= -I$(INC_DIR)
LDFLAGS        	:= -target $(TARGET_TRIPLE) \
					-fuse-ld=lld-link \
					-Wl,-subsystem:efi_application \
					-Wl,-entry:efi_main \
					-nostdlib

# QEMU Configuration
QEMU           	:= qemu-system-x86_64
OVMF_PATH      	:= $(FIRM_DIR)/bios64.bin
QEMU_FLAGS     	:= -drive format=raw,unit=0,file=$(TARGET_IMAGE) \
					-bios $(OVMF_PATH) \
					-m 256M \
					-vga std \
					-name "UEFI BOOT TEST" \
					-machine q35 \
					-net none


all: $(TARGET_IMAGE)

run: all
	if [ ! -f "$(OVMF_PATH)" ]; then \
		echo "Error: OVMF file not found at '$(OVMF_PATH)'."; \
		exit 1; \
	fi
	$(QEMU) $(QEMU_FLAGS)

image: $(TARGET_IMAGE)

$(TARGET_IMAGE): $(TARGET_EFI)
	echo "--- Invoking Build Script to Create UEFI Image ---"
	$(IMG_SCRIPT) \
		"$(TARGET_IMAGE)" \
		"$(IMG_SIZE_MB)" \
		"$(EFI_SOURCE_DIR)" \
		"$(MOUNT_POINT)"

compile: $(TARGET_EFI)
utils: $(UTILS)

$(UTILS_OUT_DIR)/%: $(UTILS_DIR)/%.c
	mkdir -p $(UTILS_OUT_DIR)
	$(CC) $(COMMON_FLAGS) -o $@ $<

$(TARGET_EFI): $(OBJECTS)
	mkdir -p $(shell dirname $@)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(COMMON_FLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJ_DIR) $(EFI_SOURCE_DIR) $(BUILD_DIR) $(UTILS_OUT_DIR) || \
	(echo "Permission denied. Retrying with sudo..." && sudo rm -rf $(OBJ_DIR) $(EFI_SOURCE_DIR) $(BUILD_DIR) $(UTILS_OUT_DIR)); \