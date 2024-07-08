#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    // Initialize syslog
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    // Check for correct number of arguments
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: %d", argc);
        fprintf(stderr, "Usage: %s <file> <string>\n", argv[0]);
        return 1;
    }

    const char *file_path = argv[1];
    const char *string_to_write = argv[2];

    // Attempt to open the file for writing
    FILE *file = fopen(file_path, "w");
    if (!file) {
        syslog(LOG_ERR, "Failed to open file: %s, Error: %s", file_path, strerror(errno));
        perror("fopen");
        return 1;
    }

    // Write the string to the file
    if (fprintf(file, "%s", string_to_write) < 0) {
        syslog(LOG_ERR, "Failed to write to file: %s, Error: %s", file_path, strerror(errno));
        fclose(file);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", string_to_write, file_path);

    // Close the file
    if (fclose(file) != 0) {
        syslog(LOG_ERR, "Failed to close file: %s, Error: %s", file_path, strerror(errno));
        return 1;
    }

    // Close syslog
    closelog();
    return 0;
}

