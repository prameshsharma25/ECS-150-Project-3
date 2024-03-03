#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fs.h>

#define ASSERT(cond, func)


int main(int argc, char *argv[])
{
	int ret;
	char *diskname;
	//int fd;
	//char data[26] = "abcdefghijklmnopqrstuvwxyz";

	if (argc < 1) {
		printf("Usage: %s <diskimage>\n", argv[0]);
		exit(1);
	}

	/* Mount disk */
	diskname = argv[1];
	ret = fs_mount(diskname);

	/* Close file and unmount */
	fs_umount();

	return 0;
}