#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

void initialise_file_table(void)
{
    for (int i = 0; i < FT_SIZE; i++)
    {
        initialise_file_handle(i);
    }
}

void initialise_file_handle(int ft_index)
{
    file_table[ft_index].fh.flag = 0;
    file_table[ft_index].fh.offset = FT_UNUSED;
    file_table[ft_index].fh.count = 0;
    file_table[ft_index].vnode = 0;
}

void decrement_fh_count(int ft_index)
{
    file_table[ft_index].fh.count--;
    if (file_table[ft_index].fh.count <= 0){
        vfs_close(file_table[ft_index].vnode);
        initialise_file_handle(ft_index);
    }
}

int get_ft_index(int *ft_index)
{
    for (int i = 0; i < FT_SIZE; i++)
    {
        if (file_table[i].fh.offset == FT_UNUSED)
        {
            *ft_index = i;
            return 0;
        }
    }
    return ENFILE;
}

int get_fd(int *fd)
{
    for (int i = 3; i < FD_SIZE; i++)
    {
        if (curproc->fd_table[i] == FD_UNUSED)
        {
            *fd = i;
            return 0;
        }
    }
    return EMFILE;
}

int sys_open_ft(char *filename, int flags, int mode, int *ftindex) 
{
    if (filename == NULL) return EFAULT;

    // check if flag is valid
    int allflags = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NOCTTY;
    if ((flags & allflags) != flags) {
		return EINVAL;
	}

    // Open the file
    struct vnode *vnode;
    int err = vfs_open((char *) filename, flags, mode, &vnode);
    if (err) return err;

    // check if file table is full
    int ft_ind;
    err = get_ft_index(&ft_ind);
    if (err) return err;
    
    // Allocate file handle
    file_table[ft_ind].fh.flag = flags;
    file_table[ft_ind].fh.count++;
    file_table[ft_ind].fh.offset = FT_USED;
    file_table[ft_ind].vnode = vnode;
    *ftindex = ft_ind;

    // increase vnode reference count to prevent it from being freed prematurely,
    // and ensure that it remains valid for all processes using it
    VOP_INCREF(vnode); 

    return 0;
}

// https://cgi.cse.unsw.edu.au/~cs3231/18s1/os161/man/syscall/open.html
int sys_open(int a0, int a1, int a2, int *ret)
{
    int err;
    char filename[__NAME_MAX];
    size_t allocated_size;
    err = copyinstr((userptr_t)a0, filename, __NAME_MAX, &allocated_size);
    if (err) return err;

    int flags = a1;
    mode_t mode = a2;

    // Assign the file handle to an unused file descriptor
    int fd;
    err = get_fd(&fd);
    if (err) return err;

    int ft_index;
    err = sys_open_ft(filename, flags, mode, &ft_index);
    if (err) return err;

    curproc->fd_table[fd] = ft_index; // allocate an unused file table index to fd_table[fd]
    *ret = fd;

    return 0;
}

// https://cgi.cse.unsw.edu.au/~cs3231/18s1/os161/man/syscall/close.html
int sys_close(int fd)
{
    // invalid file handle or already closed
    if (fd < 0 || fd >= FT_SIZE || (curproc->fd_table[fd] == FD_UNUSED)) return EBADF;
    int ft_index = curproc->fd_table[fd];
    if (file_table[ft_index].fh.offset == FT_UNUSED) return EBADF;
    decrement_fh_count(ft_index);
    curproc->fd_table[fd] = FD_UNUSED;
    return 0;
}

// https://cgi.cse.unsw.edu.au/~cs3231/18s1/os161/man/syscall/read.html
int sys_read(int a0, int a1, int a2, int32_t *ret)
{
    int fd = a0;
    size_t buflen = a2;

    // invalid file handle or not opened for reading
    if (fd < 0 || fd >= FD_SIZE) return EBADF;
    if (curproc->fd_table[fd] == FD_UNUSED) return EBADF;

    int ft_index = curproc->fd_table[fd];
    if (file_table[ft_index].fh.offset == FT_UNUSED) return EBADF;

    // Check if the file can be read
    if ((file_table[ft_index].fh.flag & O_ACCMODE) == O_WRONLY) return EBADF;

    // iovec and uio initialisation
    struct iovec io;
    struct uio uio;
    uio_uinit(&io, &uio, (userptr_t)a1, buflen, file_table[ft_index].fh.offset, UIO_READ);

    // Read data from the file using the VOP_READ function
    int err = VOP_READ(file_table[ft_index].vnode, &uio);
    if (err) return err;

    // Calculate the number of bytes that were actually read
    *ret = buflen - uio.uio_resid;

    // Update the file offset by the number of bytes read
    file_table[ft_index].fh.offset += *ret;

    return 0;
}

// https://cgi.cse.unsw.edu.au/~cs3231/18s1/os161/man/syscall/write.html
int sys_write(int a0, int a1, int a2, int32_t *ret)
{
    int fd = a0;
    size_t nbytes = a2;

    // invalid file handle or not opened for writing
    if (fd < 0 || fd >= FD_SIZE) return EBADF;
    if (curproc->fd_table[fd] == FD_UNUSED) return EBADF;

    int ft_index = curproc->fd_table[fd];
    if (file_table[ft_index].fh.offset == FT_UNUSED) return EBADF;

    // Check if the file can be written to
    if ((file_table[ft_index].fh.flag & O_ACCMODE) == O_RDONLY) return EBADF;

    // iovec and uio initialisation
    struct iovec io;
    struct uio uio;
    uio_uinit(&io, &uio, (userptr_t)a1, nbytes, file_table[ft_index].fh.offset, UIO_WRITE);

    // Write data to the file using the VOP_WRITE function
    int err = VOP_WRITE(file_table[ft_index].vnode, &uio);
    if (err) return err;

    // Calculate the number of bytes that were actually written
    *ret = nbytes - uio.uio_resid;
    // Update the file offset by the number of bytes written
    file_table[ft_index].fh.offset += *ret; 

    return 0;
}

// https://cgi.cse.unsw.edu.au/~cs3231/18s1/os161/man/syscall/dup2.html
int sys_dup2(int oldfd, int newfd, int *retval)
{
    // Validate file descriptors
    if (oldfd < 0 || oldfd >= FD_SIZE || newfd < 0 || newfd >= FD_SIZE) {
        return EBADF;
    }

    // Check if oldfd is open
    int old_ft_index = curproc->fd_table[oldfd];
    if (old_ft_index == FD_UNUSED) return EBADF;

    // Cloning file handle to itself has no effect
    if (oldfd == newfd) return newfd;

    // Check if newfd is already open, and close it if it is
    int new_ft_index = curproc->fd_table[newfd];
    if (new_ft_index != FD_UNUSED) {
        int err = sys_close(newfd);
        if (err) return err;
    }

    // Duplicate the file descriptor
    curproc->fd_table[newfd] = old_ft_index;
    file_table[old_ft_index].fh.count++;

    *retval = newfd;
    return 0;
}

// https://cgi.cse.unsw.edu.au/~cs3231/18s1/os161/man/syscall/lseek.html
off_t sys_lseek(int fd, off_t pos, int whence, off_t *ret)
{
    // invalid file handle
    if (fd < 0 || fd >= FD_SIZE) return EBADF;
    if (curproc->fd_table[fd] == FD_UNUSED) return EBADF;

    int ft_index = curproc->fd_table[fd];
    if (file_table[ft_index].fh.offset == FT_UNUSED) return EBADF;

    // Check if the file is seekable
    if (!VOP_ISSEEKABLE(file_table[ft_index].vnode)) return ESPIPE;

    int err;
    struct stat f_stat;
    off_t offset;
    switch (whence)
    {
        case SEEK_SET:
            // Set the file offset to the specified position
            if (pos < 0) return EINVAL;
            file_table[ft_index].fh.offset = pos;
            break;
        case SEEK_CUR:
            // Set the file offset to its current position plus the specified offset
            offset = file_table[ft_index].fh.offset + pos;
            if (offset < 0) return EINVAL;
            file_table[ft_index].fh.offset = offset;
            break;
        case SEEK_END:
            err = VOP_STAT(file_table[ft_index].vnode, &f_stat);
            if (err) return err;
            // Set the file offset to the size of the file plus the specified offset
            offset = f_stat.st_size + pos;
            if (offset < 0) return EINVAL; 
            file_table[ft_index].fh.offset = offset;
            break;
        default:
            // Invalid whence value
            return EINVAL;
    }

    // Return the new file offset
    *ret = file_table[ft_index].fh.offset;
    return 0;
}