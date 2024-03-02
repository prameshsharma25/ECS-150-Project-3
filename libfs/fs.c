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
	char filename[16];
	uint32_t size;
	uint16_t first_data_block_index;
	uint8_t padding[10];

} Root_Directory;

#pragma pack(pop)

/*
* -1 if fd is not valid, points to rdir entry of file, else it points to the rdir index
*/
static int fdArray[FS_OPEN_MAX_COUNT]; // index is 1-1 with fd
static size_t offsetArray[FS_OPEN_MAX_COUNT]; // 1-1 with fdArray --> all initialized to 0
static int openFiles = 0; // save computation by storing the number of files open

/*
* Count the number of rdir spots taken
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

/*
* Count the number of fat spots taken
*/
int fat_blocks_written(){
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}
	Root_Directory rdir[128];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}
	int fatBlocksWritten = 0;
	for(int i = 0; i < 128; ++i){
		if(rdir[i].filename[0] == 0){
			continue;
		}
		double frac = (double)rdir[i].size/BLOCK_SIZE;
		double ceiling = ceil(frac);
		int c = round(ceiling);
		fatBlocksWritten += c;
	}
	fatBlocksWritten++; // account for the EOF at the beginning manually
	return fatBlocksWritten;
}

/*
* Determine if fs is mounted
*/
int is_mounted(){
	uint16_t fat_block_init[BLOCK_SIZE];
	if (block_read(1, &fat_block_init) == -1){
		return 0;
	}

	if(fat_block_init[0] != FAT_EOC){
		return 0;
	}
	return 1;
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

	for(int i = 0; i < FS_OPEN_MAX_COUNT; ++i){
		fdArray[i] = -1;
	}

	for(int i = 0; i < FS_OPEN_MAX_COUNT; ++i){
		offsetArray[i] = 0;
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

	/*
	* reset fd arrays
	*/
	for(int i = 0; i < FS_OPEN_MAX_COUNT; ++i){
		fdArray[i] = -1;
	}
	for(int i = 0; i < FS_OPEN_MAX_COUNT; ++i){
		offsetArray[i] = 0;
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

	printf("FS Info:\ntotal_blk_count=%i\nfat_blk_count=%i\nrdir_blk=%i\ndata_blk=%i\ndata_blk_count=%i\nfat_free_ratio=%i/%i\nrdir_free_ratio=%i/%i\n", 
	superblock.total_blocks,superblock.fat_block_count,superblock.root_directory_index,superblock.data_block_start_index, 
	superblock.data_block_count,BLOCK_SIZE-fatBlocksWritten,BLOCK_SIZE,FS_FILE_MAX_COUNT-filesWritten,FS_FILE_MAX_COUNT);

	return 0;
}

int fs_create(const char *filename)
{

	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}

	int filesWritten = files_written();

	/*
	* Obtain superblock
	*/
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}


	/*
	* Proper file init and err checking
	*/
	if(filesWritten == FS_FILE_MAX_COUNT){
		return -1; // too many files
	}
	char fileStore[FS_FILENAME_LEN];
	memset(fileStore, 0, FS_FILENAME_LEN);
	strcpy(fileStore,filename);
	if(fileStore[FS_FILENAME_LEN - 1] != 0) {
		return -1; // invalid size
	}

	/*
	* Fetch rdir block from disk
	*/
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}

	/*
	* See if filename already exists
	*/
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(strcmp(rdir[i].filename,fileStore) == 0){
			printf("filename already exists! at %i",rdir[i].first_data_block_index);
			return -1;
		}
	}

	/*
	* Create new rdir entry
	*/
	Root_Directory new_dir_entry;
	strcpy(new_dir_entry.filename,fileStore);
	new_dir_entry.size = 0;
	new_dir_entry.first_data_block_index = FAT_EOC;

	for(int i = 0; i < FS_FILE_MAX_COUNT; ++i){
		if (rdir[i].filename[0] == 0){ // first letter is null terminated
			rdir[i] = new_dir_entry; // add new directory entry if empty
		}
	}

	if (block_write(superblock.root_directory_index, &rdir) == -1){
		perror("Error when writing root directory back to disk\n");
		return -1;
	}

	return 0;

}

int fs_delete(const char *filename)
{
	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}

	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
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
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	Root_Directory dirRemoval;
	int file_exists = 0;
	int rdir_index;
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}
	for(int i = 0; i < FS_FILE_MAX_COUNT; ++i){
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
		fat_index = temp_index;
	}

	free(fatBlocks);
	return 0;
}

int fs_ls(void)
{
	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}

	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}

	/*
	* Fetch rdir block from disk
	*/
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}

	printf("FS Ls:\n");
	for(int i = 0; i < FS_FILE_MAX_COUNT; ++i){
		if(rdir[i].filename[0] == 0){
			printf("file: %s, size: %i, data_blk: %i",rdir[i].filename,rdir[i].size,rdir[i].first_data_block_index);
		}
	}

}

int fs_open(const char *filename)
{
	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}

	if(openFiles > FS_OPEN_MAX_COUNT){
		return -1; // too many files open
	}

	char fileStore[FS_FILENAME_LEN];
	memset(fileStore, 0, FS_FILENAME_LEN);
	strcpy(fileStore,filename);
	if(fileStore[FS_FILENAME_LEN - 1] != 0) {
		return -1; // invalid size
	}

	/*
	* Pick out open file descriptor
	*/
	int fd;
	for(int i = 0; i < FS_OPEN_MAX_COUNT; ++i){
		if(fdArray[i] == -1){
			fd = i;
			break;
		}
	}

	/*
	* Open superblock
	*/
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}

	/*
	* Open root directory and add dir index to array
	*/
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}

	/*
	* Search for file name
	*/
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(strcmp(rdir[i].filename,fileStore) == 0){
			fdArray[fd] = i;
			break;
		}
	}
	openFiles++;
	return fd;
}

int fs_close(int fd)
{
	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}
	if((fdArray[fd] == -1) || (fd > 31)){
		return -1; // its already closed!
	}
	else{
		fdArray[fd] = -1; // defacto close
		offsetArray[fd] = 0;
		openFiles--;
		return 0;
	}
}

int fs_stat(int fd)
{
	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}

	if((fdArray[fd] == -1) || (fd > 31)){
		return -1; // its closed or invalid
	}

	/*
	* Open superblock
	*/
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}

	/*
	* Open rdir
	*/
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}
	int rdir_idx = fdArray[fd];
	printf("%i\n",rdir[rdir_idx].size);
	return 0;
}

int fs_lseek(int fd, size_t offset)
{

	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}

	if((fdArray[fd] == -1) || (fd > 31) || (fd < 0)){
		return -1; // its closed or invalid
	}

	offsetArray[fd] = offset;
	return 0;

}

int fs_write(int fd, void *buf, size_t count)
{
	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}

	if((fdArray[fd] == -1) || (fd > 31)){
		return -1; // its closed or invalid
	}

	/*
	* Open superblock
	*/
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}

	/*
	* Open rdir
	*/
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}

	int rdir_idx = fdArray[fd];

	/*
	* Open fat blocks
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

	/*
	* Given the first index in rdir, go to data block of file, perform write...
	*/
	int block_index = rdir[rdir_idx].first_data_block_index; // first index of block array, as provided in FAT
}

int fs_read(int fd, void *buf, size_t count)
{
	if(!is_mounted()){
		return -1; // disk hasnt been mounted yet
	}

	if((fdArray[fd] == -1) || (fd > 31)){
		return -1; // its closed or invalid
	}

	/*
	* Open superblock
	*/
	Superblock superblock;
	if (block_read(0, &superblock) == -1){
		return -1;
	}

	/*
	* Open rdir
	*/
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1){
		perror("Error when reading root directory from disk\n");
		return -1;
	}

	int rdir_idx = fdArray[fd];

	/*
	* Open fat blocks
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

	/*
	* Given the first index in rdir, go to data block of file, perform read...
	*/
	int block_index = rdir[rdir_idx].first_data_block_index;
}
