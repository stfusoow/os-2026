#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

static char CHILD_PROGRAM_NAME[] = "child";

void print_error(const char *msg) {
    write(STDERR_FILENO, msg, strlen(msg));
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
            exit(EXIT_FAILURE);
        }
        while (progpath[len] != '/') --len;
        progpath[len] = '\0';
    }
    int child_to_parent[2];
    if (pipe(child_to_parent) == -1) {
        print_error("failed to create pipe\n");
        exit(EXIT_FAILURE);
    }
    const pid_t child = fork();
    switch (child) {
        case -1: {
            print_error("failed to spawn new process\n");
            exit(EXIT_FAILURE);
        } break;
        case 0: {
            close(child_to_parent[0]);
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                 print_error("dup2 failed\n");
                 exit(EXIT_FAILURE);
            }
            close(input_fd);
            if (dup2(child_to_parent[1], STDOUT_FILENO) == -1) {
                 print_error("dup2 failed\n");
                 exit(EXIT_FAILURE);
            }
            close(child_to_parent[1]);
            char path[1024];
            strcpy(path, progpath);
            strcat(path, "/");
            strcat(path, CHILD_PROGRAM_NAME);
            char *const args[] = {CHILD_PROGRAM_NAME, NULL};
            int32_t status = execv(path, args);
            if (status == -1) {
                print_error("error\n");
                exit(EXIT_FAILURE);
            }
        } break;

        default: {
            close(child_to_parent[1]);
            close(input_fd);
            char buf[4096];
            ssize_t bytes;
            while ((bytes = read(child_to_parent[0], buf, sizeof(buf))) > 0) {
                write(STDOUT_FILENO, buf, bytes);
            }
            if (bytes < 0) {
                print_error("failed to read from pipe\n");
                exit(EXIT_FAILURE);
            }
            close(child_to_parent[0]);
            wait(NULL);
        } break;
    }
    return 0;
}
