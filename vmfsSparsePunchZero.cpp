#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <regex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

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

struct VMDKInfo {
	std::string name;
	std::string type;
	unsigned long long size;
	void* data;
	VMDKInfo* parent;
};

void* getMapping(const std::string file) {
	int fd = open(file.c_str(), O_RDONLY);
	if (fd == -1) {
		printf("failed to open extent %s\n", file.c_str());
		exit(1);
	}
	struct stat sb;
	if (fstat(fd, &sb) == -1) {
		printf("failed to stat extent %s\n", file.c_str());
		exit(1);
	}
	void* addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == NULL) {
		printf("failed to mmap extent %s\n", file.c_str());
		exit(1);
	}
	return addr;
}

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
			result->data = getMapping(match.str(4));
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

void* getSector(const VMDKInfo* info, uint32_t sector, bool recursive) {
	static char* zero = NULL;
	if (zero == NULL) zero = new char[512]();
	if (info->type == "VMFS") return (char*)info->data + sector * 512LL;
	else if (info->type == "VMFSSPARSE") {
		COWDisk_Header* header = (COWDisk_Header*)info->data;
		if (header->numSectors != info->size) {
			printf("extent sector number %u mismatch with descriptor %s:%llu\n", header->numSectors, info->name.c_str(), info->size);
			exit(1);
		}
		uint32_t gde = *((uint32_t*)((char*)info->data + header->gdOffset * 512LL) + sector / header->grainSize / 4096);
		if (gde != 0) {
			uint32_t gte = *((uint32_t*)((char*)info->data + gde * 512LL) + sector / header->grainSize % 4096);
			if (gte == 1) return zero;
			else if (gte > 1) return (char*)info->data + (gte + sector % header->grainSize) * 512LL;
		}
		if (recursive) {
			if (info->parent == NULL) return zero;
			else return getSector(info->parent, sector, recursive);
		}
		else return NULL;
	}
	else {
		printf("extent type %s not supported\n", info->type.c_str());
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("vmfsSparsePunchZero snapshot.vmdk\n");
		return 0;
	}
	VMDKInfo *info = readHeader(argv[1]);
	if (info->type == "VMFSSPARSE" && info->parent != NULL) {
		std::string fn = std::string(argv[1]) + ".new";
		FILE* fp = fopen(fn.c_str(), "wb+");
		if (fp == NULL) {
			printf("failed to create new disk %s\n", fn.c_str());
			exit(1);
		}
		COWDisk_Header* header = (COWDisk_Header*)info->data;
		uint32_t headerSize = header->gdOffset * 512;
		uint32_t totalSize = (headerSize + header->numGDEntries * 4 + 4095) & ~4095u;
		void* newData = new char[totalSize]();
		COWDisk_Header* newHeader = (COWDisk_Header*)newData;
		*newHeader = *header;
		newHeader->freeSector = totalSize / 512;
		uint32_t* gdBase = (uint32_t*)((char*)newData + newHeader->gdOffset * 512);
		if (fwrite(newData, totalSize, 1, fp) != 1) {
			printf("failed to write header to new disk %s\n", fn.c_str());
			exit(1);
		}
		for (uint32_t i = 0; i < newHeader->numGDEntries; i++) {
			uint32_t gtData[4096] = {};
			fpos_t fgt;
			for (uint32_t j = 0; j < 4096; j++) {
				bool writeGrain = false;
				for (uint32_t k = 0; k < newHeader->grainSize; k++) {
					void* currentData = getSector(info, i * 4096 * newHeader->grainSize + j * newHeader->grainSize + k, false);
					void* baseData = getSector(info->parent, i * 4096 * newHeader->grainSize + j * newHeader->grainSize + k, true);
					if (currentData != NULL && memcmp(currentData, baseData, 512)) writeGrain = true;
				}
				if (writeGrain) {
					if (gdBase[i] == 0) {
						gdBase[i] = newHeader->freeSector;
						newHeader->freeSector += 32;
						if (fgetpos(fp, &fgt) != 0) {
							printf("failed to get pos of new disk %s\n", fn.c_str());
							exit(1);
						}
						if (fwrite(gtData, sizeof(gtData), 1, fp) != 1) {
							printf("failed to write grain table to new disk %s\n", fn.c_str());
							exit(1);
						}
					}
					gtData[j] = newHeader->freeSector;
					newHeader->freeSector += newHeader->grainSize;
					for (uint32_t k = 0; k < newHeader->grainSize; k++) {
						void* currentData = getSector(info, i * 4096 * newHeader->grainSize + j * newHeader->grainSize + k, false);
						if (fwrite(currentData, 512, 1, fp) != 1) {
							printf("failed to write sector to new disk %s\n", fn.c_str());
							exit(1);
						}
					}
				}
			}
			if (gdBase[i] != 0) {
				fpos_t fcur;
				if (fgetpos(fp, &fcur) != 0) {
					printf("failed to get pos of new disk %s\n", fn.c_str());
					exit(1);
				}
				if (fsetpos(fp, &fgt) != 0) {
					printf("failed to set pos of new disk %s\n", fn.c_str());
					exit(1);
				}
				if (fwrite(gtData, sizeof(gtData), 1, fp) != 1) {
					printf("failed to write grain table to new disk %s\n", fn.c_str());
					exit(1);
				}
				if (fsetpos(fp, &fcur) != 0) {
					printf("failed to set pos of new disk %s\n", fn.c_str());
					exit(1);
				}
			}
		}
		// padding
		uint32_t pad = ((newHeader->freeSector - totalSize / 512 + 32767) & ~32767u) + totalSize / 512 - newHeader->freeSector;
		char zero[512] = {};
		while (pad--) {
			if (fwrite(zero, 512, 1, fp) != 1) {
				printf("failed to pad new disk %s\n", fn.c_str());
				exit(1);
			}
		}
		if (fseek(fp, 0L, SEEK_SET) != 0) {
			printf("failed to seek new disk %s\n", fn.c_str());
			exit(1);
		}
		if (fwrite(newData, totalSize, 1, fp) != 1) {
			printf("failed to write header to new disk %s\n", fn.c_str());
			exit(1);
		}
		fclose(fp);
	}
	while (info) {
		uint32_t count = 0;
		for (unsigned long long i = 0; i < info->size; i++) if (getSector(info, (uint32_t)i, false)) count++;
		printf("%s:%u/%llu %s\n", info->name.c_str(), count, info->size, info->type.c_str());
		info = info->parent;
	}
    return 0;
}

