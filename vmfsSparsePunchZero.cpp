#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <regex>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define COWDISK_MAX_PARENT_FILELEN  1024
#define COWDISK_MAX_NAME_LEN        60
#define COWDISK_MAX_DESC_LEN        512
struct __attribute__((__packed__)) COWDisk_Header {
	uint32_t       magicNumber;
	uint32_t       version;
	uint32_t       flags;
	uint32_t       numSectors;
	uint32_t       grainSize;
	uint32_t       gdOffset;
	uint32_t       numGDEntries;
	uint32_t       freeSector;
	union {
		struct __attribute__((__packed__)) {
			uint32_t cylinders;
			uint32_t heads;
			uint32_t sectors;
		} root;
		struct __attribute__((__packed__)) {
			char     parentFileName[COWDISK_MAX_PARENT_FILELEN];
			uint32_t parentGeneration;
		} child;
	} u;
	uint32_t       generation;
	char           name[COWDISK_MAX_NAME_LEN];
	char           description[COWDISK_MAX_DESC_LEN];
	uint32_t       savedGeneration;
	char           reserved[8];
	uint32_t       uncleanShutdown;
	char           padding[396];
};

#define CACHE_DATA_SECTOR 256

struct VMDKInfo {
	std::string name;
	std::string type;
	std::string disk;
	unsigned long long size;
	int fd;
	COWDisk_Header* header;
	uint32_t* gd;
	uint32_t gdeCache;
	uint32_t* gtCache;
	uint32_t ssCache;
	uint32_t seCache;
	void* sdCache;
	VMDKInfo* parent;
};

VMDKInfo* readHeader(std::string file) {
	FILE* fp = fopen(file.c_str(), "r");
	if (fp == NULL) {
		printf("failed to open descriptor %s\n", file.c_str());
		exit(1);
	}
	VMDKInfo* result = new VMDKInfo{ file };
	char buff[4096];
	std::regex extentPattern(R"RRR(^\s*(RW|RDONLY|NOACCESS)\s+(\d+)\s+(VMFS|VMFSSPARSE)\s+"([^"]+)")RRR");
	std::regex parentPattern(R"RRR(^\s*parentFileNameHint\s*=\s*"([^"]+)")RRR");

	while (fgets(buff, sizeof(buff), fp) != NULL) {
		std::cmatch match;
		if (std::regex_search(buff, match, extentPattern)) {
			if (result->size != 0) {
				printf("only one extent is supported in descriptor %s\n", file.c_str());
				exit(1);
			}
			result->size = std::stoull(match.str(2));
			result->type = match.str(3);
			result->disk = match.str(4);
			result->fd = open(result->disk.c_str(), O_RDONLY);
			if (result->fd == -1) {
				printf("failed to open extent %s\n", result->disk.c_str());
				exit(1);
			}
			if (result->type == "VMFSSPARSE") {
				result->header = new COWDisk_Header;
				if (pread(result->fd, result->header, sizeof(COWDisk_Header), 0) != sizeof(COWDisk_Header)) {
					printf("failed to read extent %s\n", result->disk.c_str());
					exit(1);
				}
				result->gd = new uint32_t[result->header->numGDEntries];
				if (pread(result->fd, result->gd, result->header->numGDEntries * 4, result->header->gdOffset * 512LL) != result->header->numGDEntries * 4) {
					printf("failed to read extent %s\n", result->disk.c_str());
					exit(1);
				}
			}
		}
		else if (std::regex_search(buff, match, parentPattern)) {
			if (result->parent != NULL) {
				printf("only one parent is supported in %s\n", file.c_str());
				exit(1);
			}
			result->parent = readHeader(match.str(1));
		}
	}
	if (result->size == 0) {
		printf("no extent found in descriptor %s\n", file.c_str());
		exit(1);
	}
	else if (result->parent != NULL && result->parent->size != result->size) {
		printf("extent size mismatch between descriptor %s and it's parent\n", file.c_str());
		exit(1);
	}
	return result;
}

bool readSector(VMDKInfo* info, void* data, uint32_t sector) {
	if (sector < info->ssCache || sector >= info->seCache || info->sdCache == NULL) {
		if (info->sdCache == NULL) info->sdCache = new char[CACHE_DATA_SECTOR * 512LL];
		info->ssCache = sector / CACHE_DATA_SECTOR * CACHE_DATA_SECTOR;
		info->seCache = info->ssCache + pread(info->fd, info->sdCache, CACHE_DATA_SECTOR * 512, info->ssCache * 512LL) / 512;
	}
	if (sector >= info->seCache) return false;
	memcpy(data, (char*)info->sdCache + (sector - info->ssCache) * 512, 512);
	return true;
}

bool getSector(VMDKInfo* info, void* data, uint32_t sector, bool recursive) {
	if (info->type == "VMFS") {
		if (data && !readSector(info, data, sector)) goto read_failed;
		return true;
	}
	else if (info->type == "VMFSSPARSE") {
		if (info->header->numSectors != info->size) {
			printf("extent sector number %u mismatch with descriptor %s:%llu\n", info->header->numSectors, info->name.c_str(), info->size);
			exit(1);
		}
		uint32_t gde = info->gd[sector / info->header->grainSize / 4096];
		if (gde != 0) {
			if (info->gdeCache != gde || info->gtCache == NULL) {
				if (info->gtCache == NULL) info->gtCache = new uint32_t[4096];
				info->gdeCache = gde;
				if (pread(info->fd, info->gtCache, 4096 * 4, gde * 512LL) != 4096 * 4) goto read_failed;
			}
			uint32_t gte = info->gtCache[sector / info->header->grainSize % 4096];
			if (gte == 1) {
				if (data) memset(data, 0, 512);
				return true;
			}
			else if (gte > 1) {
				if (data && !readSector(info, data, gte + sector % info->header->grainSize)) goto read_failed;
				return true;
			}
		}
		if (recursive) {
			if (info->parent == NULL) {
				if (data) memset(data, 0, 512);
				return true;
			}
			else return getSector(info->parent, data, sector, recursive);
		}
		else return false;
	}
	else {
		printf("extent type %s not supported\n", info->type.c_str());
		exit(1);
	}
read_failed:
	printf("failed to read extent %s\n", info->disk.c_str());
	exit(1);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("vmfsSparsePunchZero snapshot.vmdk\n");
		return 0;
	}
	VMDKInfo *info = readHeader(argv[1]);
	if (info->type == "VMFSSPARSE" && info->parent != NULL) {
		std::string fn = info->disk + ".new";
		int fd = open(fn.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd == -1) {
			printf("failed to create new disk %s\n", fn.c_str());
			exit(1);
		}
		uint32_t headerSize = info->header->gdOffset * 512;
		uint32_t dataSize = (headerSize + info->header->numGDEntries * 4 + 4095) & ~4095u;
		void* newData = new char[dataSize]();
		COWDisk_Header* newHeader = (COWDisk_Header*)newData;
		*newHeader = *info->header;
		newHeader->freeSector = dataSize / 512;
		uint32_t* gdBase = (uint32_t*)((char*)newData + newHeader->gdOffset * 512);
		char currentSector[512], baseSector[512];
		for (uint32_t i = 0; i < newHeader->numGDEntries; i++) {
			uint32_t gtData[4096] = {};
			for (uint32_t j = 0; j < 4096; j++) {
				for (uint32_t k = 0; k < newHeader->grainSize; k++) {
					if (getSector(info, currentSector, i * 4096 * newHeader->grainSize + j * newHeader->grainSize + k, false)) {
						getSector(info->parent, baseSector, i * 4096 * newHeader->grainSize + j * newHeader->grainSize + k, true);
						if (memcmp(currentSector, baseSector, 512)) {
							if (gdBase[i] == 0) {
								gdBase[i] = newHeader->freeSector;
								newHeader->freeSector += 32;
							}
							gtData[j] = newHeader->freeSector;
							newHeader->freeSector += newHeader->grainSize;
							for (k = 0; k < newHeader->grainSize; k++) {
								getSector(info, currentSector, i * 4096 * newHeader->grainSize + j * newHeader->grainSize + k, false);
								if (pwrite(fd, currentSector, 512, (gtData[j] + k) * 512LL) != 512) {
									printf("failed to write sector to new disk %s\n", fn.c_str());
									exit(1);
								}
							}
							break;
						}
					}
				}
			}
			if (gdBase[i] != 0) {
				if (pwrite(fd, gtData, sizeof(gtData), gdBase[i] * 512LL) != sizeof(gtData)) {
					printf("failed to write grain table to new disk %s\n", fn.c_str());
					exit(1);
				}
			}
		}
		// padding
		uint32_t pad = ((newHeader->freeSector - dataSize / 512 + 32767) & ~32767u) + dataSize / 512 - newHeader->freeSector;
		char zero[512] = {};
		for (uint32_t i = 0; i < pad; i++) {
			if (pwrite(fd, zero, 512, (newHeader->freeSector + i) * 512LL) != 512) {
				printf("failed to pad new disk %s\n", fn.c_str());
				exit(1);
			}
		}
		if (pwrite(fd, newData, dataSize, 0) != dataSize) {
			printf("failed to write header to new disk %s\n", fn.c_str());
			exit(1);
		}
		close(fd);
	}
	while (info) {
		uint32_t count = 0;
		for (unsigned long long i = 0; i < info->size; i++) if (getSector(info, NULL, (uint32_t)i, false)) count++;
		printf("%s:%u/%llu %s\n", info->name.c_str(), count, info->size, info->type.c_str());
		info = info->parent;
	}
	return 0;
}

