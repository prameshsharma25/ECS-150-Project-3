#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h" // Assuming fs.h contains declarations for file system functions

#define ASSERT(cond)                                          \
    do                                                        \
    {                                                         \
        if (!(cond))                                          \
        {                                                     \
            fprintf(stderr, "Assertion failed: %s\n", #cond); \
            exit(1);                                          \
        }                                                     \
    } while (0)

int main(int argc, char *argv[])
{
    int ret;
    char *diskname;

    /* Mount disk */
    diskname = argv[1];
    ret = fs_mount(diskname);
    ASSERT(ret == 0);

    // Test Write with Offset
    ret = fs_create("file_offset");
    ASSERT(ret == 0);

    char initial_data[] = "initial data";
    int fd_offset = fs_open("file_offset");
    ret = fs_write(fd_offset, initial_data, strlen(initial_data));
    ASSERT(ret == strlen(initial_data));

    char new_data[] = "new data";
    ret = fs_write(fd_offset, new_data, strlen(new_data));
    ASSERT(ret == strlen(new_data));
    fs_ls();

    // Test Write on File That Already Exists
    ret = fs_create("file_exists");
    ASSERT(ret == 0);

    char existing_data[] = "existing data";
    int fd_exists = fs_open("file_exists");
    ret = fs_write(fd_exists, existing_data, strlen(existing_data));
    ASSERT(ret == strlen(existing_data));

    char additional_data[] = "additional data";
    ret = fs_write(fd_exists, additional_data, strlen(additional_data));
    ASSERT(ret == strlen(additional_data));

    // Test File Delete
    ret = fs_create("file_delete");
    ASSERT(ret == 0);
    fs_ls();

    ret = fs_delete("file_delete");
    ASSERT(ret == 0);
    fs_ls();

    // Test Bounds Testing
    ret = fs_create("file_bounds");
    ASSERT(ret == 0);

    char large_data[1048577];
    memset(large_data, 'A', sizeof(large_data));

    // Attempt to write data exceeding file size limit
    int fd_bounds = fs_open("file_bounds");
    ret = fs_write(fd_bounds, large_data, sizeof(large_data));

    ret = fs_delete("file_bounds");
    ASSERT(ret == 0);
    fs_ls();


    /* Close file and unmount */
    ret = fs_umount();
    ASSERT(ret == 0);

    return 0;
}