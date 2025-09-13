# Makefile for building and running a UEFI Application.
# Version 5: Uses granular sudo and robust lazy unmounting.

.PHONY: all run image compile clean
.DEFAULT_GOAL = all

# --- Configuration ---

# Directories
BUILD_DIR      := bin
SRC_DIR        := src
OBJ_DIR        := $(BUILD_DIR)/obj
EFI_SOURCE_DIR := $(BUILD_DIR)/source_efi
MOUNT_POINT    := $(BUILD_DIR)/mount_point

# Source and Target Files (Auto-discovery of .c files)
SOURCES        := $(wildcard $(SRC_DIR)/*.c)
OBJECTS        := $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
TARGET_EFI     := $(EFI_SOURCE_DIR)/EFI/BOOT/BOOTX64.EFI
TARGET_IMAGE   := $(BUILD_DIR)/efi.img

# Image Properties
IMG_SIZE_MB    := 128

# Compiler and Flags
CC             := clang
TARGET_TRIPLE  := x86_64-unknown-windows

CFLAGS         := -target $(TARGET_TRIPLE) \
                  -std=c17 \
                  -Wall \
                  -Wextra \
                  -Wpedantic \
                  -mno-red-zone \
                  -ffreestanding \
                  -nostdlib

LDFLAGS        := -target $(TARGET_TRIPLE) \
                  -fuse-ld=lld-link \
                  -Wl,-subsystem:efi_application \
                  -Wl,-entry:efi_main \
                  -nostdlib

# QEMU Configuration
QEMU           := qemu-system-x86_64
OVMF_PATH      := $(CURDIR)/firmware/bios64.bin
QEMU_FLAGS     := -drive format=raw,unit=0,file=$(TARGET_IMAGE) \
                  -bios $(OVMF_PATH) \
                  -m 256M \
                  -vga std \
                  -name "UEFI BOOT TEST" \
                  -machine q35 \
                  -net none

# --- Build Rules ---

all: $(TARGET_IMAGE)

run: all
	@if [ ! -f "$(OVMF_PATH)" ]; then \
		echo "Error: OVMF file not found at '$(OVMF_PATH)'."; \
		exit 1; \
	fi
	@$(QEMU) $(QEMU_FLAGS)

image: $(TARGET_IMAGE)

$(TARGET_IMAGE): $(TARGET_EFI)
	mkdir -p $(BUILD_DIR)
	dd if=/dev/zero of="$@" bs=1M count=$(IMG_SIZE_MB) status=progress >&2
	sudo sgdisk --zap-all --new=1:0:0 --typecode=1:ef00 "$@" > /dev/null
	$(SHELL) -ec ' \
		LOOP_DEVICE=$$(sudo losetup --find --show --partscan "$@"); \
		if [ -z "$$LOOP_DEVICE" ]; then echo "Error: Failed to set up loop device." >&2; exit 1; fi; \
		trap "echo '\''Cleaning up mount and loop device...'\''; sudo umount -l \"$(MOUNT_POINT)\" 2>/dev/null || true; sudo losetup --detach \"$$LOOP_DEVICE\" 2>/dev/null || true; rm -rf \"$(MOUNT_POINT)\";" EXIT; \
		PARTITION="$${LOOP_DEVICE}p1"; \
		sleep 1; \
		sudo mkfs.fat -F 32 "$$PARTITION" > /dev/null; \
		mkdir -p "$(MOUNT_POINT)"; \
		sudo mount "$$PARTITION" "$(MOUNT_POINT)"; \
		sudo cp -r "$(EFI_SOURCE_DIR)"/* "$(MOUNT_POINT)/"; \
		sudo sync; \
	'

compile: $(TARGET_EFI)

$(TARGET_EFI): $(OBJECTS)
	mkdir -p $(shell dirname $@)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	if [ -d "$(BUILD_DIR)" ]; then \
		rm -rf $(OBJ_DIR) $(EFI_SOURCE_DIR) $(MOUNT_POINT) || \
		(echo "Permission denied. Retrying with sudo..." && sudo rm -rf $(OBJ_DIR) $(EFI_SOURCE_DIR) $(MOUNT_POINT)); \
	fi