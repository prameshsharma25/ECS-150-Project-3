#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xffff
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
	uint16_t * entries;
} FAT;

typedef struct
{
	char filename[16];
	uint32_t size;
	uint16_t first_data_block_index;
	uint8_t padding[10];

} Root_Directory;

#pragma pack(pop)


static Superblock superblock;
static FAT fat;
static int filesWritten = 0;
static int fatBlocksWritten = 0;


int fs_mount(const char *diskname)
{
	if (block_disk_open(diskname) == -1){
		return -1;
	}

	if (block_read(0, &superblock) == -1){
		block_disk_close();
		return -1;
	}

	if((strcmp(superblock.signature,"ECS150FS") != 0) || (superblock.total_blocks != block_disk_count())){
		return -1; // invalid signature
	}

	uint16_t fat_block_init[BLOCK_SIZE]; // temporary array which takes what it can from each fat block 
	fat_block_init[0] = FAT_EOC; // make sure first entry is FAT_EOC
	if(block_write(1, &fat_block_init) == -1){
		return -1;
	}
	fatBlocksWritten++;


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
	if(superblock.data_block_count == 0){
		return -1; // disk hasnt been mounted yet
	}

	if(filesWritten == 0){
		printf("FS Info:\n");
		return 0;
	}
	
	printf("FS Info:\ntotal_blk_count=%i\nfat_blk_count=%i\nrdir_blk=%i\ndata_blk= \
	%i\ndata_blk_count=%i\nfat_free_ratio=%i/%i\nrdir_free_ratio=%i/%i", \
	superblock.total_blocks,superblock.fat_block_count,superblock.root_directory_index,superblock.data_block_start_index, \
	superblock.data_block_count,4096-fatBlocksWritten,128-filesWritten);

	return 0;
}

int fs_create(const char *filename)
{

	/*
	* Proper init and err checking
	*/
	if(filesWritten == 128){
		return -1; // too many files
	}
	char fileStore[16];
	memset(fileStore, 0, 16);
	strcpy(fileStore,filename);
	if(fileStore[15] != 0) {
		return -1; // invalid size
	}
	
	/*
	* Attempt to open file
	*/
	int fd;
	if((fd = open(fileStore, O_RDWR, 0644) < 0)){
		return -1; // problem with opening file
	}
	struct stat st;
	fstat(fd, &st); // obtain file size

	/*
	* Fetch rdir block from disk
	*/
	Root_Directory rdir[128];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}

	/*
	* Add new rdir entry
	*/
	Root_Directory new_dir_entry;
	strcpy(new_dir_entry.filename,fileStore);
	new_dir_entry.size = st.st_size;
	new_dir_entry.first_data_block_index = 

	/*
	* Add file to free data block
	*/


	close(fd); // we dont need this file anymore in local


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
