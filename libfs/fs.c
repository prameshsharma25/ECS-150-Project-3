#include <assert.h>
#include <fcntl.h>
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
static int fdArray[FS_OPEN_MAX_COUNT];		  // index is 1-1 with fd
static size_t offsetArray[FS_OPEN_MAX_COUNT]; // 1-1 with fdArray --> all initialized to 0
static int openFiles = 0;					  // save computation by storing the number of files open

/*
 * round from scratch
 */
double round(double x)
{
	double fractionalPart = x - (int)x;

	// If the fractional part is exactly 0.5, round to the nearest even integer
	if (fractionalPart == 0.5)
	{
		// If the integer part is odd, round up
		if ((int)x % 2 != 0)
			return x + 0.5;
		else
			return x - 0.5;
	}

	return x >= 0 ? (int)(x + 0.5) : (int)(x - 0.5);
}

/*
 * ceil from scratch
 */
double ceil(double x)
{
	int intPart = (int)x;

	// If x is already an integer, return x
	if (x == intPart)
		return x;

	// If x is negative, return its integer part
	if (x < 0)
		return intPart;

	return intPart + 1.0;
}

/*
 * Determine if mounted or not
 */
static int is_mounted()
{
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}
	return 0;
}

/*
 * Count the number of rdir spots taken
 */
static int files_written()
{
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}
	Root_Directory rdir[128];
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}
	int filesWritten = 0;
	for (int i = 0; i < 128; ++i)
	{
		if (rdir[i].filename[0] != 0)
		{
			filesWritten++;
		}
	}

	return filesWritten;
}

/*
 * Count the number of fat spots taken
 */
int fat_blocks_written()
{
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	Root_Directory rdir[128];
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	int fatBlocksWritten = 0;
	for (int i = 0; i < 128; ++i)
	{
		if (rdir[i].filename[0] == 0)
		{
			continue;
		}
		double frac = (double)rdir[i].size / BLOCK_SIZE;
		double ceiling = ceil(frac);
		int c = round(ceiling);
		fatBlocksWritten += c;
	}

	fatBlocksWritten++; // account for the EOF at the beginning manually

	return fatBlocksWritten;
}

/*
 * Allocate the next block from FAT
 */
int fs_allocate_block(uint16_t *fatBlocks, int data_block_count)
{
	// Find a free block in the FAT
	for (int i = 0; i < data_block_count; i++)
	{
		if (fatBlocks[i] == 0)
		{
			fatBlocks[i] = FAT_EOC;
			return i;
		}
	}
	// No free blocks available
	return -1;
}

int fs_mount(const char *diskname)
{
	if (block_disk_open(diskname) == -1)
	{
		return -1;
	}

	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	if ((strncmp(superblock.signature, "ECS150FS", 8) != 0) || (superblock.total_blocks != block_disk_count()))
	{
		return -1; // invalid signature
	}

	uint16_t fat_block_init[BLOCK_SIZE];
	if (block_read(1, &fat_block_init) == -1)
	{
		return -1;
	}

	fat_block_init[0] = FAT_EOC; // make sure first entry is FAT_EOC

	if (block_write(1, &fat_block_init) == -1)
	{
		return -1;
	}

	for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
	{
		fdArray[i] = -1;
	}

	for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
	{
		offsetArray[i] = 0;
	}

	return 0;
}

int fs_umount(void)
{
	if (block_disk_count() == -1)
	{
		return -1;
	}

	if (block_disk_close() == -1)
	{
		return -1;
	}

	/*
	 * reset fd arrays
	 */
	for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
	{
		fdArray[i] = -1;
	}

	for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
	{
		offsetArray[i] = 0;
	}

	return 0;
}

int fs_info(void)
{
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	int filesWritten = files_written();
	int fatBlocksWritten = fat_blocks_written();

	printf("FS Info:\ntotal_blk_count=%i\nfat_blk_count=%i\nrdir_blk=%i\ndata_blk=%i\ndata_blk_count=%i\nfat_free_ratio=%i/%i\nrdir_free_ratio=%i/%i\n",
		   superblock.total_blocks, superblock.fat_block_count, superblock.root_directory_index, superblock.data_block_start_index,
		   superblock.data_block_count, superblock.data_block_count - fatBlocksWritten, superblock.data_block_count, FS_FILE_MAX_COUNT - filesWritten, FS_FILE_MAX_COUNT);

	return 0;
}

int fs_create(const char *filename)
{

	/*
	 * Obtain superblock
	 */
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}
	int filesWritten = files_written();
	/*
	 * Proper file init and err checking
	 */
	if (filesWritten == FS_FILE_MAX_COUNT)
	{
		return -1; // too many files
	}
	char fileStore[FS_FILENAME_LEN];
	memset(fileStore, 0, FS_FILENAME_LEN);
	strcpy(fileStore, filename);
	if (fileStore[FS_FILENAME_LEN - 1] != 0)
	{
		return -1; // invalid size
	}

	/*
	 * Fetch rdir block from disk
	 */
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	/*
	 * See if filename already exists
	 */
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(rdir[i].filename, fileStore) == 0)
		{
			return -1;
		}
	}

	/*
	 * Create new rdir entry
	 */
	Root_Directory new_dir_entry;
	strcpy(new_dir_entry.filename, fileStore);
	new_dir_entry.size = 0;
	new_dir_entry.first_data_block_index = FAT_EOC;

	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i)
	{
		if (rdir[i].filename[0] == 0)
		{							 // first letter is null terminated
			rdir[i] = new_dir_entry; // add new directory entry if empty
			break;
		}
	}

	if (block_write(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	return 0;
}

int fs_delete(const char *filename)
{
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	/*
	 * Check for valid file
	 */
	char fileStore[16];
	memset(fileStore, 0, 16);
	strcpy(fileStore, filename);
	if (fileStore[15] != 0)
	{
		return -1; // invalid size
	}

	/*
	 * Fetch rdir block from disk
	 */
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	Root_Directory dirRemoval;
	int file_exists = 0;
	int rdir_index;
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i)
	{
		if (strcmp(rdir[i].filename, fileStore) == 0)
		{
			file_exists = 1;
			dirRemoval = rdir[i];
			rdir_index = i;
			break;
		}
	}

	if (!file_exists)
	{
		return -1;
	}

	int blockSpan = ceil(dirRemoval.size / BLOCK_SIZE); // number of blocks the data will be written to
	if (blockSpan == 0)
	{
		Root_Directory empty_directory;
		strcpy(empty_directory.filename, "\0");
		rdir[rdir_index] = empty_directory; // effectively removes size zero file
		return 0;
	}

	/*
	 * Fetch fat blocks from disk
	 */

	uint16_t *fatBlocks = (uint16_t *)malloc(sizeof(uint16_t) * (BLOCK_SIZE * superblock.fat_block_count));
	uint16_t tempFatBlock[BLOCK_SIZE];
	for (int i = 1; i < superblock.root_directory_index; ++i)
	{
		if (block_read(i, &tempFatBlock) == -1)
		{
			return -1;
		}

		memcpy(fatBlocks + (i - 1) * BLOCK_SIZE, tempFatBlock, BLOCK_SIZE);
	}

	int fat_index = dirRemoval.first_data_block_index;
	int temp_index;
	char dataBlock[BLOCK_SIZE]; // empty datablock cell
	for (int i = 0; i < blockSpan; ++i)
	{
		if (block_write(fat_index + superblock.data_block_start_index, &dataBlock) == -1)
		{ // override data block
			return -1;
		}
		temp_index = fatBlocks[fat_index]; // perform swap
		fatBlocks[fat_index] = 0;		   // perform vanquish
		fat_index = temp_index;
	}

	free(fatBlocks);

	return 0;
}

int fs_ls(void)
{

	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	/*
	 * Fetch rdir block from disk
	 */
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i)
	{
		if (rdir[i].filename[0] != 0)
		{
			printf("file: %s, size: %i, data_blk: %i\n", rdir[i].filename, rdir[i].size, rdir[i].first_data_block_index);
		}
	}

	return 0;
}

int fs_open(const char *filename)
{
	/*
	 * Open superblock
	 */
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	if (openFiles > FS_OPEN_MAX_COUNT)
	{
		return -1; // too many files open
	}

	char fileStore[FS_FILENAME_LEN];
	memset(fileStore, 0, FS_FILENAME_LEN);
	strcpy(fileStore, filename);
	if (fileStore[FS_FILENAME_LEN - 1] != 0)
	{
		return -1; // invalid size
	}

	/*
	 * Pick out open file descriptor
	 */
	int fd;
	for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
	{
		if (fdArray[i] == -1)
		{
			fd = i;
			break;
		}
	}

	/*
	 * Open root directory and add dir index to array
	 */
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	/*
	 * Search for file name
	 */
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(rdir[i].filename, fileStore) == 0)
		{
			fdArray[fd] = i;
			break;
		}
	}
	openFiles++;
	return fd;
}

int fs_close(int fd)
{
	if (is_mounted() < 0)
	{
		return -1; // disk hasnt been mounted yet
	}
	if ((fdArray[fd] == -1) || (fd > 31) || (fd < 0))
	{
		return -1; // its already closed or invalid!
	}
	else
	{
		fdArray[fd] = -1;	 // defacto close
		offsetArray[fd] = 0; // reset offset
		openFiles--;
		return 0;
	}
}

int fs_stat(int fd)
{
	if (is_mounted() < 0)
	{
		return -1; // disk hasnt been mounted yet
	}

	if ((fdArray[fd] == -1) || (fd > 31) || (fd < 0))
	{
		return -1; // its closed or invalid
	}

	/*
	 * Open superblock
	 */
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	/*
	 * Open rdir
	 */
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	int rdir_idx = fdArray[fd];
	printf("%i\n", rdir[rdir_idx].size);

	return 0;
}

int fs_lseek(int fd, size_t offset)
{

	if (is_mounted() < 0)
	{
		return -1; // disk hasnt been mounted yet
	}

	if ((fdArray[fd] == -1) || (fd > 31) || (fd < 0))
	{
		return -1; // its closed or invalid
	}

	offsetArray[fd] = offset;

	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{

	/*
	 * Open superblock
	 */
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	if ((fdArray[fd] == -1) || (fd > 31) || (fd < 0))
	{
		return -1; // its closed or invalid
	}

	/*
	 * Open rdir
	 */
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	int rdir_idx = fdArray[fd];

	/*
	 * Open fat blocks
	 */
	uint16_t *fatBlocks = (uint16_t *)malloc(sizeof(uint16_t) * (BLOCK_SIZE * superblock.fat_block_count));
	uint16_t tempFatBlock[BLOCK_SIZE];
	for (int i = 1; i < superblock.root_directory_index; ++i)
	{
		block_read(i, &tempFatBlock);
		memcpy(fatBlocks + (i - 1) * BLOCK_SIZE, tempFatBlock, BLOCK_SIZE);
	}

	/*
	 * Need to determine where exactly to begin writing.. do so by checking whether or not
	 * current index is FAT_EOC => we need a new spot. We can do some basic arithmetic with the
	 * size of the file to determine whether or not we need a new fat cell.
	 */
	// int size = rdir[rdir_idx].size;
	int amtLeft = (int)count;
	int prev_idx;
	int block_index;
	if (rdir[rdir_idx].first_data_block_index == FAT_EOC)
	{
		int set_rdir_idx = 1;
		for (int i = 0; i < BLOCK_SIZE * superblock.fat_block_count; ++i)
		{ // find free spot!
			if (fatBlocks[i] == 0)
			{
				if (set_rdir_idx)
				{
					rdir[rdir_idx].first_data_block_index = i;
					fatBlocks[i] = FAT_EOC;
					prev_idx = i;
					set_rdir_idx = 0;
				}
				else
				{
					fatBlocks[prev_idx] = i;
					fatBlocks[i] = FAT_EOC;
					prev_idx = i;
				}
				amtLeft -= BLOCK_SIZE;
				if (amtLeft > 0)
				{
					continue;
				}
				else
				{
					break;
				}
			}
		}
		if (set_rdir_idx == 1)
		{ // theres no space for anything
			return 0;
		}
	}
	else
	{
		size_t num_fat_blocks = 0;
		block_index = rdir[rdir_idx].first_data_block_index;
		while (block_index != FAT_EOC)
		{ // find last index
			num_fat_blocks++;
			prev_idx = block_index;
			block_index = fatBlocks[block_index];
		}
		amtLeft = num_fat_blocks * BLOCK_SIZE - count;
		if (count > num_fat_blocks * BLOCK_SIZE)
		{
			for (int i = 0; i < BLOCK_SIZE * superblock.fat_block_count; ++i)
			{ // find free spot!
				if (fatBlocks[i] == 0)
				{
					fatBlocks[prev_idx] = i;
					fatBlocks[i] = FAT_EOC;
					prev_idx = i;

					amtLeft -= BLOCK_SIZE;
					if (amtLeft > 0)
					{
						continue;
					}
					else
					{
						break;
					}
				}
			}
		}
	}

	/*
	 * Given the first index in rdir, go to data block of file
	 */
	block_index = rdir[rdir_idx].first_data_block_index; // first index of block array, as provided in FAT
	// printf("%p %ld %i", buf, count, block_index);
	int next_index;
	size_t offset = offsetArray[fd];

	/*
	* Go to position in FAT array - seek to the offset -
	  break with desired index.
	*/
	while (1)
	{
		if (offset >= BLOCK_SIZE)
		{
			// if offset >= block size --> go to the next index
			next_index = fatBlocks[block_index];
			if (next_index == FAT_EOC)
			{			  // offset exceeds space for file
				return 0; // 0 bytes read
			}
			block_index = next_index;
			offset -= BLOCK_SIZE;
		}
		else
		{
			// if offset < block size --> we are ready to read
			break; // block_index is where we want to start
		}
	}

	/*
	 * Begin piping in data through temp data blocks, memcpy buffer to data block, write block back into disk
	 */

	char data_block[BLOCK_SIZE];
	int bytes_written = 0;
	while (1)
	{
		block_read(block_index + superblock.data_block_start_index, &data_block); // read block from disk
		if (count > BLOCK_SIZE - offset)										  // if the count is bigger than the number of remaining blocks
		{
			memcpy(data_block + offset, buf + bytes_written, BLOCK_SIZE - offset);
			block_write(block_index + superblock.data_block_start_index, &data_block);
			bytes_written += (int)(BLOCK_SIZE - offset);
			count -= BLOCK_SIZE - offset;
			offset = 0;
			block_index = fatBlocks[block_index];
			if (block_index == FAT_EOC)
			{
				break;
			}
		}
		else
		{
			memcpy(data_block + offset, buf + bytes_written, count);
			block_write(block_index + superblock.data_block_start_index, &data_block);
			bytes_written += (int)count;
			break;
		}
	}

	free(fatBlocks);
	return bytes_written;
}

int fs_read(int fd, void *buf, size_t count)
{
	/*
	 * Open superblock
	 */
	Superblock superblock;
	if (block_read(0, &superblock) == -1)
	{
		return -1;
	}

	if ((fdArray[fd] == -1) || (fd > 31) || (fd < 0) || (buf == NULL))
	{
		return -1; // its closed or invalid size
	}

	/*
	 * Open rdir
	 */
	Root_Directory rdir[FS_FILE_MAX_COUNT];
	if (block_read(superblock.root_directory_index, &rdir) == -1)
	{
		return -1;
	}

	int rdir_idx = fdArray[fd];

	/*
	 * Open fat blocks
	 */
	uint16_t *fatBlocks = (uint16_t *)malloc(sizeof(uint16_t) * (BLOCK_SIZE * superblock.fat_block_count));
	uint16_t tempFatBlock[BLOCK_SIZE];
	for (int i = 1; i < superblock.root_directory_index; ++i)
	{
		block_read(i, &tempFatBlock);
		memcpy(fatBlocks + (i - 1) * BLOCK_SIZE, tempFatBlock, BLOCK_SIZE);
	}

	/*
	 * Initialize variables for current idx, next idx, and offset
	 */
	int block_index = rdir[rdir_idx].first_data_block_index;

	int next_index;
	size_t offset = offsetArray[fd]; // temp variable that serves as a counter
	/*
	* Go to position in FAT array - seek to the offset -
	  break with desired index.
	*/
	while (1)
	{
		if (offset >= BLOCK_SIZE)
		{
			// if offset >= block size --> go to the next index
			next_index = fatBlocks[block_index];
			if (next_index == FAT_EOC)
			{			  // offset exceeds space for file
				return 0; // 0 bytes read
			}
			block_index = next_index;
			offset -= BLOCK_SIZE;
		}
		else
		{
			// if offset < block size --> we are ready to read
			break; // block_index is where we want to start
		}
	}

	/*
	 * Begin piping in data through temp data blocks
	 */

	char data_block[BLOCK_SIZE];
	int bytes_read = 0;
	while (1)
	{
		block_read(block_index + superblock.data_block_start_index, &data_block);
		if (count > BLOCK_SIZE - offset)
		{
			memcpy(buf + bytes_read, data_block + offset, BLOCK_SIZE - offset);
			bytes_read += (int)(BLOCK_SIZE - offset);
			count -= BLOCK_SIZE - offset;
			offset = 0;
			block_index = fatBlocks[block_index];
			if (block_index == FAT_EOC)
			{
				break;
			}
		}
		else
		{
			memcpy(buf + bytes_read, data_block + offset, count);
			bytes_read += (int)count;
			break;
		}
	}

	offsetArray[fd] += bytes_read;
	return bytes_read;
}
