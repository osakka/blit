/* TCC-built WebSocket pixel-frame client for direct-KMS and live-host tests.
 * Legacy usage: probe SESSION [--60hz]
 * Remote usage: probe HOST PORT SESSION [--60hz]
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int send_all(int fd, const void *data, size_t length)
{
    const unsigned char *p = (const unsigned char *)data;
    while (length > 0) {
        ssize_t n = send(fd, p, length, 0);
        if (n <= 0) return 0;
        p += n;
        length -= (size_t)n;
    }
    return 1;
}

static int recv_all(int fd, void *data, size_t length)
{
    unsigned char *p = (unsigned char *)data;
    while (length > 0) {
        ssize_t n = recv(fd, p, length, 0);
        if (n <= 0) return 0;
        p += n;
        length -= (size_t)n;
    }
    return 1;
}

static int read_text(int fd, char *text, size_t capacity)
{
    unsigned char header[2];
    unsigned char extended[8];
    size_t length;
    if (!recv_all(fd, header, 2)) return 0;
    length = header[1] & 0x7f;
    if (length == 126) {
        if (!recv_all(fd, extended, 2)) return 0;
        length = ((size_t)extended[0] << 8) | extended[1];
    } else if (length == 127 || length >= capacity) {
        return 0;
    }
    if (!recv_all(fd, text, length)) return 0;
    text[length] = '\0';
    return (header[0] & 0x0f) == 1;
}

static int send_masked(int fd, int opcode, int final,
                       const unsigned char *payload, size_t length)
{
    unsigned char header[10];
    unsigned char mask[4] = { 0x2a, 0x17, 0x49, 0x63 };
    unsigned char *masked;
    size_t header_length = 2;
    size_t i;

    header[0] = (unsigned char)((final ? 0x80 : 0) | opcode);
    if (length < 126) {
        header[1] = (unsigned char)(0x80 | length);
    } else if (length <= 65535) {
        header[1] = 0x80 | 126;
        header[2] = (unsigned char)(length >> 8);
        header[3] = (unsigned char)length;
        header_length = 4;
    } else {
        return 0;
    }
    masked = (unsigned char *)malloc(length);
    if (masked == NULL) return 0;
    for (i = 0; i < length; ++i) masked[i] = payload[i] ^ mask[i % 4];
    if (!send_all(fd, header, header_length) ||
        !send_all(fd, mask, sizeof(mask)) || !send_all(fd, masked, length)) {
        free(masked);
        return 0;
    }
    free(masked);
    return 1;
}

int main(int argc, char **argv)
{
    struct sockaddr_in address;
    char request[512];
    char response[1024];
    const char *host = "127.0.0.1";
    int port = 19191;
    unsigned char pixels[32 * 32 * 4];
    const char *session;
    int fd;
    int stream_mode = 0;
    int frame;
    size_t i;

    if (argc < 2 || argc > 5) return 2;
    if (argc >= 4) {
        host = argv[1];
        port = atoi(argv[2]);
        session = argv[3];
        if (port < 1 || port > 65535 || session[0] == '\0') return 2;
        if (argc == 5 && strcmp(argv[4], "--60hz") != 0) return 2;
        stream_mode = argc == 5;
    } else {
        session = argv[1];
        if (argc == 3 && strcmp(argv[2], "--60hz") != 0) return 2;
        stream_mode = argc == 3;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) return 2;
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;
    snprintf(request, sizeof(request),
             "GET /v1/stream HTTP/1.1\r\nHost: %s:%d\r\n"
             "Upgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n",
             host, port);
    if (!send_all(fd, request, strlen(request))) return 1;
    /* Consume the HTTP upgrade response. */
    for (i = 0; i < sizeof(response) - 1; ++i) {
        if (!recv_all(fd, response + i, 1)) return 1;
        response[i + 1] = '\0';
        if (i >= 3 && strstr(response + i - 3, "\r\n\r\n") != NULL)
            break;
    }
    if (!read_text(fd, response, sizeof(response)) ||
        strstr(response, "stream_connected") == NULL) return 1;
    snprintf(request, sizeof(request),
             "{\"type\":\"pixel_frame_begin\",\"session_id\":\"%s\","
             "\"image_id\":91,\"width\":32,\"height\":32,"
             "\"format\":32,\"x\":16,\"y\":16,\"z\":4,\"ack\":%s}",
             session, stream_mode ? "false" : "true");
    if (!send_masked(fd, 1, 1, (unsigned char *)request, strlen(request)))
        return 1;
    do {
        if (!read_text(fd, response, sizeof(response))) return 1;
    } while (strstr(response, "pixel_frame_ready") == NULL);
    for (i = 0; i < sizeof(pixels); i += 4) {
        pixels[i] = 0xff;
        pixels[i + 1] = 0;
        pixels[i + 2] = 0;
        pixels[i + 3] = 0xff;
    }
    if (!send_masked(fd, 2, 1, pixels, sizeof(pixels))) return 1;
    if (stream_mode) {
        struct timespec interval;
        interval.tv_sec = 0;
        interval.tv_nsec = 16666667;
        for (frame = 1; frame < 60; ++frame) {
            pixels[0] = (unsigned char)(frame * 3);
            pixels[1] = (unsigned char)(frame * 5);
            if (!send_masked(fd, 2, 1, pixels, sizeof(pixels))) return 1;
            nanosleep(&interval, NULL);
        }
        /* Leave the stream alive while the gateway drains its socket. */
        sleep(1);
        puts("PIXEL_FRAME_STREAM_SENT frames=60 target_hz=60");
        close(fd);
        return 0;
    }
    do {
        if (!read_text(fd, response, sizeof(response))) return 1;
    } while (strstr(response, "pixel_frame_present") == NULL);
    puts("PIXEL_FRAME_PROBE_PASS");
    close(fd);
    return 0;
}
