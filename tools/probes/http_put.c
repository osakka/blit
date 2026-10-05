#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *entry;
    char request[4096];
    char response[16384];
    size_t body_length;
    size_t used;
    ssize_t count;
    int fd = -1;
    int status = 1;

    if (argc != 4)
        return 2;
    body_length = strlen(argv[3]);
    if (body_length > 2048)
        return 2;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
    if (getaddrinfo("127.0.0.1", argv[1], &hints, &result) != 0)
        return 1;
    for (entry = result; entry != NULL; entry = entry->ai_next) {
        fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (fd >= 0 && connect(fd, entry->ai_addr, entry->ai_addrlen) == 0)
            break;
        if (fd >= 0)
            close(fd);
        fd = -1;
    }
    if (fd < 0)
        goto done;
    used = (size_t)snprintf(request, sizeof(request),
                            "PUT %s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
                            argv[2], body_length, argv[3]);
    if (used >= sizeof(request) || send(fd, request, used, 0) != (ssize_t)used)
        goto done;
    used = 0;
    while (used + 1 < sizeof(response) &&
           (count = recv(fd, response + used, sizeof(response) - used - 1, 0)) > 0)
        used += (size_t)count;
    response[used] = '\0';
    {
        char *body = strstr(response, "\r\n\r\n");
        if (body != NULL)
            fputs(body + 4, stdout);
        else
            fputs(response, stdout);
    }
    if (used > 0)
        status = 0;
done:
    if (fd >= 0)
        close(fd);
    freeaddrinfo(result);
    return status;
}
