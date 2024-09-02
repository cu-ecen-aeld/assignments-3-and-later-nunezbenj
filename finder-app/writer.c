#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <syslog.h>

void create_directory(const char *path) {
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Error: Two arguments are required.\n");
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    char dirpath[256];
    snprintf(dirpath, sizeof(dirpath), "%s", writefile);

    // Get the directory path
    char *last_slash = strrchr(dirpath, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        create_directory(dirpath);
    }

    // Open syslog
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        int errnum = errno;  // Capture errno immediately
        fprintf(stderr, "Error: Failed to create or open the file '%s': %s\n", writefile, strerror(errnum));
        syslog(LOG_ERR, "Failed to create or open the file '%s': %s", writefile, strerror(errnum));
        closelog();
        return 1;
    }

    if (fprintf(file, "%s", writestr) < 0) {
        int errnum = errno;  // Capture errno immediately
        fprintf(stderr, "Error: The file '%s' could not be written to: %s\n", writefile, strerror(errnum));
        syslog(LOG_ERR, "The file '%s' could not be written to: %s", writefile, strerror(errnum));
        fclose(file);
        closelog();
        return 1;
    }

    fclose(file);

	syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    printf("Writing %s to %s\n", writestr, writefile);
    return 0;
}

