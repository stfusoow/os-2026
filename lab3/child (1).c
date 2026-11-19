#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define SHM_DATA_SIZE 4096

typedef struct {
    size_t len;
    int done;
    char data[SHM_DATA_SIZE];
} shared_t;

static void print_error(const char *msg) {
    write(STDERR_FILENO, msg, strlen(msg));
}

static int my_itoa(long long num, char *str) {
    int i = 0;
    int is_negative = 0;
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return i;
    }
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    while (num != 0) {
        int rem = num % 10;
        str[i++] = rem + '0';
        num = num / 10;
    }
    if (is_negative) {
        str[i++] = '-';
    }
    str[i] = '\0';
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
    return i;
}

static int sem_wait_retry(sem_t *s) {
    while (sem_wait(s) == -1) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

static int send_to_parent(shared_t *shm, sem_t *sem_empty, sem_t *sem_full,
                          const char *buf, size_t len, int done) {
    if (sem_wait_retry(sem_empty) == -1) {
        return -1;
    }
    memcpy(shm->data, buf, len);
    shm->len = len;
    shm->done = done;
    if (sem_post(sem_full) == -1) {
        return -1;
    }
    return 0;
}

static int process_line(char *line, shared_t *shm, sem_t *sem_empty, sem_t *sem_full) {
    long long sum = 0;
    char *ptr = line;
    char *endptr;
    while (*ptr != '\0') {
        int val = (int)strtol(ptr, &endptr, 10);
        if (ptr == endptr) {
            break;
        }
        sum += val;
        ptr = endptr;
    }
    char out_buf[64];
    int out_len = my_itoa(sum, out_buf);
    out_buf[out_len++] = '\n';
    return send_to_parent(shm, sem_empty, sem_full, out_buf, (size_t)out_len, 0);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        print_error("child: usage: child <shm> <sem_empty> <sem_full>\n");
        return EXIT_FAILURE;
    }
    const char *shm_name = argv[1];
    const char *sem_empty_name = argv[2];
    const char *sem_full_name = argv[3];

    int shm_fd = shm_open(shm_name, O_RDWR, 0);
    if (shm_fd == -1) {
        print_error("child: failed to open shared memory\n");
        return EXIT_FAILURE;
    }
    shared_t *shm = mmap(NULL, sizeof(shared_t), PROT_READ | PROT_WRITE,
                         MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        print_error("child: failed to map shared memory\n");
        close(shm_fd);
        return EXIT_FAILURE;
    }
    close(shm_fd);

    sem_t *sem_empty = sem_open(sem_empty_name, 0);
    if (sem_empty == SEM_FAILED) {
        print_error("child: failed to open semaphore empty\n");
        munmap(shm, sizeof(shared_t));
        return EXIT_FAILURE;
    }
    sem_t *sem_full = sem_open(sem_full_name, 0);
    if (sem_full == SEM_FAILED) {
        print_error("child: failed to open semaphore full\n");
        sem_close(sem_empty);
        munmap(shm, sizeof(shared_t));
        return EXIT_FAILURE;
    }

    int status = EXIT_SUCCESS;
    char read_buf[4096];
    char line_buf[4096];
    int line_len = 0;
    ssize_t bytes_read;

    while ((bytes_read = read(STDIN_FILENO, read_buf, sizeof(read_buf))) != 0) {
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            print_error("child: failed to read input\n");
            status = EXIT_FAILURE;
            goto finish;
        }
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = read_buf[i];
            if (c == '\n' || line_len >= (int)sizeof(line_buf) - 1) {
                line_buf[line_len] = '\0';
                if (process_line(line_buf, shm, sem_empty, sem_full) == -1) {
                    print_error("child: semaphore error\n");
                    status = EXIT_FAILURE;
                    goto finish;
                }
                line_len = 0;
                if (c == '\n') {
                    continue;
                }
            }
            if (c != '\n') {
                line_buf[line_len++] = c;
            }
        }
    }
    if (line_len > 0) {
        line_buf[line_len] = '\0';
        if (process_line(line_buf, shm, sem_empty, sem_full) == -1) {
            print_error("child: semaphore error\n");
            status = EXIT_FAILURE;
            goto finish;
        }
    }

finish:
    if (send_to_parent(shm, sem_empty, sem_full, "", 0, 1) == -1) {
        print_error("child: failed to send end marker\n");
        status = EXIT_FAILURE;
    }
    sem_close(sem_empty);
    sem_close(sem_full);
    munmap(shm, sizeof(shared_t));
    return status;
}
