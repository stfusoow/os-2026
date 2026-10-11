#include <unistd.h>
#include <stdlib.h>
#include <string.h>
int my_itoa(long long num, char *str) {
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
int main() {
    char read_buf[4096];
    char line_buf[4096];
    int line_len = 0;
    ssize_t bytes_read;
    while ((bytes_read = read(STDIN_FILENO, read_buf, sizeof(read_buf))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = read_buf[i];
            if (c == '\n' || line_len >= (int)sizeof(line_buf) - 1) {
                line_buf[line_len] = '\0';
                long long sum = 0;
                char *ptr = line_buf;
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
                write(STDOUT_FILENO, out_buf, out_len);
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
        long long sum = 0;
        char *ptr = line_buf;
        char *endptr;
        while (*ptr != '\0') {
            int val = (int)strtol(ptr, &endptr, 10);
            if (ptr == endptr) break;
            sum += val;
            ptr = endptr;
        }
        char out_buf[64];
        int out_len = my_itoa(sum, out_buf);
        out_buf[out_len++] = '\n';
        write(STDOUT_FILENO, out_buf, out_len);
    }
    return 0;
}
