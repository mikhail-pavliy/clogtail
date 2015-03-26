#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>

#define BUFFERSIZE 4096
#define OFFSETEXT ".offset"
#define OFFSETSZ 7

typedef struct {
    off_t offset;
    off_t size;
    ino_t inode;
} offset_t;

void fassert(int res, char *message) {
    if (res < 0) {
        perror(message);
        exit(1);
    }
}

int main (int argc, char *argv[]) {

    unsigned char *buf, *input_fn, *offset_fn, *offset_suffix = OFFSETEXT;
    struct stat input_stat, search_stat;
    int no_offset_file, search_for_rotated_file;
    offset_t offset_data;
    glob_t glob_data;
    ssize_t rd, wr;
    off_t start;
    int res;

    if (argc < 2) {
        printf("arguments required:\n\t- path to file (required)\n\t- glob to search for rotated file (optional)\n");
        return 1;
    }

    input_fn = argv[1];
    offset_fn = malloc(strlen(input_fn) + OFFSETSZ);
    strcpy(offset_fn, input_fn);
    strcat(offset_fn, offset_suffix);

    int input_fd = open(input_fn, O_RDONLY);
    fassert(input_fd, "file open");

    res = fstat(input_fd, &input_stat);
    fassert(res, "file stat");

    no_offset_file = access(offset_fn, F_OK | R_OK | W_OK );

    int offset_fd = open(offset_fn, O_RDWR | O_CREAT);
    fassert(offset_fd, "offset file open");

    if (!no_offset_file) {

        res = read(offset_fd, &offset_data, sizeof(offset_t));
        fassert(res, "offset file read");

        if (offset_data.offset == input_stat.st_size && offset_data.inode == input_stat.st_ino)
            return 0;

        buf = malloc(BUFFERSIZE);
        if (buf == NULL) {
            perror("malloc");
            return 1;
        }

        if (offset_data.inode != input_stat.st_ino) {

            int i = 0, found = 0;

            if (argc == 2) {

                if (!glob(argv[2], GLOB_NOSORT, NULL, &glob_data))
                    for (i = 0, found = 0; i < glob_data.gl_pathc && !found; i++)
                        if(!stat(glob_data.gl_pathv[i], &search_stat))
                            found = (search_stat.st_ino == offset_data.inode);

                globfree(&glob_data);

                if (found) {
                    int globfd = open(glob_data.gl_pathv[i - 1], O_RDONLY);
                    fassert(globfd, "found file open");

                    res = lseek(globfd, offset_data.offset, SEEK_SET);
                    fassert(res, "found file lseek");

                    do {
                        rd = read(globfd, buf, BUFFERSIZE);
                        fassert(rd, "found file read");

                        wr = write(STDOUT_FILENO, buf, rd);

                    } while (rd == BUFFERSIZE);

                    close(globfd);

                }

            } else
                fputs("warning, file rotated and no glob specified", stderr);

            offset_data.inode = input_stat.st_ino;
            offset_data.offset = 0;

        } else {

            if (offset_data.size > input_stat.st_size ) {
                fputs("warning, truncated file\n", stderr);
                offset_data.offset = 0;
            }

            res = lseek(input_fd, offset_data.offset, SEEK_SET);
            fassert(res, "lseek");
        }

        do {
            rd = read(input_fd, buf, BUFFERSIZE);
            fassert(rd, "read");

            wr = write(STDOUT_FILENO, buf, rd);
            offset_data.offset += rd;

        } while (rd == BUFFERSIZE);
    }

    close(input_fd);

    offset_data.offset = input_stat.st_size;
    offset_data.inode = input_stat.st_ino;
    offset_data.size = input_stat.st_size;
    lseek(offset_fd, 0, SEEK_SET);
    write(offset_fd, &offset_data, sizeof(offset_t));
    close(offset_fd);

    return 0;
}