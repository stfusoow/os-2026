#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>

#define SHM_DATA_SIZE 4096

typedef struct {
    size_t len;
    int done;
    char data[SHM_DATA_SIZE];
} shared_t;

static char CHILD_PROGRAM_NAME[] = "child";

static void print_error(const char *msg) {
    write(STDERR_FILENO, msg, strlen(msg));
}

static int sem_wait_retry(sem_t *s) {
    while (sem_wait(s) == -1) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

int main() {
    char filename[256];
    ssize_t bytes_read = read(STDIN_FILENO, filename, sizeof(filename) - 1);
    if (bytes_read <= 0) {
        print_error("failed to read filename\n");
        exit(EXIT_FAILURE);
    }
    filename[bytes_read] = '\0';
    if (filename[bytes_read - 1] == '\n') {
        filename[bytes_read - 1] = '\0';
    }

    int input_fd = open(filename, O_RDONLY);
    if (input_fd == -1) {
        print_error("failed to open input file\n");
        exit(EXIT_FAILURE);
    }

    char progpath[1024];
    {
        ssize_t len = readlink("/proc/self/exe", progpath, sizeof(progpath) - 1);
        if (len == -1) {
            print_error("failed to read full program path\n");
            close(input_fd);
            exit(EXIT_FAILURE);
        }
        progpath[len] = '\0';
        while (len > 0 && progpath[len] != '/') --len;
        progpath[len] = '\0';
    }

    char shm_name[64], sem_empty_name[64], sem_full_name[64];
    pid_t self = getpid();
    snprintf(shm_name, sizeof(shm_name), "/lab3_shm_%d", (int)self);
    snprintf(sem_empty_name, sizeof(sem_empty_name), "/lab3_sem_empty_%d", (int)self);
    snprintf(sem_full_name, sizeof(sem_full_name), "/lab3_sem_full_%d", (int)self);

    int shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd == -1) {
        print_error("failed to create shared memory\n");
        close(input_fd);
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(shared_t)) == -1) {
        print_error("failed to resize shared memory\n");
        close(shm_fd);
        shm_unlink(shm_name);
        close(input_fd);
        exit(EXIT_FAILURE);
    }
    shared_t *shm = mmap(NULL, sizeof(shared_t), PROT_READ | PROT_WRITE,
                         MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        print_error("failed to map shared memory\n");
        close(shm_fd);
        shm_unlink(shm_name);
        close(input_fd);
        exit(EXIT_FAILURE);
    }
    close(shm_fd);
    shm->len = 0;
    shm->done = 0;

    sem_t *sem_empty = sem_open(sem_empty_name, O_CREAT | O_EXCL, 0600, 1);
    if (sem_empty == SEM_FAILED) {
        print_error("failed to create semaphore empty\n");
        munmap(shm, sizeof(shared_t));
        shm_unlink(shm_name);
        close(input_fd);
        exit(EXIT_FAILURE);
    }
    sem_t *sem_full = sem_open(sem_full_name, O_CREAT | O_EXCL, 0600, 0);
    if (sem_full == SEM_FAILED) {
        print_error("failed to create semaphore full\n");
        sem_close(sem_empty);
        sem_unlink(sem_empty_name);
        munmap(shm, sizeof(shared_t));
        shm_unlink(shm_name);
        close(input_fd);
        exit(EXIT_FAILURE);
    }

    int exit_code = EXIT_SUCCESS;

    const pid_t child = fork();
    switch (child) {
        case -1: {
            print_error("failed to spawn new process\n");
            exit_code = EXIT_FAILURE;
        } break;
        case 0: {
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                print_error("dup2 failed\n");
                exit(EXIT_FAILURE);
            }
            close(input_fd);

            char path[1024];
            strcpy(path, progpath);
            strcat(path, "/");
            strcat(path, CHILD_PROGRAM_NAME);
            char *const args[] = {CHILD_PROGRAM_NAME, shm_name, sem_empty_name,
                                  sem_full_name, NULL};
            execv(path, args);
            print_error("error: failed to exec child\n");
            exit(EXIT_FAILURE);
        } break;

        default: {
            close(input_fd);
            for (;;) {
                if (sem_wait_retry(sem_full) == -1) {
                    print_error("sem_wait failed\n");
                    exit_code = EXIT_FAILURE;
                    break;
                }
                if (shm->done) {
                    break;
                }
                write(STDOUT_FILENO, shm->data, shm->len);
                if (sem_post(sem_empty) == -1) {
                    print_error("sem_post failed\n");
                    exit_code = EXIT_FAILURE;
                    break;
                }
            }
            int status;
            if (wait(&status) == -1) {
                print_error("wait failed\n");
                exit_code = EXIT_FAILURE;
            } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                exit_code = EXIT_FAILURE;
            }
        } break;
    }

    sem_close(sem_empty);
    sem_close(sem_full);
    sem_unlink(sem_empty_name);
    sem_unlink(sem_full_name);
    munmap(shm, sizeof(shared_t));
    shm_unlink(shm_name);
    if (child == -1) {
        close(input_fd);
    }
    return exit_code;
}
