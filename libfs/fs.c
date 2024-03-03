#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#pragma pack(push, 1)

typedef struct
{
	char signature[8];
	uint16_t total_blocks;
	uint16_t root_directory_index;
	uint16_t data_block_start_index;
	uint16_t data_block_count;
	uint8_t fat_block_count;
	uint8_t padding[4079];
} Superblock;

typedef struct
{
	uint16_t  * entries;
} FAT;

typedef struct
{
	char filename[16];
	uint32_t size;
	uint16_t first_data_block_index;
	uint8_t padding[10];

} Root_Directory;

static Superblock superblock;
static FAT fat;
static Root_Directory root;

#pragma pack(pop)

int fs_mount(const char *diskname)
{
	if (block_disk_open(diskname) == -1){
		return -1;
	}

	if (block_read(0, &superblock) == -1){
		block_disk_close();
		return -1;
	}

	printf("%s %i %i %i %i %i \n",superblock.signature,superblock.total_blocks,superblock.root_directory_index,
	superblock.data_block_start_index,superblock.data_block_count,superblock.fat_block_count);

	if(strcmp(superblock.signature,"ECS150FS") != 0){
		return -1;
	}

	return 0;
}

int fs_umount(void)
{
	if (block_disk_count() == -1){
		return -1;
	}

	if (block_disk_close() == -1){
		return -1;
	}


	return 0;
}

int fs_info(void)
{
	return 0;
}

int fs_create(const char *filename)
{
	char fileStore[17];
	memset(fileStore, 0, 17);
	strcpy(fileStore,filename);
	if(fileStore[16] != 0) {
		return -1; // invalid size
	}
	int fd;
	if(fd = open(fileStore, O_RDWR, 0644) < 0){
		return -1; // problem with opening file
	}

}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_ls(void)
{
	/* TODO: Phase 2 */
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}
