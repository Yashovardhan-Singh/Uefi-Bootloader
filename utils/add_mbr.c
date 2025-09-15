#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <efi.h>

// Overridable from CLI using -D
#define LBA_SIZE	512			// bytes
#define KiB			* 1024		// Keeping * is crazy lol
#define MiB			KiB * 1024	// Size * MiB becomes Size MiB
#define ESP_SIZE	33 MiB		// MiB
#define DATA_SIZE	1 MiB		// MiB

typedef struct {
	uint8_t 			bootIndicator;
	uint8_t				startCHS[3];
	uint8_t 			osIndicator;
	uint8_t				endCHS[3];
	uint32_t			startingLBA;
	uint32_t			sizeInLBA;
} __attribute__((packed))
MbrPartitionRecord;

typedef struct {
	uint8_t 			bootStrapCode[440];
	uint32_t 			uniqueMbrSignature;
	uint16_t 			unknown;		// spec defines 2 bytes as unknown
	MbrPartitionRecord 	partition[4];
	uint16_t 			signature;
} __attribute__((packed))
MbrBootRecord;

uint32_t bytesToLBAs(const uint32_t imgSize) {
	return (imgSize / LBA_SIZE) + (imgSize % LBA_SIZE > 0 ? 1 : 0);
}

bool writeMbr(FILE* img, uint32_t imgSize) {
	uint32_t wSize = imgSize - 1;
	if (imgSize > 0xFFFFFFFF) wSize = 0xFFFFFFFF; 

	MbrBootRecord mbr = {
		.bootStrapCode = { 0 },
		.uniqueMbrSignature = 0,
		.unknown = 0,
		.partition = { 0 },
		.signature = 0xAA55
	};

	mbr.partition[0] = (MbrPartitionRecord) {
		.bootIndicator = 0,
		.startCHS = { 0x00, 0x02, 0x00 },
		.osIndicator = 0xEE,
		.endCHS = { 0xFF, 0xFF, 0xFF },
		.startingLBA = 0x00000001,
		.sizeInLBA = wSize
	};

	if (fwrite(&mbr, 1, sizeof(mbr), img) != sizeof(mbr)) 
		return false;

	return true;
}

int main(int argc, char** argv) {

	if (argc != 2 ) {
		fprintf(stderr, "Error, number of arguments is not 2 :(\n");
		return EXIT_FAILURE;
	}

	uint32_t imgSize = DATA_SIZE + ESP_SIZE + 1 MiB;
	uint32_t lbaSize = bytesToLBAs(imgSize);

	FILE* imgFile = fopen(argv[1], "wb+");
	if (!imgFile) {
		fprintf(stderr, "Failed to open image file: %s\n", argv[1]);	
		return EXIT_FAILURE;
	}

	if (!writeMbr(imgFile, lbaSize)) {
		fprintf(stderr, "Couldn't write bytes to file: %s", argv[1]);
		return 0;
	}

	return EXIT_SUCCESS;
}