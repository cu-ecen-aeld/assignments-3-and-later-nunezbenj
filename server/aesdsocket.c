#define _POSIX_C_SOURCE 200809L
#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdarg.h>   // <-- needed for va_list / va_start / va_end

#define MYPORT "9000"
#define BACKLOG 10
#define MAXDATASIZE 1024
#define DATAFILE "/var/tmp/aesdsocketdata"

/* ------- tiny logging helpers: mirror to stdout/stderr and syslog ------- */
static void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(ap);

    va_start(ap, fmt);
    syslog(LOG_INFO, fmt, ap);
    va_end(ap);
}

static void log_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);

    va_start(ap, fmt);
    syslog(LOG_ERR, fmt, ap);
    va_end(ap);
}
/* ----------------------------------------------------------------------- */

static ssize_t send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = send(fd, p, left, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        p += n;
        left -= n;
    }
    return (ssize_t)(len - left);
}

static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        int err = errno;
        log_err("fork failed: %s", strerror(err));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);  // parent exits

    if (setsid() == -1) {
        int err = errno;
        log_err("setsid failed: %s", strerror(err));
        exit(EXIT_FAILURE);
    }

    if (chdir("/") != 0) {
        int err = errno;
        log_err("chdir(\"/\") failed: %s", strerror(err));
        exit(EXIT_FAILURE);
    }

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        (void)dup2(fd, STDIN_FILENO);
        (void)dup2(fd, STDOUT_FILENO);
        (void)dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
}

int main(int argc, char *argv[])
{
    bool run_as_daemon = (argc == 2 && strcmp(argv[1], "-d") == 0);

    openlog("aesdsocket", LOG_PID, LOG_USER);

    char buf[MAXDATASIZE];
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct addrinfo hints, *serverinfo;
    int status, sockfd;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    status = getaddrinfo(NULL, MYPORT, &hints, &serverinfo);
    if (status != 0) {
        log_err("getaddrinfo: %s", gai_strerror(status));
        closelog();
        return EXIT_FAILURE;
    }

    sockfd = socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol);
    if (sockfd == -1) {
        int err = errno;
        log_err("socket: %s", strerror(err));
        freeaddrinfo(serverinfo);
        closelog();
        return EXIT_FAILURE;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        int err = errno;
        log_err("setsockopt(SO_REUSEADDR) failed: %s", strerror(err));
    }

    if (bind(sockfd, serverinfo->ai_addr, serverinfo->ai_addrlen) != 0) {
        int err = errno;
        log_err("bind: %s", strerror(err));
        freeaddrinfo(serverinfo);
        close(sockfd);
        closelog();
        return EXIT_FAILURE;
    }
    freeaddrinfo(serverinfo);
    log_info("bound on port %s", MYPORT);

    if (listen(sockfd, BACKLOG) != 0) {
        int err = errno;
        log_err("listen: %s", strerror(err));
        close(sockfd);
        closelog();
        return EXIT_FAILURE;
    }

    if (run_as_daemon) {
        log_info("daemonizing after successful bind/listen");
        daemonize();
    }

    log_info("server: waiting for connections on port %s", MYPORT);

    unlink(DATAFILE);
    log_info("reset data file at %s", DATAFILE);

    for (;;) {
        addr_size = sizeof their_addr;
        int new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if (new_fd < 0) {
            if (errno == EINTR) continue;
            int err = errno;
            log_err("accept: %s", strerror(err));
            continue;
        }

        char s[INET6_ADDRSTRLEN];
        void *addr;
        if (their_addr.ss_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)&their_addr;
            addr = &(ipv4->sin_addr);
        } else {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&their_addr;
            addr = &(ipv6->sin6_addr);
        }
        inet_ntop(their_addr.ss_family, addr, s, sizeof(s));
        log_info("Accepted connection from %s", s);

        FILE *file_pointer = fopen(DATAFILE, "a+b");
        if (!file_pointer) {
            int err = errno;
            log_err("fopen(%s): %s", DATAFILE, strerror(err));
            close(new_fd);
            continue;
        }

        bool saw_newline = false;
        size_t total_rx = 0, total_appended = 0;

        for (;;) {
            ssize_t numbytes = recv(new_fd, buf, sizeof(buf), 0);
            if (numbytes < 0) {
                if (errno == EINTR) continue;
                int err = errno;
                log_err("recv: %s", strerror(err));
                goto close_client;
            }
            if (numbytes == 0) {
                log_info("peer %s closed write side (EOF)", s);
                break;
            }

            total_rx += (size_t)numbytes;
            size_t w = fwrite(buf, 1, (size_t)numbytes, file_pointer);
            total_appended += w;
            if (w != (size_t)numbytes) {
                int err = errno;
                log_err("fwrite: wrote %zu of %zd bytes: %s", w, numbytes, strerror(err));
                goto close_client;
            }

            for (ssize_t i = 0; i < numbytes; ++i) {
                if (buf[i] == '\n') { saw_newline = true; break; }
            }
            if (saw_newline) {
                log_info("newline detected from %s (rx so far: %zu bytes)", s, total_rx);
                break;
            }
        }

        fflush(file_pointer);
        if (fseek(file_pointer, 0L, SEEK_SET) != 0) {
            int err = errno;
            log_err("fseek(SEEK_SET): %s", strerror(err));
            goto close_client;
        }

        size_t total_tx = 0;
        for (;;) {
            size_t r = fread(buf, 1, sizeof(buf), file_pointer);
            if (r > 0) {
                if (send_all(new_fd, buf, r) < 0) {
                    int err = errno;
                    log_err("send: %s", strerror(err));
                    goto close_client;
                }
                total_tx += r;
            }
            if (r < sizeof(buf)) {
                if (feof(file_pointer)) { clearerr(file_pointer); break; }
                if (ferror(file_pointer)) {
                    int err = errno;
                    log_err("fread: %s", strerror(err));
                    goto close_client;
                }
            }
        }
        log_info("completed reply to %s (appended %zu bytes this conn, sent %zu bytes total file)",
                 s, total_appended, total_tx);

    close_client:
        fclose(file_pointer);
        close(new_fd);
        log_info("Closed connection from %s", s);
    }

    close(sockfd);
    closelog();
    return EXIT_SUCCESS;
}

