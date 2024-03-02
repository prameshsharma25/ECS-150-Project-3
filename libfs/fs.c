#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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


static FAT fat;

/*
* Count the number of rdir spots
*/
static int files_written(){
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}
	Root_Directory rdir[128];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}
	int filesWritten = 0;
	for(int i = 0; i < 128; ++i){
		if(rdir[i].filename[0] == 0){
			// nothing
		}
		else{
			filesWritten++;
		}
	}
	return filesWritten;
}

int fat_blocks_written(){
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}
	uint16_t tempFatBlock[BLOCK_SIZE];
	int fatBlocksWritten = 0;
	for(int i = 1; i < superblock.root_directory_index; i++){
		if (block_read(i, &tempFatBlock ) == -1){
			perror("Error when reading fat block from disk\n");
			return -1;
		}
		for(int j = 0; j < BLOCK_SIZE; j++){
			if(tempFatBlock[j] != 0){
				fatBlocksWritten++;
			}
		}
	}
	return fatBlocksWritten;
}

int fs_mount(const char *diskname)
{
	if (block_disk_open(diskname) == -1){
		return -1;
	}
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}

	if((strncmp(superblock.signature,"ECS150FS",8) != 0) || (superblock.total_blocks != block_disk_count())){
		return -1; // invalid signature
	}


	uint16_t fat_block_init[BLOCK_SIZE];
	if (block_read(1, &fat_block_init) == -1){
		return -1;
	}

	fat_block_init[0] = FAT_EOC; // make sure first entry is FAT_EOC

	if(block_write(1, &fat_block_init) == -1){
		return -1;
	}
	//fatBlocksWritten++;


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
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}

	int filesWritten = files_written();
	int fatBlocksWritten = fat_blocks_written();

	printf("FS Info:\ntotal_blk_count=%i\nfat_blk_count=%i\nrdir_blk=%i\ndata_blk= \
	%i\ndata_blk_count=%i\nfat_free_ratio=%i/%i\nrdir_free_ratio=%i/%i", \
	superblock.total_blocks,superblock.fat_block_count,superblock.root_directory_index,superblock.data_block_start_index, \
	superblock.data_block_count,BLOCK_SIZE-fatBlocksWritten,BLOCK_SIZE,128-filesWritten,128);

	return 0;
}

int fs_create(const char *filename)
{
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}

	int filesWritten = files_written();
	int fatBlocksWritten = fat_blocks_written();

	if(fatBlocksWritten == 0){
		return -1; // disk hasnt been mounted yet
	}

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
	* See if filename already exists
	*/
	for(int i = 0; i < 128; i++){
		if((strcmp(rdir[i].filename,fileStore)) == 0){
			printf("filename already exists! at %i",rdir[i].first_data_block_index);
			return -1;
		}
	}

	/*
	* Create new rdir entry
	*/
	Root_Directory new_dir_entry;
	strcpy(new_dir_entry.filename,fileStore);
	new_dir_entry.size = st.st_size;

	/*
	* Add file to free data block and configure FAT
	*/
	int blockSpan = ceil(new_dir_entry.size/BLOCK_SIZE); // number of blocks the data will be written to
	if(blockSpan == 0){
		new_dir_entry.first_data_block_index = FAT_EOC;
	}
	else{

		/*
		* Extract Fat Cells
		*/
		uint16_t * fatBlocks = (uint16_t *)malloc(sizeof(uint16_t)*(BLOCK_SIZE*superblock.fat_block_count));
		uint16_t tempFatBlock[BLOCK_SIZE];
		for(int i = 1; i < superblock.root_directory_index; i++){
			if (block_read(i, &tempFatBlock ) == -1){
				perror("Error when reading fat block from disk\n");
				return -1;
			}
			memcpy(fatBlocks + (i-1)*BLOCK_SIZE,tempFatBlock,BLOCK_SIZE);
		}

		/*
		* Determine free entries in fat -- > write to corresponding data block
		*/
		char dataBlock[BLOCK_SIZE];
		int dataBlocksWritten = 0;
		int sizeLeft = new_dir_entry.size;
		int prevFatIndex = -1;
		for(int i = 0; i < superblock.data_block_count; ++i){
			if(dataBlocksWritten == blockSpan){
				break;
			}
			if(fatBlocks[i] == 0){
				if(prevFatIndex == -1){
					new_dir_entry.first_data_block_index = i;
				}
				if (block_read(i+superblock.data_block_start_index, &dataBlock) == -1){
					perror("Error when reading data block from disk\n");
					return -1;
				}
				if(sizeLeft > BLOCK_SIZE){
					if(prevFatIndex != -1){
						fatBlocks[prevFatIndex] = i;
					}
					read(fd,dataBlock,BLOCK_SIZE);
					lseek(fd,BLOCK_SIZE,SEEK_CUR); // move BLOCK_SIZE bytes in file
					sizeLeft -= BLOCK_SIZE;
		
					
				} else{
					if(prevFatIndex != -1){
						fatBlocks[prevFatIndex] = i;
					}
					read(fd,dataBlock,sizeLeft);
					fatBlocks[i] = FAT_EOC;
				}
				/*
				* Write data block back into disk
				*/
				if (block_write(i+superblock.data_block_start_index, &dataBlock) == -1){
						perror("Error when writing data block back into disk\n");
						return -1;
				}
				prevFatIndex = i;
				dataBlocksWritten++;		
			}
		}
		/*
		* Write fat back into disk (sequentially)
		*/
		for(int i = 1; i < superblock.root_directory_index; i++){
			memcpy(tempFatBlock, fatBlocks + (i-1)*BLOCK_SIZE,BLOCK_SIZE);
			if (block_write(i, &tempFatBlock) == -1){
				perror("Error when reading fat block from disk\n");
				return -1;
			}
		}

		free(fatBlocks); // we are done with this

	}
	for(int i = 0; i < 128; ++i){
		if (rdir[i].filename[0] == 0){ // first letter is null terminated
			// add new directory entry
			rdir[i] = new_dir_entry;
		}
	}
	if (block_write(superblock.root_directory_index, &rdir) == -1){
		perror("Error when writing root directory back to disk\n");
		return -1;
	}

	close(fd); // we dont need this file anymore in local
	return 0;

}

int fs_delete(const char *filename)
{
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}
	int fatBlocksWritten = fat_blocks_written();

	if(fatBlocksWritten == 0){
		return -1; // disk hasnt been mounted yet
	}

	/*
	* Check for valid file
	*/
	char fileStore[16];
	memset(fileStore, 0, 16);
	strcpy(fileStore,filename);
	if(fileStore[15] != 0) {
		return -1; // invalid size
	}

	/*
	* Fetch rdir block from disk
	*/
	Root_Directory rdir[128];
	Root_Directory dirRemoval;
	int file_exists = 0;
	int rdir_index;
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}
	for(int i = 0; i < 128; ++i){
		if(strcmp(rdir[i].filename,fileStore) == 0){
			file_exists = 1;
			dirRemoval = rdir[i];
			rdir_index = i;
			break;
		}
	}

	if(!file_exists){
		return -1;
	}

	int blockSpan = ceil(dirRemoval.size/BLOCK_SIZE); // number of blocks the data will be written to
	if(blockSpan == 0){
		Root_Directory empty_directory;
		rdir[rdir_index] = empty_directory; // effectively removes size zero file
		return 0;
	}

	/*
	* Fetch fat blocks from disk
	*/

	uint16_t * fatBlocks = (uint16_t *)malloc(sizeof(uint16_t)*(BLOCK_SIZE*superblock.fat_block_count));
	uint16_t tempFatBlock[BLOCK_SIZE];
	for(int i = 1; i < superblock.root_directory_index; ++i){
		if (block_read(i, &tempFatBlock ) == -1){
			perror("Error when reading fat block from disk\n");
			return -1;
		}
		memcpy(fatBlocks + (i-1)*BLOCK_SIZE,tempFatBlock,BLOCK_SIZE);
	}

	int fat_index = dirRemoval.first_data_block_index;
	int temp_index;
	char dataBlock[BLOCK_SIZE]; // empty datablock cell
	for(int i = 0; i < blockSpan; ++i){
		if(block_write(fat_index + superblock.data_block_start_index, &dataBlock) == -1){ // override data block
			perror("Error when writing data block back into disk\n");
			return -1;
		}
		temp_index = fatBlocks[fat_index]; // perform swap
		fatBlocks[fat_index] = 0; // perform vanquish
	}

	free(fatBlocks);
	return 0;
}

int fs_ls(void)
{
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}
	int fatBlocksWritten = fat_blocks_written();

	if(fatBlocksWritten == 0){
		return -1; // disk hasnt been mounted yet
	}

	/*
	* Fetch rdir block from disk
	*/
	Root_Directory rdir[128];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}

	printf("FS Ls:\n");
	for(int i = 0; i < 128; ++i){
		if(rdir[i].filename[0] == 0){
			printf("file: %s, size: %i, data_blk: %i",rdir[i].filename,rdir[i].size,rdir[i].first_data_block_index);
		}
	}

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
