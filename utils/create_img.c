#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <uchar.h>

enum {
	PARTITION_ENTRY_SIZE = 128,
	NO_GPT_PARTITION_ENTRIES = 128,
	GPT_TABLE_SIZE = 16384,
	ALIGNMENT = 1048576,
};

typedef struct {
	uint32_t			lowTime;
	uint16_t			midTime;
	uint16_t			highTimeVer;
	uint8_t				clockSeqHighRes;
	uint8_t				clockSeqLow;
	uint8_t				node[6];
} __attribute__((packed))
Guid;

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

typedef struct {
	uint8_t 			signature[8];
	uint32_t 			revision;
	uint32_t 			headerSize;
	uint32_t 			headerCRC32;
	uint32_t 			reserved1;
	uint64_t 			selfLBA;
	uint64_t			altLBA;
	uint64_t			firstUseLBA;
	uint64_t			lastUseLBA;
	Guid				diskGUID;
	uint64_t			partEntryArrLBA;
	uint32_t			numPartEntries;
	uint32_t			sizePartEntry;
	uint32_t			partEntryArrCRC32;
	uint8_t				reserved2[512-92];
} __attribute__((packed))
GptHeader;

typedef struct {
	Guid				partTypeGUID;
	Guid				uniquePartGUID;
	uint64_t			startLBA;
	uint64_t			endLBA;
	uint64_t			attrib;
	char16_t			partName[36];
} __attribute__((packed))
GptPartEntry;

// Global variables, for CLI options
uint64_t lba_size = 512;
uint64_t esp_size = 33 * 1024 * 1024;
uint64_t data_size = 1 * 1024 * 1024;
uint64_t write_size = 0;

uint64_t align_lba = 0;
uint64_t esp_lba = 0;
uint64_t data_lba = 0;

static uint32_t crc_table[256] = { 0 };
static bool crc_table_init = false;

const Guid ESP_GUID = { 0xC12A7328, 0xF81F, 0x11D2, 0xBA, 0x4B,
							{ 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } };

const Guid BASIC_DATA_GUID = { 0xEBD0A0A2, 0xB9E5, 0x4433, 0x87, 0xC0,
								{ 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } };

uint32_t bytesToLBAs(const uint32_t bytes) {
	return (bytes + (lba_size - 1)) / lba_size;
}

void padOutZeroes(FILE* img) {
	uint8_t zeroSector[512] = { 0 };
    size_t remaining = lba_size;
    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(zeroSector)) ? sizeof(zeroSector) : remaining;
        if (fwrite(zeroSector, 1, chunk, img) != chunk) {
            break;
        }
        remaining -= chunk;
    }
}

uint64_t getNextAlignedLBA(const uint64_t lba) {
	return lba - (lba % align_lba) + align_lba;
}

void createCRC32Table() {
	uint32_t c;
	for (int32_t n = 0; n < 256; n++) {
		c = (uint32_t) n;
		for (uint8_t k = 0; k < 8; k++) {
			c = (c & 1) ? 0x0edb88320l ^ (c >> 1) : c >> 1;
		}
		crc_table[n] = c;
	}
	crc_table_init = true;
}

uint32_t calcCRC32(void* buf, int32_t len) {
	uint8_t *bufp = buf;
	uint32_t c = 0xFFFFFFFFL;

	if (!crc_table_init) createCRC32Table();

	for (int32_t n = 0; n < len; n++)
		c = crc_table[(c ^ bufp[n]) & 0xFF] ^ (c >> 8);

	return c ^ 0xFFFFFFFFL;
}

Guid getGuid() {
	uint8_t guidArr[16] = { 0 };

	srand(time(NULL));

	for (uint32_t i = 0; i < sizeof(guidArr); i++) 
		guidArr[i] = rand() % (UINT8_MAX + 1);

	Guid guid = {
		.lowTime = * (uint32_t *) &guidArr[0],
		.midTime = * (uint16_t *) &guidArr[4],
		.highTimeVer = * (uint16_t *) &guidArr[6],
		.clockSeqLow = guidArr[9],
		.clockSeqHighRes = guidArr[8],
		.node = {
			guidArr[10], guidArr[11], guidArr[12],
			guidArr[13], guidArr[14], guidArr[15]
		}
	};

	guid.highTimeVer &= ~(1 << 15);
	guid.highTimeVer |= (1 << 14);
	guid.highTimeVer &= ~(1 << 13);
	guid.highTimeVer &= ~(1 << 12);

	guid.clockSeqHighRes |= (1 << 7);
	guid.clockSeqHighRes |= (1 << 6);
	guid.clockSeqHighRes &= ~(1 << 5);

	return guid;
}

bool writeMbr(FILE* img) {
	uint32_t wSize = write_size - 1;
	if (write_size > 0xFFFFFFFF) wSize = 0xFFFFFFFF; 

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
	padOutZeroes(img);
	
	return true;
}

bool writeGpts(FILE* img) {

    GptHeader primary = {
        .signature = { "EFI PART" },
        .revision = 0x00010000,
        .headerSize = 92,
        .headerCRC32 = 0,
        .reserved1 = 0,
        .selfLBA = 1,                            // After MBR(0)
        .altLBA = write_size - 1,
        .firstUseLBA =  1 + 1 + (GPT_TABLE_SIZE / lba_size),
        .lastUseLBA = write_size - 1 - (GPT_TABLE_SIZE / lba_size) - 1,
        .diskGUID = getGuid(),
        .partEntryArrLBA = 2,                    // After MBR(0) and GPT(1)
        .numPartEntries = NO_GPT_PARTITION_ENTRIES,
        .sizePartEntry = PARTITION_ENTRY_SIZE,
        .partEntryArrCRC32 = 0,
        .reserved2 = { 0 },
    };

    GptPartEntry table[NO_GPT_PARTITION_ENTRIES] = { 0 };

    table[0] = (GptPartEntry) {
        .partTypeGUID = ESP_GUID,
        .uniquePartGUID = getGuid(),
        .startLBA = esp_lba,
        .endLBA = esp_lba + bytesToLBAs(esp_size) - 1,
        .attrib = 0,
        .partName = u"EFI SYSTEM"
    };

    table[1] = (GptPartEntry) {
        .partTypeGUID = BASIC_DATA_GUID,
        .uniquePartGUID = getGuid(),
        .startLBA = data_lba,
        .endLBA = data_lba + bytesToLBAs(data_size) - 1,
        .attrib = 0,
        .partName = u"BASIC DATA"
    };

    primary.partEntryArrCRC32 = calcCRC32(table, sizeof(table));

    primary.headerCRC32 = 0;
    primary.headerCRC32 = calcCRC32(&primary, primary.headerSize);

    if (fseek(img, primary.selfLBA * lba_size, SEEK_SET) != 0) return false;
    if (fwrite(&primary, 1, lba_size, img) != lba_size) return false;

    if (fseek(img, primary.partEntryArrLBA * lba_size, SEEK_SET) != 0) return false;
    if (fwrite(&table, 1, sizeof(table), img) != sizeof(table)) return false;

    GptHeader secondary = primary;
    secondary.selfLBA = primary.altLBA;
    secondary.altLBA = primary.selfLBA;
    secondary.partEntryArrLBA = primary.altLBA - (GPT_TABLE_SIZE / lba_size);

    secondary.partEntryArrCRC32 = calcCRC32(table, sizeof(table));

    secondary.headerCRC32 = 0;
    secondary.headerCRC32 = calcCRC32(&secondary, secondary.headerSize);

    if (fseek(img, secondary.partEntryArrLBA * lba_size, SEEK_SET) != 0) return false;
    if (fwrite(&table, 1, sizeof(table), img) != sizeof(table)) return false;

    if (fseek(img, secondary.selfLBA * lba_size, SEEK_SET) != 0) return false;
    if (fwrite(&secondary, 1, lba_size, img) != lba_size) return false;

    return true;
}

int main(int argc, char** argv) {

	if (argc != 2 ) {
		fprintf(stderr, "Error, number of arguments is not 2 :(\n");
		return EXIT_FAILURE;
	}

	const uint64_t padding = (ALIGNMENT * 2 + (lba_size * 67));
	uint32_t img_size = data_size + esp_size + padding;
	write_size  = bytesToLBAs(img_size);
	align_lba = ALIGNMENT / lba_size;
	esp_lba = align_lba;
	data_lba = getNextAlignedLBA(esp_lba + bytesToLBAs(esp_size));

	FILE* imgFile = fopen(argv[1], "wb+");
	if (!imgFile) {
		fprintf(stderr, "Failed to open image file: %s\n", argv[1]);	
		return EXIT_FAILURE;
	}

	if (!writeMbr(imgFile)) {
		fprintf(stderr, "Couldn't write bytes to file: %s", argv[1]);
		return EXIT_FAILURE;
	}

	if (!writeGpts(imgFile)) {
		fprintf(stderr, "Couldn't write gpt bytes to file: %s", argv[1]);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}