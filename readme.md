# UEFI Bootloader

UEFI bootloader, work in progress.

building the repo only works in a **POSIX environment**.

bundled [firmware](firmware/bios64.bin) as well as default clang target is **x86_64 architecture**. Build your own firmware using [edk2](https://github.com/tianocore/edk2), and modify the makefile to target other platforms

## Build Instructions:
1) Clone the repo:  
```
git clone https://gitlab.com/Yashovardhan-Singh/uefi-bootloader.git
```
2) Makefile rules:
	* **run**: compile and create image, then run using qemu
	* **compile**: compile the source to .efi
	* **compile_utils**: compile all utility program in [utils](utils/) and place them in [utils](utils/)/out
	* **image**: create a FAT32 image from .efi
	* **clean**: clean build artifacts in bin folder, preserves built image. deletes utilities as well

**NOTE:** The makefile will prompt you for higher privilege, required to create a working bootable image

## Dependencies:
* [qemu](https://www.qemu.org/): Emulator to run the bootloader
* [clang](https://clang.llvm.org/): Compiler for C, works better than gcc
* [lld](https://lld.llvm.org/): LLVM linker, required for PE/COFF executable