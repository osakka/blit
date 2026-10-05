/* Minimal TCC-built WebSocket observer for the QEMU input gate. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int send_all(int fd, const void *data, size_t length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    while (length > 0) {
        ssize_t sent = send(fd, bytes, length, 0);
        if (sent <= 0) return 0;
        bytes += sent;
        length -= (size_t)sent;
    }
    return 1;
}

static int recv_all(int fd, void *data, size_t length)
{
    unsigned char *bytes = (unsigned char *)data;
    while (length > 0) {
        ssize_t received = recv(fd, bytes, length, 0);
        if (received <= 0) return 0;
        bytes += received;
        length -= (size_t)received;
    }
    return 1;
}

static int send_text(int fd, const char *text)
{
    unsigned char frame[512];
    unsigned char mask[4] = { 0x31, 0x27, 0x19, 0x07 };
    size_t length = strlen(text);
    size_t i;
    if (length > 125) return 0;
    frame[0] = 0x81;
    frame[1] = (unsigned char)(0x80 | length);
    memcpy(frame + 2, mask, sizeof(mask));
    for (i = 0; i < length; ++i)
        frame[6 + i] = (unsigned char)text[i] ^ mask[i % 4];
    return send_all(fd, frame, length + 6);
}

int main(void)
{
    struct sockaddr_in address;
    struct timeval wait;
    fd_set readable;
    char request[512];
    char headers[2048];
    unsigned char header[2];
    unsigned char payload[2048];
    int fd;
    size_t used = 0;
    time_t deadline = time(NULL) + 50;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(19191);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        return 1;
    snprintf(request, sizeof(request),
             "GET /v1/stream HTTP/1.1\r\nHost: 127.0.0.1\r\n"
             "Upgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
    if (!send_all(fd, request, strlen(request))) return 1;
    while (used + 1 < sizeof(headers)) {
        if (!recv_all(fd, headers + used, 1)) return 1;
        ++used;
        headers[used] = '\0';
        if (used >= 4 && strstr(headers + used - 4, "\r\n\r\n") != NULL)
            break;
    }
    if (!send_text(fd, "{\"type\":\"subscribe\",\"subscriptions\":[\"input\"]}"))
        return 1;
    while (time(NULL) < deadline) {
        int ready;
        size_t length;
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        wait.tv_sec = 1;
        wait.tv_usec = 0;
        ready = select(fd + 1, &readable, NULL, NULL, &wait);
        if (ready <= 0) continue;
        if (!recv_all(fd, header, sizeof(header))) break;
        length = header[1] & 0x7f;
        if (length == 126) {
            unsigned char extended[2];
            if (!recv_all(fd, extended, sizeof(extended))) break;
            length = ((size_t)extended[0] << 8) | extended[1];
        } else if (length == 127) {
            break;
        }
        if (length >= sizeof(payload) || !recv_all(fd, payload, length))
            break;
        payload[length] = '\0';
        puts((char *)payload);
        fflush(stdout);
    }
    close(fd);
    return 0;
}
