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

#define MYPORT "9000"
#define BACKLOG 10
#define MAXDATASIZE 1024               // chunk size for recv/send
#define DATAFILE "/var/tmp/aesdsocketdata"
#include <signal.h>

static volatile sig_atomic_t exit_requested = 0;

static void signal_handler(int signo)
{
    (void)signo;
    exit_requested = 1;
}

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
        syslog(LOG_ERR, "fork failed: %s", strerror(err));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // parent exits, child continues as daemon
        exit(EXIT_SUCCESS);
    }

    if (setsid() == -1) {
        int err = errno;
        syslog(LOG_ERR, "setsid failed: %s", strerror(err));
        exit(EXIT_FAILURE);
    }

    if (chdir("/") != 0) {
        int err = errno;
        syslog(LOG_ERR, "chdir(\"/\") failed: %s", strerror(err));
        exit(EXIT_FAILURE);
    }

    // redirect stdio to /dev/null
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

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    char buf[MAXDATASIZE];
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct addrinfo hints, *serverinfo;
    int status, sockfd;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags = AI_PASSIVE;      // fill in my IP

    status = getaddrinfo(NULL, MYPORT, &hints, &serverinfo);
    if (status != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(status));
        closelog();
        return EXIT_FAILURE;
    }

    sockfd = socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol);
    if (sockfd == -1) {
        int err = errno;
        syslog(LOG_ERR, "socket: %s", strerror(err));
        freeaddrinfo(serverinfo);
        closelog();
        return EXIT_FAILURE;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        int err = errno;
        syslog(LOG_WARNING, "setsockopt(SO_REUSEADDR) failed: %s", strerror(err));
    }

    if (bind(sockfd, serverinfo->ai_addr, serverinfo->ai_addrlen) != 0) {
        int err = errno;
        syslog(LOG_ERR, "bind: %s", strerror(err));
        freeaddrinfo(serverinfo);
        close(sockfd);
        closelog();
        return EXIT_FAILURE;
    }
    freeaddrinfo(serverinfo);

    if (listen(sockfd, BACKLOG) != 0) {
        int err = errno;
        syslog(LOG_ERR, "listen: %s", strerror(err));
        close(sockfd);
        closelog();
        return EXIT_FAILURE;
    }

    // daemonize AFTER bind/listen succeed
    if (run_as_daemon) {
        daemonize();
    }

    syslog(LOG_INFO, "server: waiting for connections on port %s", MYPORT);

    // start with a clean cumulative file
    unlink(DATAFILE);

    while (!exit_requested) {
        addr_size = sizeof their_addr;
        int new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if (new_fd < 0) {
            if (errno == EINTR) continue;  // signal interrupted accept() - loop re-test exit flag
            int err = errno;
            syslog(LOG_ERR, "accept: %s", strerror(err));
            continue; // keep server alive
        }

        // log peer IP
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
        syslog(LOG_INFO, "Accepted connection from %s", s);

        // open cumulative file (append + read)
        FILE *file_pointer = fopen(DATAFILE, "a+b");
        if (!file_pointer) {
            int err = errno;
            syslog(LOG_ERR, "fopen(%s): %s", DATAFILE, strerror(err));
            close(new_fd);
            continue;
        }

        bool saw_newline = false;

        // Receive, append, and detect newline delimiter
        for (;;) {
            ssize_t numbytes = recv(new_fd, buf, sizeof(buf), 0);
            if (numbytes < 0) {
                if (errno == EINTR) continue; // retry
                int err = errno;
                syslog(LOG_ERR, "recv: %s", strerror(err));
                goto close_client;
            }
            if (numbytes == 0) {
                // client closed without newline; we'll still send whatever we have
                break;
            }

            if (fwrite(buf, 1, (size_t)numbytes, file_pointer) != (size_t)numbytes) {
                int err = errno;
                syslog(LOG_ERR, "fwrite: %s", strerror(err));
                goto close_client;
            }

            for (ssize_t i = 0; i < numbytes; ++i) {
                if (buf[i] == '\n') { saw_newline = true; break; }
            }
            if (saw_newline) break; // respond now (avoids nc deadlock)
        }

        // Flush and send cumulative file exactly once
        fflush(file_pointer);
        if (fseek(file_pointer, 0L, SEEK_SET) != 0) {
            int err = errno;
            syslog(LOG_ERR, "fseek(SEEK_SET): %s", strerror(err));
            goto close_client;
        }

        for (;;) {
            size_t r = fread(buf, 1, sizeof(buf), file_pointer);
            if (r > 0) {
                if (send_all(new_fd, buf, r) < 0) {
                    int err = errno;
                    syslog(LOG_ERR, "send: %s", strerror(err));
                    goto close_client;
                }
            }
            if (r < sizeof(buf)) {
                if (feof(file_pointer)) { clearerr(file_pointer); break; }
                if (ferror(file_pointer)) {
                    int err = errno;
                    syslog(LOG_ERR, "fread: %s", strerror(err));
                    goto close_client;
                }
            }
        }

    close_client:
        fclose(file_pointer);
        close(new_fd);
        syslog(LOG_INFO, "Closed connection from %s", s);
        // accept next client
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    close(sockfd);
    unlink(DATAFILE);
    closelog();
    return EXIT_SUCCESS;

}

