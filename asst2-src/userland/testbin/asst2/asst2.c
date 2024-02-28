#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>

#define MAX_BUF 500
char teststr[] = "The quick brown fox jumped over the lazy dog.";
char buf[MAX_BUF];

int
main(int argc, char * argv[])
{
        int fd, r, i, j , k;
        (void) argc;
        (void) argv;

        printf("\n**********\n* File Tester\n");

        snprintf(buf, MAX_BUF, "**********\n* write() works for stdout\n");
        write(1, buf, strlen(buf));
        snprintf(buf, MAX_BUF, "**********\n* write() works for stderr\n");
        write(2, buf, strlen(buf));

        printf("**********\n* opening new file \"test.file\"\n");
        fd = open("test.file", O_RDWR | O_CREAT, 0600); /* mode u=rw in octal */
        printf("* open() got fd %d\n", fd);
        if (fd < 0) {
                printf("ERROR opening file: %s\n", strerror(errno));
                exit(1);
        }

        printf("* writing test string\n");
        r = write(fd, teststr, strlen(teststr));
        printf("* wrote %d bytes\n", r);
        if (r < 0) {
                printf("ERROR writing file: %s\n", strerror(errno));
                exit(1);
        }

        printf("* writing test string again\n");
        r = write(fd, teststr, strlen(teststr));
        printf("* wrote %d bytes\n", r);
        if (r < 0) {
                printf("ERROR writing file: %s\n", strerror(errno));
                exit(1);
        }
        printf("* closing file\n");
        close(fd);

        printf("**********\n* opening old file \"test.file\"\n");
        fd = open("test.file", O_RDONLY);
        printf("* open() got fd %d\n", fd);
        if (fd < 0) {
                printf("ERROR opening file: %s\n", strerror(errno));
                exit(1);
        }

        printf("* reading entire file into buffer \n");
        i = 0;
        do  {
                printf("* attempting read of %d bytes\n", MAX_BUF -i);
                r = read(fd, &buf[i], MAX_BUF - i);
                printf("* read %d bytes\n", r);
                i += r;
        } while (i < MAX_BUF && r > 0);

        printf("* reading complete\n");
        if (r < 0) {
                printf("ERROR reading file: %s\n", strerror(errno));
                exit(1);
        }
        k = j = 0;
        r = strlen(teststr);
        do {
                if (buf[k] != teststr[j]) {
                        printf("ERROR  file contents mismatch\n");
                        exit(1);
                }
                k++;
                j = k % r;
        } while (k < i);
        printf("* file content okay\n");

        printf("**********\n* testing lseek\n");
        r = lseek(fd, 5, SEEK_SET);
        if (r < 0) {
                printf("ERROR lseek: %s\n", strerror(errno));
                exit(1);
        }

        printf("* reading 10 bytes of file into buffer \n");
        i = 0;
        do  {
                printf("* attempting read of %d bytes\n", 10 - i );
                r = read(fd, &buf[i], 10 - i);
                printf("* read %d bytes\n", r);
                i += r;
        } while (i < 10 && r > 0);
        printf("* reading complete\n");
        if (r < 0) {
                printf("ERROR reading file: %s\n", strerror(errno));
                exit(1);
        }

        k = 0;
        j = 5;
        r = strlen(teststr);
        do {
                if (buf[k] != teststr[j]) {
                        printf("ERROR  file contents mismatch\n");
                        exit(1);
                }
                k++;
                j = (k + 5)% r;
        } while (k < 5);

        printf("* file lseek okay\n");
        printf("* closing file\n");

        printf("**********\n* testing dup2\n");

        int newfd = 5; // choose an unused fd
        printf("* duplicating file descriptor %d to %d using dup2\n", fd, newfd);
        if (dup2(fd, newfd) < 0) {
            printf("ERROR dup2: %s\n", strerror(errno));
            exit(1);
        }

        printf("* reading 10 bytes of file into buffer using duplicated file descriptor\n");

        if (lseek(newfd, 0, SEEK_SET) < 0) {
            printf("ERROR lseek: %s\n", strerror(errno));
            exit(1);
        }

        i = 0;
        do {
            printf("* attempting read of %d bytes\n", 10 - i);
            r = read(newfd, &buf[i], 10 - i);
            printf("* read %d bytes\n", r);
            i += r;
        } while (i < 10 && r > 0);
        printf("* reading complete\n");

        if (r < 0) {
            printf("ERROR reading file: %s\n", strerror(errno));
            exit(1);
        }

        k = 0;
        r = strlen(teststr);
        do {
            if (buf[k] != teststr[k]) {
                printf("ERROR file contents mismatch using duplicated file descriptor\n");
                exit(1);
            }
            k++;
        } while (k < 10);

        printf("* file content using duplicated file descriptor is okay\n");
        printf("* closing duplicated file descriptor\n");

        
        printf("**********\n* Testing read() with a closed file descriptor\n");
        close(newfd);
        i = 0;
        do {
                printf("* attempting read of %d bytes\n", MAX_BUF - i);
                r = read(newfd, &buf[i], MAX_BUF - i);
                printf("* read %d bytes\n", r);
                i += r;
        } while (i < MAX_BUF && r > 0);
        printf("* reading complete\n");
        if (r < 0) {
                printf("* cannot read from a closed file descriptor\n");
        } else {
                printf("* ERROR: cannot read from a closed file descriptor\n");
                exit(1);
        }

        printf("**********\n* Testing write() with a closed file descriptor\n");
        close(fd);
        r = write(fd, teststr, strlen(teststr));
        if (r < 0) {
                printf("* cannot write to a closed file descriptor\n");
        } else {
                printf("* ERROR: writing to a closed file descriptor\n");
                exit(1);
        }

        printf("**********\n* Testing open() with a non-existent file\n");
        fd = open("non-existent.file", O_RDONLY);
        printf("* open() got fd %d\n", fd);
        if (fd < 0) {
                printf("* cannot open a non-existent file\n");
        } else {
                printf("* ERROR: cannot open a non-existent file\n");
                exit(1);
        }

        // printf("**********\n* Testing open() with an invalid file mode\n");
        // fd = open("test.file", O_RDWR | O_CREAT, 01000); /* Invalid mode */ << MODES IGNORED IN OS161
        // printf("* open() got fd %d\n", fd);
        // if (fd < 0) {
        //         printf("ERROR opening file: %s\n", strerror(errno));
        //         exit(1);
        // }

        printf("**********\n* Testing dup2() with an invalid file descriptor\n");
        int invalid_fd = 99; // an invalid file descriptor
        if (dup2(invalid_fd, 1) < 0) {
                printf("dup2() invalid file descriptor\n");
        } else {
                printf("dup2() invalid file descriptor\n");
                exit(1);
        }

        printf("**********\n* Testing lseek() with an invalid file descriptor\n");
        invalid_fd = 99; // an invalid file descriptor
        r = lseek(invalid_fd, 5, SEEK_SET);
        if (r < 0) {
                printf("lseek() invalid file descriptor\n");
        } else {
                printf("lseek() invalid file descriptor\n");
                exit(1);
        }

        printf("All tests successful!\n\n");

        return 0;
}


