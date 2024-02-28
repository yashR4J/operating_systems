/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>


#define FT_SIZE __OPEN_MAX
#define FT_UNUSED -1
#define FT_USED 0

typedef struct fh {
    int flag;
    int count;
    off_t offset;
} FH;

struct fnode {
    FH fh;
    struct vnode *vnode;
};

struct fnode file_table[FT_SIZE]; // static file table

// Helper Functions
void initialise_file_table(void);
void initialise_file_handle(int ft_index);
void decrement_fh_count(int ft_index);
int get_ft_index(int *ft_index);
int get_fd(int *fd);

// System Calls
int sys_open_ft(char *filename, int flags, int mode, int *ftindex);
int sys_open(int a0, int a1, int a2, int *ret);
int sys_close(int fd);
int sys_read(int a0, int a1, int a2, int32_t *ret);
int sys_write(int a0, int a1, int a2, int32_t *ret);
int sys_dup2(int oldfd, int newfd, int *retval);
off_t sys_lseek(int fd, off_t pos, int whence, off_t *ret);

#endif /* _FILE_H_ */
