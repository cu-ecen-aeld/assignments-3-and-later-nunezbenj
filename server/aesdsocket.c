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

#define MYPORT "9000"
#define BACKLOG 10
#define MAXDATASIZE 1024 // chunk size for recv/send

/**
Last hurdle Notes:

We don’t wait for recv()==0 anymore. As soon as we see a \n in the input, we send the cumulative file and close.
This matches echo ... | nc ... which writes a line and then expects a reply.

For very long strings (arriving over multiple recv()), the newline will be in the final chunk;
we’ll append all chunks first, then send once.

The cumulative file persists across connections ("a+b" and no per-connection unlink),
so each new client gets everything so far.
Persist cumulative file across connections (a+b mode, no unlink inside the loop).

Echo back once per connection, not after every recv chunk.

Detect newline (\n) so you don’t deadlock with nc, since echo always sends a newline.
**/

/**
Next steps:

* Multi-client support with pthreads.

* Daemonization (background process).

* Signal handling (SIGINT, SIGTERM) for graceful cleanup.

* File cleanup on exit.

**/

static ssize_t send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t left = len;
    while (left > 0)
    {
        ssize_t n = send(fd, p, left, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        p += n;
        left -= n;
    }
    return (ssize_t)(len - left);
}

int main(void)
{
    char buf[MAXDATASIZE];
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct addrinfo hints, *serverinfo;
    int status, sockfd;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // fill in my IP

    if ((status = getaddrinfo(NULL, MYPORT, &hints, &serverinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    sockfd = socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol);
    if (sockfd == -1)
    {
        perror("socket");
        freeaddrinfo(serverinfo);
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0)
    {
        perror("setsockopt(SO_REUSEADDR)"); // non-fatal
    }

    if (bind(sockfd, serverinfo->ai_addr, serverinfo->ai_addrlen) != 0)
    {
        perror("bind");
        freeaddrinfo(serverinfo);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(serverinfo);

    if (listen(sockfd, BACKLOG) != 0)
    {
        perror("listen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("server: waiting for connections...\n");

    // start with a clean cumulative file
    unlink("tmp.tmp");

    for (;;)
    {
        addr_size = sizeof their_addr;
        int new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if (new_fd < 0)
        {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue; // keep server alive
        }

		// syslog ip address of accepted connection
        char s[INET6_ADDRSTRLEN];
        void *addr;
        if (their_addr.ss_family == AF_INET)
        {
            // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)&their_addr;
            addr = &(ipv4->sin_addr);
        }
        else
        {
            // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&their_addr;
            addr = &(ipv6->sin6_addr);
        }
        inet_ntop(their_addr.ss_family, addr, s, sizeof(s));
        printf("Accepted connection from %s\n", s);
        syslog(LOG_INFO, "Accepted connection from %s", s);

        // open cumulative file (append + read)
        FILE *file_pointer = fopen("tmp.tmp", "a+b");
        if (!file_pointer)
        {
            perror("fopen");
            close(new_fd);
            continue;
        }

        bool saw_newline = false;

        // Receive, append, and detect newline delimiter
        for (;;)
        {
            ssize_t numbytes = recv(new_fd, buf, sizeof(buf), 0);
            if (numbytes < 0)
            {
                if (errno == EINTR)
                    continue; // retry
                perror("recv");
                goto close_client;
            }
            if (numbytes == 0)
            {
                // client closed without newline; we'll still send whatever we have
                break;
            }

            // append exactly what we received
            if (fwrite(buf, 1, (size_t)numbytes, file_pointer) != (size_t)numbytes)
            {
                perror("fwrite");
                goto close_client;
            }

            // check if this chunk contains a newline
            for (ssize_t i = 0; i < numbytes; ++i)
            {
                if (buf[i] == '\n')
                {
                    saw_newline = true;
                    break;
                }
            }

            if (saw_newline)
                break; // we can respond now (avoid nc deadlock)
        }

        // Flush and send cumulative file exactly once
        fflush(file_pointer);
        if (fseek(file_pointer, 0L, SEEK_SET) != 0)
        {
            perror("fseek SEEK_SET");
            goto close_client;
        }

        for (;;)
        {
            size_t r = fread(buf, 1, sizeof(buf), file_pointer);
            if (r > 0)
            {
                if (send_all(new_fd, buf, r) < 0)
                {
                    perror("send");
                    goto close_client;
                }
            }
            if (r < sizeof(buf))
            {
                if (feof(file_pointer))
                {
                    clearerr(file_pointer);
                    break;
                }
                if (ferror(file_pointer))
                {
                    perror("fread");
                    goto close_client;
                }
            }
        }

    close_client:
        fclose(file_pointer);
        close(new_fd);
        // accept next client
    }

    close(sockfd);
    return 0;
}
