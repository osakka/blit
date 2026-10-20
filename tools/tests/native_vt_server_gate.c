#define main isolated_module_gate_main
#include "native_vt_kernel_gate.c"
#undef main
#include <arpa/inet.h>
#include <sys/time.h>

static char response[32 * 1024 * 1024];
static size_t response_length;
static char *http(const char *method, const char *path, const char *body)
{
    struct sockaddr_in address;
    struct timeval timeout = { .tv_sec = 3 };
    char request[4096];
    int fd, size;
    ssize_t count;
    char *start;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd); return NULL;
    }
    size = snprintf(request, sizeof(request),
        "%s %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n"
        "Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
        method, path, body ? strlen(body) : 0, body ? body : "");
    CHECK(send(fd, request, size, MSG_NOSIGNAL) == size);
    response_length = 0;
    while ((count = recv(fd, response + response_length,
                         sizeof(response) - response_length - 1, 0)) > 0)
        response_length += count;
    close(fd);
    response[response_length] = 0;
    if (strncmp(response, "HTTP/1.1 2", 10)) {
        fprintf(stderr, "VT_SERVER_HTTP_ERROR %s %s: %.400s\n", method, path, response);
        CHECK(0);
    }
    start = strstr(response, "\r\n\r\n");
    CHECK(start != NULL);
    return start + 4;
}
static void chord(int device, int function)
{
    struct input_event events[12];
    int codes[] = {KEY_LEFTCTRL, KEY_LEFTALT, function, function, KEY_LEFTALT, KEY_LEFTCTRL};
    int i;
    memset(events, 0, sizeof(events));
    for (i = 0; i < 6; ++i) {
        events[i * 2].type = EV_KEY;
        events[i * 2].code = codes[i];
        events[i * 2].value = i < 3;
        events[i * 2 + 1].type = EV_SYN;
    }
    CHECK(write(device, events, sizeof(events)) == sizeof(events));
}
static void active(int number, int wanted)
{
    int i;
    for (i = 0; i < 200; ++i) {
        char *body = http("GET", "/v1/diagnostics/console", NULL);
        struct vt_stat state;
        CHECK(ioctl(tty, VT_GETSTATE, &state) == 0);
        if (body && state.v_active == number &&
            strstr(body, wanted ? "\"active\":true" : "\"active\":false")) return;
        usleep(10000);
    }
    CHECK(0);
}
int main(void)
{
    struct termios saved, raw;
    char cmdline[4096] = {0};
    int fd = open("/proc/cmdline", O_RDONLY), device, i, status;
    pid_t server;
    char *body;
    char display[64], create[512];
    unsigned char *before;
    size_t before_length;
    CHECK(fd >= 0 && read(fd, cmdline, sizeof(cmdline) - 1) > 0);
    close(fd);
    CHECK(strstr(cmdline, " blit_vt_test=1 ") != NULL);
    tty = open("/dev/tty1", O_RDWR | O_NONBLOCK | O_NOCTTY);
    CHECK(tty >= 0 && tcgetattr(tty, &saved) == 0);
    raw = saved; cfmakeraw(&raw);
    CHECK(tcsetattr(tty, TCSANOW, &raw) == 0);
    device = keyboard();
    server = fork();
    CHECK(server >= 0);
    if (!server) {
        setenv("BLIT_DISPLAY_BACKEND", "kms", 1);
        setenv("BLIT_BIND_ADDRESS", "127.0.0.1", 1);
        setenv("BLIT_SETTINGS", "ignore", 1);
        setenv("BLIT_PSF_FONT", "/font.psf", 1);
        setenv("BLIT_LOGIN_AUTOSTART", "0", 1);
        execl("/blit-server", "/blit-server", "8080", NULL);
        _exit(127);
    }
    for (i = 0; i < 500; ++i) {
        if (http("GET", "/v1/version", NULL)) break;
        usleep(10000);
    }
    CHECK(i < 500);
    body = http("GET", "/v1/diagnostics", NULL);
    CHECK(body && strstr(body, "\"ready\":true") && strstr(body, "\"backend\":\"drm-kms\""));
    active(1, 1);
    body = http("GET", "/v1/displays", NULL);
    CHECK(body);
    {
        char *id = strstr(body, "\"id\":\"display-");
        int length = 0;
        CHECK(id); id += 6;
        while (id[length] && id[length] != '"' && length < 63) ++length;
        memcpy(display, id, length); display[length] = 0;
    }
    snprintf(create, sizeof(create),
        "{\"command\":\"/bin/busybox\",\"arguments\":[\"sh\",\"-c\",\"printf '\\\\033[?25l'; /bin/busybox cat\"],\"surface\":{\"width\":80,\"height\":24},\"display_id\":\"%s\"}", display);
    body = http("POST", "/v1/terminal-sessions", create);
    CHECK(body);
    /* The server chooses the connector ID. Bind to its discovered pane via
       the normal API rather than hard-code a virtual GPU's connector IDs. */
    puts("VT_SERVER_STAGE session_created");
    usleep(200000);
    key(device); expect_console(0);
    body = http("GET", "/v1/terminal-sessions/1", NULL);
    CHECK(body && strstr(body, "\"cursor\":{\"x\":1"));
    {
        char route[128];
        snprintf(route, sizeof(route), "/v1/diagnostics/screenshots/%s", display);
        body = http("GET", route, NULL);
        CHECK(body && body[0] == 'B' && body[1] == 'M');
        before_length = response_length - (body - response);
        before = malloc(before_length);
        CHECK(before); memcpy(before, body, before_length);
        chord(device, KEY_F2);
        active(2, 0);
        body = http("GET", "/v1/diagnostics/console", NULL);
        CHECK(body && strstr(body, "\"keyboard_muted\":false"));
        {
            int other = open("/dev/tty2", O_RDWR | O_NONBLOCK | O_NOCTTY);
            struct termios old_other, raw_other;
            struct pollfd input;
            char typed;
            CHECK(other >= 0 && tcgetattr(other, &old_other) == 0);
            raw_other = old_other; cfmakeraw(&raw_other);
            CHECK(tcsetattr(other, TCSANOW, &raw_other) == 0);
            /* Queue input while Blit's main loop cannot drain it. On return
               it must not be replayed into the desktop's PTY. */
            CHECK(kill(server, SIGSTOP) == 0);
            CHECK(waitpid(server, &status, WUNTRACED) == server && WIFSTOPPED(status));
            key(device);
            input.fd = other; input.events = POLLIN;
            CHECK(poll(&input, 1, 1000) == 1 && read(other, &typed, 1) == 1 && typed == 'a');
            expect_console(0);
            chord(device, KEY_F1);
            CHECK(kill(server, SIGCONT) == 0);
            tcsetattr(other, TCSANOW, &old_other);
            close(other);
        }
        active(1, 1);
        usleep(200000);
        body = http("GET", "/v1/terminal-sessions/1", NULL);
        CHECK(body && strstr(body, "\"cursor\":{\"x\":1"));
        body = http("GET", route, NULL);
        CHECK(body && response_length - (body - response) == before_length &&
              !memcmp(before, body, before_length));
        free(before);
    }
    body = http("POST", "/v1/control/shutdown", "{}");
    CHECK(body);
    CHECK(waitpid(server, &status, 0) == server && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    modes_restored();
    key(device); expect_console(1);
    tcsetattr(tty, TCSANOW, &saved);
    ioctl(device, UI_DEV_DESTROY); close(device); close(tty);
    puts("VT_SERVER_PASS native_kms=ready physical_evdev_pty=pass console_leak=none physical_vt_chords=pass away_keys=console_only queued_replay=none scanout_restored=byte_identical api_shutdown=pass keyboard_restored=pass");
    return 0;
}
