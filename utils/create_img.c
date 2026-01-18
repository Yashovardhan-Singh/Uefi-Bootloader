// --------------------------------------------
// Title: UEFI compliant FAT32 Image Creator
// Credits: Yashovardhan-Singh 2025-2026
// Description: 1000+ line C code, designed to
//        replace a 75 line POSIX shell script
//
// Followed:
// https://www.youtube.com/watch?v=t3iwBQg_Gik&list=PLT7NbkyNWaqZYHNLtOZ1MNxOt8myP5K0p&index=2
// https://github.com/queso-fuego/UEFI-GPT-image-creator
// --------------------------------------------

// std includes
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uchar.h>

// ------------
// CONSTANTS
// ------------
enum {
    // Todo: Comment why these values or you'll forget a week in
    PARTITION_ENTRY_SIZE     = 128,
    NO_GPT_PARTITION_ENTRIES = 128,
    GPT_TABLE_SIZE           = 16384,
    ALIGNMENT                = 1048576,
};

typedef enum {
    ATTR_READ_ONLY = 0x01,
    ATTR_HIDDEN    = 0x02,
    ATTR_SYSTEM    = 0x04,
    ATTR_VOLUME_ID = 0x08,
    ATTR_DIRECTORY = 0x10,
    ATTR_ARCHIVE   = 0x20,
    ATTR_LONG_NAME =
        ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID,
} DirAttr;

// -------------
// STRUCTS
// -------------

// Globally unique identifier
// same as universally unique identifier
// just microsoft L calling it GUID when everyone else calls it GUID
// for finger-printing anything and everything
typedef struct {
    uint32_t lowTime;      // low field of TimeStamp
    uint16_t midTime;      // mid field of TimeStamp
    uint16_t highTimeVer;  // high field of TimeStampe multiplexed with version
                           // number
    uint8_t clockSeqHighRes;  // high field of clock sequence multiplexed with
                              // variant
    uint8_t clockSeqLow;      // low field of clock sequence
    uint8_t node[6];  // what? Absolute brain fart field, i stole code here
} __attribute__((packed)) Guid;

typedef struct {
    uint8_t bootIndicator;  // set to 0 for non bootable partition, undefined on
                            // non UEFI, ignored on UEFI
    uint8_t  startCHS[3];   // CHS address of partition start
    uint8_t  osIndicator;   // set to 0xEE (GPT Protective)
    uint8_t  endCHS[3];     // CHS address of partition end
    uint32_t startingLBA;   // LBA of the GPT partition header
    uint32_t sizeInLBA;     // size of disk - 1, clamped to 0xFFFFFFFF
} __attribute__((packed)) MbrPartitionRecord;

typedef struct {
    uint8_t            bootStrapCode[440];  // MBR Code (asm)
    uint32_t           uniqueMbrSignature;  // Unused, set to 0
    uint16_t           unknown;             // Unused, set to 0
    MbrPartitionRecord partition[4];        // partitions for MBR
    uint16_t           signature;           // MAGIC NUMBER 0xAA55 (LE)
} __attribute__((packed)) MasterBootRecord;

typedef struct {
    uint8_t  signature[8];  // "EFI PART" in hex 0x5452415020494645
    uint32_t revision;  // header version is 1.0, so 0x00010000 (first 4 major,
                        // last 4 minor)
    uint32_t headerSize;   // must be >= 92 and <= Logical block size
    uint32_t headerCRC32;  // CRC32 checksum, set to 0, then compute checksumfor
                           // "headerSize" amount of bytes
    uint32_t reserved1;    // Has to be 0
    uint64_t selfLBA;      // LBA containing this header
    uint64_t altLBA;       // LBA of backup/alternate GPT
    uint64_t firstUseLBA;  // first usable LBA of any partition described in GPT
    uint64_t lastUseLBA;   // last usable LBA of any partition described in GPT
    Guid     diskGUID;     // GUID for the disk
    uint64_t partEntryArrLBA;  // starting LBA of GPT entry array
    uint32_t numPartEntries;   // number of partition entries in GPT
    uint32_t
        sizePartEntry;  // size in bytes of entries in GPT (128 * 2n where n
                        // >= 0)
    uint32_t partEntryArrCRC32;  // CRC32 checksum of GPT entry array. (computed
                                 // over partEntryArrLBA to (partEntryArrLBA +
                                 // (numPartEntries * sizePartEntry)))
    uint8_t reserved2[512 - 92];  // rest of the block is reserved and must be 0
} __attribute__((packed)) GptHeader;

typedef struct {
    Guid partTypeGUID;  // Unique ID to define purpose and type, 0 means not
                        // being used
    Guid     uniquePartGUID;  // GUID unique to every GPT entry
    uint64_t startLBA;        // starting LBA of partition defined by this entry
    uint64_t endLBA;          // ending LBA of partition defined by this entry
    uint64_t attrib;          // all bits are reserved by UEFI
    char16_t partName[36];    // Null terminated string, human readable name of
                              // partition
} __attribute__((packed)) GptPartEntry;

// Volume Boot Record for FAT32
typedef struct {
    uint8_t jmpBoot[3];    // boot sector jump instruction to boot code. Should
                           // be [0]=0xEB, [1]=any, [2]=0x90
    uint8_t  oemName[8];   // Should be "MSWIN1.4" for driver compat
    uint16_t bytesPerSec;  // bytes per sector (512, 1024, 2048, 4096. def: 512)
    uint8_t  secPerClust;  // sectors per cluster. Power of 2, > 0 && <= 128.
                          // bytesPerSec * secPerClust should never be > 32K (32
                          // * 1024)
    uint16_t rsvdSecCnt;  // Number of reserved sectors. Usually 32, never 0
    uint8_t  numFATs;     // Number of FAT data structures on volume
    uint16_t rootEntCnt;  // Number of directory entries in root directory
    uint16_t totSec16;  // total count of sectors on volume. must be 0 for FAT32
    uint8_t  media;     // Type (0xF8 for non-removable, 0xF0 for removable)
    uint16_t fatSz16;   // for FAT12/16. 0 on FAT32
    uint16_t secPerTrk;  // sectors per track. for volumes with CHS
    uint16_t numHeads;   // number of heads, the H in CHS
    uint32_t hiddSec;    // number of hidden sectors. for volumes with CHS
    uint32_t totSec32;   // total count of sectors on volume. non 0 on FAT32
    uint32_t fatSz32;    // 32 bit count of occupied sectors by 1 FAT
    uint16_t extFlags;   // google the flags highk
    uint16_t fsVer;      // high byte major, low byte minor
    uint32_t rootClus;   // cluster number of first cluster of root dir. def: 2
    uint16_t fsInfo;     // usually 1
    uint16_t bkBootSec;  // sector number of backup boot record in rsvd vol area
    uint8_t  reserved[12];        // reserved for future use
    uint8_t  drvNum;              // drive number. compat w FAT12/16
    uint8_t  reserved1;           // reserved. compat w FAT12/16
    uint8_t  bootSig;             // boot signature. compat w FAT12/16
    uint8_t  volID[4];            // volume ID. compat w FAT12/16
    uint8_t  volLab[11];          // volume label. compat w FAT12/16
    uint8_t  filSysTyp[8];        // Always set to "FAT32   "
    uint8_t  bootCode[510 - 90];  // rest of the boot code ig
    uint16_t bootSectSig;         // 0xAA55. Of course. you again.
} __attribute__((packed)) VolumeBootRecord;

// File System Info struct for FAT32
typedef struct {
    uint32_t leadSig;         // always 0x41615252
    uint8_t  reserved1[480];  // reserved for future use, always 0
    uint32_t strucSig;        // always 0x61417272
    uint32_t freeCount;       // <= cluster count. I32MAX means recompute
    uint32_t nextFree;        // hint for FAT driver to find next free cluster
    uint8_t  reserved2[12];   // reserved, always 0
    uint32_t trailSig;        // always 0xAA550000
} __attribute__((packed)) FSInfo;

// FAT32 Directory Entry struct
typedef struct {
    uint8_t  name[11];  // short name
    uint8_t  attr;      // attributes. upper 2 bits are reserved
    uint8_t  ntRes;     // set to 0 on file creation, and never even look again
    uint8_t  crtTimeTenth;  // creation timestamp (10ths of a sec)
    uint16_t crtTime;       // creation timestamp
    uint16_t crtDate;       // creation date
    uint16_t lstAccDate;    // date of last access
    uint16_t fstClustHI;    // high word of first cluster
    uint16_t wrtTime;       // time of last write
    uint16_t wrtDate;       // date of last write
    uint16_t fstClustLO;    // low word of first cluster
    uint32_t fileSize;      // size of file
} __attribute__((packed)) DirEntry;

// Global variables, for CLI options
uint64_t lba_unit_size_b  = 512;               // in bytes
uint64_t esp_full_size_b  = 33 * 1024 * 1024;  // 33 MiB
uint64_t data_full_size_b = 1 * 1024 * 1024;   // 1 MiB
uint64_t img_size         = 0;                 // ?

uint64_t esp_size_lba   = 0;  // EFI system partition size in LBAs
uint64_t data_size_lba  = 0;  // Data partition size in LBAs
uint64_t img_size_lba   = 0;  // Size of image in LBAs
uint64_t gpt_table_lbas = 0;  // Size of GPT table in LBAs

uint64_t global_lba_alignment = 0;  // LBA alignment, derived in main
uint64_t esp_location_lba     = 0;  // EFI system partition location LBA
uint64_t data_location_lba    = 0;  // Data location LBA

static uint32_t crc_table[256] = {0};    // Table for creating CRC32 checksums
static bool     crc_table_init = false;  // has CRC32 table been created?

// GUIDs to write
// Unsure about these being hardcoded, but we'll see ig lol
const Guid ESP_GUID        = {0xC12A7328, 0xF81F,
                              0x11D2,     0xBA,
                              0x4B,       {0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};
const Guid BASIC_DATA_GUID = {0xEBD0A0A2, 0xB9E5,
                              0x4433,     0x87,
                              0xC0,       {0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}};

// DO NOT INLINE
uint32_t bytesToLBAs(const uint32_t bytes) {
    return (bytes + (lba_unit_size_b - 1)) / lba_unit_size_b;
}

// seems redundant but lba_unit_size_b is a cli config flag (todo)
void padOutZeroes(FILE *img) {
    uint8_t zeroSector[512] = {0};
    size_t  remaining       = lba_unit_size_b;
    while (remaining > 0) {
        size_t chunk =
            (remaining > sizeof(zeroSector)) ? sizeof(zeroSector) : remaining;
        // i forgot why size = 1 and n = chunk, but doesn't matter in this case
        // tbh
        if (fwrite(zeroSector, 1, chunk, img) != chunk) {
            break;
        }
        remaining -= chunk;
    }
}

// Next aligned LBA
uint64_t getNextAlignedLBA(const uint64_t lba) {
    return lba - (lba % global_lba_alignment) + global_lba_alignment;
}

// get date and time for directory creation
void getDirectoryCreationTimeDate(uint16_t *creationTime,
                                  uint16_t *creationDate) {
    time_t    curr_time = time(NULL);
    struct tm stamp     = *localtime(&curr_time);

    *creationDate = ((stamp.tm_year - 80) << 9) | ((stamp.tm_mon + 1) << 5) |
                    ((stamp.tm_mday));
    if (stamp.tm_sec == 60)
        stamp.tm_sec = 59;
    *creationTime =
        stamp.tm_hour << 11 | stamp.tm_min << 5 | (stamp.tm_sec / 2);
}

// Creates a table for CRC32
// https://www.w3.org/TR/png/#D-CRCAppendix
void createCRC32Table() {
    uint32_t c;
    for (int32_t i = 0; i < 256; i++) {
        c = (uint32_t) i;
        for (uint8_t k = 0; k < 8; k++) {
            c = (c & 1) ? 0x0edb88320l ^ (c >> 1) : c >> 1;
        }
        crc_table[i] = c;
    }
    crc_table_init = true;
}

// Cyclic Redundancy Check (32 bit)
// https://www.w3.org/TR/png/#D-CRCAppendix
uint32_t calcCRC32(void *buf, int32_t len) {
    uint8_t *bufp = buf;
    uint32_t c    = 0xFFFFFFFFL;

    if (!crc_table_init)
        createCRC32Table();

    for (int32_t n = 0; n < len; n++)
        c = crc_table[(c ^ bufp[n]) & 0xFF] ^ (c >> 8);

    return c ^ 0xFFFFFFFFL;
}

Guid getGuid() {
    uint8_t guidArr[16] = {0};  // 128 bit guid, as byte array

    srand(time(NULL));  // Seed using OS time

    for (uint32_t i = 0; i < sizeof(guidArr); i++)
        guidArr[i] = rand() % (UINT8_MAX + 1);  // keeps rand() output to 8 bits

    Guid guid = {.lowTime     = *(uint32_t *) &guidArr[0],  // 4 bytes: 0,1,2,3
                 .midTime     = *(uint16_t *) &guidArr[4],  // 2 bytes: 4,5
                 .highTimeVer = *(uint16_t *) &guidArr[6],  // 2 bytes: 6,7
                 .clockSeqLow = guidArr[9],  // big endian so guidArr[9] first
                 .clockSeqHighRes = guidArr[8],  // guidArr[8] is next byte
                 .node = {guidArr[10], guidArr[11], guidArr[12], guidArr[13],
                          guidArr[14], guidArr[15]}};

    // Fill out the version bits (from the spec)
    // Version 4
    guid.highTimeVer &= ~(1 << 15);
    guid.highTimeVer |= (1 << 14);
    guid.highTimeVer &= ~(1 << 13);
    guid.highTimeVer &= ~(1 << 12);
    // produces: 0b11101111

    // Fill out the variant bits (from the spec)
    // Variant 2
    guid.clockSeqHighRes |= (1 << 7);
    guid.clockSeqHighRes |= (1 << 6);
    guid.clockSeqHighRes &= ~(1 << 5);
    // produces: 0b11000000

    return guid;
}

bool writeMbr(FILE *img) {
    uint32_t wSize = img_size_lba - 1;  // write size
    if (img_size_lba > 0xFFFFFFFF)      // clamp(size, size, 0xINT32_MAX)
        wSize = 0xFFFFFFFF;

    // Go to struct definition for details on fields
    MasterBootRecord mbr = {.bootStrapCode      = {0},
                            .uniqueMbrSignature = 0,
                            .unknown            = 0,
                            .partition          = {0},
                            .signature          = 0xAA55};  // MAGIC NUMBER

    // only using the first partition
    mbr.partition[0] = (MbrPartitionRecord) {.bootIndicator = 0,
                                             .startCHS    = {0x00, 0x02, 0x00},
                                             .osIndicator = 0xEE,
                                             .endCHS      = {0xFF, 0xFF, 0xFF},
                                             .startingLBA = 0x00000001,
                                             .sizeInLBA   = wSize};

    if (fwrite(&mbr, 1, sizeof(mbr), img) != sizeof(mbr))
        return false;
    padOutZeroes(img);  // Fix alignment by padding with zeroes

    return true;
}

bool writeGpts(FILE *img) {

    // Primary partition table header
    // refer to struct definition
    // for notes on fields
    GptHeader primary = {
        .signature         = {"EFI PART"},
        .revision          = 0x00010000,
        .headerSize        = 92,
        .headerCRC32       = 0,
        .reserved1         = 0,
        .selfLBA           = 1,
        .altLBA            = img_size_lba - 1,
        .firstUseLBA       = 2 + gpt_table_lbas,
        .lastUseLBA        = img_size_lba - gpt_table_lbas - 2,
        .diskGUID          = getGuid(),
        .partEntryArrLBA   = 2,
        .numPartEntries    = NO_GPT_PARTITION_ENTRIES,
        .sizePartEntry     = PARTITION_ENTRY_SIZE,
        .partEntryArrCRC32 = 0,
        .reserved2         = {0},
    };

    // declare the array of GPT entries
    GptPartEntry table[NO_GPT_PARTITION_ENTRIES] = {0};

    // first partition where UEFI data will be stored
    table[0] = (GptPartEntry) {.partTypeGUID   = ESP_GUID,
                               .uniquePartGUID = getGuid(),
                               .startLBA       = esp_location_lba,
                               .endLBA         = esp_location_lba +
                                         bytesToLBAs(esp_full_size_b) - 1,
                               .attrib   = 0,
                               .partName = u"EFI SYSTEM"};

    // 2nd partition where Other data needed will be stored
    table[1] = (GptPartEntry) {.partTypeGUID   = BASIC_DATA_GUID,
                               .uniquePartGUID = getGuid(),
                               .startLBA       = data_location_lba,
                               .endLBA         = data_location_lba +
                                         bytesToLBAs(data_full_size_b) - 1,
                               .attrib   = 0,
                               .partName = u"BASIC DATA"};

    // Calculate checksum for partition entry tables
    primary.partEntryArrCRC32 = calcCRC32(table, sizeof(table));

    // Calculate checksum for GPT header
    // DO NOT REMOVE primary.headerCRC32 = 0;
    // it breaks idk exactly why yet, need to investigate post feature
    // completion
    primary.headerCRC32 = 0;
    primary.headerCRC32 = calcCRC32(&primary, primary.headerSize);

    // Check if write pointer is in correct location
    if (fseek(img, primary.selfLBA * lba_unit_size_b, SEEK_SET) != 0)
        return false;

    // write GPT header at correct LBA
    // again, size is 1 and n is size of lba, i don't remember why
    if (fwrite(&primary, 1, lba_unit_size_b, img) != lba_unit_size_b)
        return false;

    // Check if write pointer is in correct location
    if (fseek(img, primary.partEntryArrLBA * lba_unit_size_b, SEEK_SET) != 0)
        return false;

    // write partition entry array/table at correct LBA
    if (fwrite(&table, 1, sizeof(table), img) != sizeof(table))
        return false;

    // Alternate/Backup/At the end GPT header
    GptHeader secondary       = primary;
    secondary.selfLBA         = primary.altLBA;
    secondary.altLBA          = primary.selfLBA;
    secondary.partEntryArrLBA = img_size_lba - 1 - gpt_table_lbas;

    // same as primary.partEntryArrCRC32
    secondary.partEntryArrCRC32 = calcCRC32(table, sizeof(table));

    // Calculate checksum for GPT header
    // DO NOT REMOVE secondary.headerCRC32 = 0;
    // it breaks idk exactly why yet, need to investigate post feature
    // completion
    secondary.headerCRC32 = 0;
    secondary.headerCRC32 = calcCRC32(&secondary, secondary.headerSize);

    // set write pointer at alt GPT entries LBA
    if (fseek(img, secondary.partEntryArrLBA * lba_unit_size_b, SEEK_SET) != 0)
        return false;

    // write alt GPT entries
    if (fwrite(&table, 1, sizeof(table), img) != sizeof(table))
        return false;

    // set write pointer at alt GPT header LBA
    if (fseek(img, secondary.selfLBA * lba_unit_size_b, SEEK_SET) != 0)
        return false;

    // write alt GPT header
    if (fwrite(&secondary, 1, lba_unit_size_b, img) != lba_unit_size_b)
        return false;

    return true;
}

bool writeEfiSystemPartition(FILE *img) {
    // Todo: Write ESPs
    const uint8_t    reserved_sectors = 32;
    VolumeBootRecord vbr              = {
                     .jmpBoot     = {0xEB, 0x00, 0x90},
                     .oemName     = {"MSWIN4.1"},
                     .bytesPerSec = lba_unit_size_b,
                     .secPerClust = 1,
                     .rsvdSecCnt  = reserved_sectors,
                     .numFATs     = 2,
                     .rootEntCnt  = 0,
                     .totSec16    = 0,
                     .media       = 0xF8,
                     .fatSz16     = 0,
                     .secPerTrk   = 0,
                     .numHeads    = 0,
                     .hiddSec     = esp_location_lba - 1,
                     .totSec32    = esp_size_lba,
                     .fatSz32     = (global_lba_alignment - reserved_sectors) / 2,
                     .extFlags    = 0,
                     .fsVer       = 0,
                     .rootClus    = 2,  // 0 and 1 reserved. root starts at 2
                     .fsInfo      = 1,  // 0 is this VBR, 1 is info sector
                     .bkBootSec   = 6,  // according to white paper
                     .reserved    = {0},
                     .drvNum      = 0x80,  // 1st hard drive
                     .reserved1   = 0,
                     .bootSig     = 0x29,
                     .volID       = {0},
                     .volLab      = {"NO NAME    "},
                     .filSysTyp   = {"FAT32   "},
                     .bootCode    = {0},
                     .bootSectSig = 0xAA55,
    };

    FSInfo fsi = {.leadSig   = 0x41615252,
                  .reserved1 = {0},
                  .strucSig  = 0x61417272,
                  .freeCount = 0xFFFFFFFF,
                  .nextFree  = 0xFFFFFFFF,
                  .reserved2 = {0},
                  .trailSig  = 0xAA550000};

    // Write volume boot record
    fseek(img, esp_location_lba * lba_unit_size_b, SEEK_SET);
    if (fwrite(&vbr, 1, sizeof(vbr), img) != sizeof(vbr)) {
        fprintf(stderr, "couldn't write Volume Boot Record to file");
        return false;
    }
    padOutZeroes(img);
    // write file system info
    if (fwrite(&fsi, 1, sizeof(fsi), img) != sizeof(fsi)) {
        fprintf(stderr, "couldn't write File System Info to file");
        return false;
    }
    padOutZeroes(img);

    // seek to backup boot sector location
    fseek(img, (esp_location_lba + vbr.bkBootSec) * lba_unit_size_b, SEEK_SET);

    // write vbr and FSInfo at backup location
    fseek(img, esp_location_lba * lba_unit_size_b, SEEK_SET);
    if (fwrite(&vbr, 1, sizeof(vbr), img) != sizeof(vbr)) {
        fprintf(stderr, "couldn't write Volume Boot Record to file");
        return false;
    }
    padOutZeroes(img);
    if (fwrite(&fsi, 1, sizeof(fsi), img) != sizeof(fsi)) {
        fprintf(stderr, "couldn't write File System Info to file");
        return false;
    }
    padOutZeroes(img);

    const uint32_t fat_location_lba = esp_location_lba + vbr.rsvdSecCnt;
    // write FATs
    for (uint8_t i = 0; i < vbr.numFATs; i++) {
        fseek(img, (fat_location_lba + (i * vbr.fatSz32)) * lba_unit_size_b,
              SEEK_SET);

        // cluster 0: FAT Identifier. Lowest 8 is type of media
        uint32_t cluster = 0;
        cluster          = 0xFFFFFF00 | vbr.media;
        fwrite(&cluster, sizeof(cluster), 1, img);

        // cluster 1: EOC (end of chain) marker
        cluster = 0xFFFFFFFF;
        fwrite(&cluster, sizeof(cluster), 1, img);

        // cluster 2: root directory cluster '/'
        cluster = 0xFFFFFFFF;
        fwrite(&cluster, sizeof(cluster), 1, img);

        // cluster 3: EFI directory cluster '/EFI'
        cluster = 0xFFFFFFFF;
        fwrite(&cluster, sizeof(cluster), 1, img);

        // cluster 4: BOOT directory cluster '/EFI/BOOT'
        cluster = 0xFFFFFFFF;
        fwrite(&cluster, sizeof(cluster), 1, img);
    }

    // write file data
    const uint32_t file_data_location_lba =
        fat_location_lba + (vbr.numFATs * vbr.fatSz32);
    fseek(img, file_data_location_lba * lba_unit_size_b, SEEK_SET);

    // EFI directory
    DirEntry efi_dir_entry = {
        .name         = {"EFI        "},
        .attr         = ATTR_DIRECTORY,
        .ntRes        = 0,
        .crtTimeTenth = 0,
        .crtTime      = 0,
        .crtDate      = 0,
        .lstAccDate   = 0,
        .fstClustHI   = 0,
        .wrtTime      = 0,
        .wrtDate      = 0,
        .fstClustLO   = 3,
        .fileSize     = 0,
    };

    uint16_t creationTime = 0, creationDate = 0;
    getDirectoryCreationTimeDate(&creationTime, &creationDate);
    efi_dir_entry.crtTime = creationTime;
    efi_dir_entry.wrtTime = creationTime;
    efi_dir_entry.crtDate = creationDate;
    efi_dir_entry.wrtDate = creationDate;

    /* if (fwrite(&efi_dir_entry, sizeof(efi_dir_entry), 1, img) != */
    /*     sizeof(efi_dir_entry)) { */
    /*     fprintf(stderr, "couldn't write EFI directory entry"); */
    /*     return false; */
    /* } */

    fwrite(&efi_dir_entry, sizeof(efi_dir_entry), 1, img);

    fseek(img, (file_data_location_lba + 1) * lba_unit_size_b, SEEK_SET);
    memcpy(efi_dir_entry.name, ".         ", 11);
    fwrite(&efi_dir_entry, sizeof(efi_dir_entry), 1, img);
    memcpy(efi_dir_entry.name, "..        ", 11);
    efi_dir_entry.fstClustLO = 0;
    fwrite(&efi_dir_entry, sizeof(efi_dir_entry), 1, img);
    memcpy(efi_dir_entry.name, "BOOT       ", 11);
    efi_dir_entry.fstClustLO = 4;
    fwrite(&efi_dir_entry, sizeof(efi_dir_entry), 1, img);

    fseek(img, (file_data_location_lba + 2) * lba_unit_size_b, SEEK_SET);
    memcpy(efi_dir_entry.name, ".         ", 11);
    fwrite(&efi_dir_entry, sizeof(efi_dir_entry), 1, img);
    memcpy(efi_dir_entry.name, "..        ", 11);
    efi_dir_entry.fstClustLO = 3;
    fwrite(&efi_dir_entry, sizeof(efi_dir_entry), 1, img);

    return true;
}

int main(int argc, char **argv) {

    if (argc != 2) {
        fprintf(stderr, "Error, number of arguments is not 2 :(\n");
        return EXIT_FAILURE;
    }

    gpt_table_lbas = GPT_TABLE_SIZE / lba_unit_size_b;
    // padding the image. +1 for MBR, +2 for primary and secondary GPT headers
    const uint64_t padding =
        (ALIGNMENT * 2 + (lba_unit_size_b * ((gpt_table_lbas * 2) + 1 + 2)));
    img_size = data_full_size_b + esp_full_size_b + padding;  // in bytes
    global_lba_alignment = ALIGNMENT / lba_unit_size_b;
    img_size_lba         = bytesToLBAs(img_size);
    esp_location_lba     = global_lba_alignment + 0;  // 0th LBA + alignment
    esp_size_lba         = bytesToLBAs(esp_full_size_b);
    data_location_lba    = getNextAlignedLBA(esp_location_lba + esp_size_lba);
    data_size_lba        = bytesToLBAs(data_full_size_b);

    // It could probably be more secure lol
    // Wrong, it WILL be more secure
    // prolly
    FILE *imgFile = fopen(argv[1], "wb+");
    if (!imgFile) {
        fprintf(stderr, "Failed to open image file: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    // Writing the protective MBR failed, maybe i should add a trace log, stdout
    // only
    if (!writeMbr(imgFile)) {
        fprintf(stderr, "Couldn't write bytes to file: %s", argv[1]);
        return EXIT_FAILURE;
    }

    // I definitely should write a backtrace logger, even though this will only
    // be used by me, for this project
    if (!writeGpts(imgFile)) {
        fprintf(stderr, "Couldn't write gpt bytes to file: %s", argv[1]);
        return EXIT_FAILURE;
    }

    if (!writeEfiSystemPartition(imgFile)) {
        fprintf(stderr, "Couldn't write ESP bytes to file: %s", argv[1]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
