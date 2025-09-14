#!/bin/sh

# =============================================================================
#         Builds a UEFI-compliant FAT32 disk image from a source directory.
#
#   This script is designed to be called from a Makefile or run manually.
#   It handles creating a blank image, partitioning it, formatting it,
#   and copying the necessary files, with robust cleanup on exit or failure.
# =============================================================================

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Argument Validation ---
if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <output_image_path> <image_size_mb> <efi_source_dir> <mount_point>"
    exit 1
fi

# --- Variables ---
TARGET_IMAGE="$1"
IMG_SIZE_MB="$2"
EFI_SOURCE_DIR="$3"
MOUNT_POINT="$4"
LOOP_DEVICE="" # Initialize for the trap

# --- Cleanup Function ---
# This trap ensures that we unmount and detach the loop device even if the script fails.
cleanup() {
  echo "--- Cleaning up mount and loop device ---"
  if [ -n "$LOOP_DEVICE" ] && mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    sudo umount -l "$MOUNT_POINT"
  fi
  if [ -n "$LOOP_DEVICE" ] && losetup "$LOOP_DEVICE" > /dev/null 2>&1; then
    sudo losetup --detach "$LOOP_DEVICE"
  fi
  if [ -d "$MOUNT_POINT" ]; then
    rm -rf "$MOUNT_POINT"
  fi
}

trap cleanup EXIT

# --- Main Logic ---

mkdir -p "$(dirname "$TARGET_IMAGE")"

echo "[1/5] Creating blank disk image at '$TARGET_IMAGE'..."
dd if=/dev/zero of="$TARGET_IMAGE" bs=1M count="$IMG_SIZE_MB" status=progress >&2

echo "[2/5] Creating GPT partition table..."
sudo sgdisk --zap-all --new=1:0:0 --typecode=1:ef00 "$TARGET_IMAGE" > /dev/null

echo "[3/5] Attaching image to a loop device..."
LOOP_DEVICE=$(sudo losetup --find --show --partscan "$TARGET_IMAGE")
if [ -z "$LOOP_DEVICE" ]; then
    echo "Error: Failed to set up loop device." >&2
    exit 1
fi
PARTITION="${LOOP_DEVICE}p1"
echo "      -> Loop device is $LOOP_DEVICE, partition is $PARTITION"

# Give the kernel a moment to recognize the new partition
sleep 1

echo "[4/5] Formatting and mounting..."
sudo mkfs.fat -F 32 "$PARTITION" > /dev/null
mkdir -p "$MOUNT_POINT"
sudo mount "$PARTITION" "$MOUNT_POINT"

echo "[5/5] Copying EFI files..."
sudo cp -r "$EFI_SOURCE_DIR"/* "$MOUNT_POINT/"
sudo sync
echo "      -> Files copied successfully."

echo
echo "Success! Image build finished."