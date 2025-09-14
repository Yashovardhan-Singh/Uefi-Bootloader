// Todo: write a utility program to add protective MBR to efi_image

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <efi.h>

typedef struct {
	uint8_t 			bootIndicator;
	uint8_t 			startHead;
	uint8_t 			startSector;
	uint8_t 			startTrack;
	uint8_t 			osIndicator;
	uint8_t 			endHead;
	uint8_t 			endSector;
	uint8_t 			endTrack;
	uint8_t 			startingLBA[4];
	uint8_t 			sizeInLBA[4];
} __attribute__((packed))
MbrPartitionRecord;

typedef struct {
	uint8_t 			bootStrapCode[440];
	uint8_t 			uniqueMbrSignature[4];
	uint8_t 			unknown[2];		// spec defines 2 bytes as unknown
	MbrPartitionRecord 	partition[4];
	uint16_t 			signature;
} __attribute__((packed))
MbrBootRecord;

bool writeMbr(FILE* img) {
	return true;
}

int main(int argc, char** argv) {
	return EXIT_SUCCESS;
}