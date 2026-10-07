#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define UPDATE_SOCKET "/run/blit/update.sock"
#define COMMAND_DETAIL_CAPACITY 384

static void safe_detail(char *detail)
{
    size_t i;
    for (i = 0; detail[i] != '\0'; ++i) {
        unsigned char byte = (unsigned char)detail[i];
        if (!((byte >= 'a' && byte <= 'z') ||
              (byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') ||
              byte == ' ' || byte == '.' || byte == ',' || byte == ':' ||
              byte == ';' || byte == '_' || byte == '/' || byte == '-' ||
              byte == '(' || byte == ')' || byte == '='))
            detail[i] = ' ';
    }
}

static int run_command(const char *command, char *const arguments[],
                       char *detail, size_t detail_capacity)
{
    int pipe_fds[2];
    pid_t child;
    int status;
    size_t used = 0;
    char buffer[128];
    ssize_t count;

    if (pipe(pipe_fds) < 0) {
        snprintf(detail, detail_capacity, "cannot create diagnostic pipe");
        return 0;
    }
    child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        snprintf(detail, detail_capacity, "cannot fork update command");
        return 0;
    }
    if (child == 0) {
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_fds[1], STDERR_FILENO) < 0)
            _exit(127);
        close(pipe_fds[1]);
        execv(command, arguments);
        dprintf(STDERR_FILENO, "exec failed errno=%d", errno);
        _exit(127);
    }
    close(pipe_fds[1]);
    while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
        size_t copy = (size_t)count;
        if (used + copy >= detail_capacity)
            copy = detail_capacity - used - 1;
        if (copy > 0) {
            memcpy(detail + used, buffer, copy);
            used += copy;
        }
    }
    close(pipe_fds[0]);
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
        snprintf(detail, detail_capacity, "update command did not exit normally");
        return 0;
    }
    detail[used] = '\0';
    safe_detail(detail);
    while (used > 0 && detail[used - 1] == ' ')
        detail[--used] = '\0';
    if (WEXITSTATUS(status) != 0) {
        if (used == 0)
            snprintf(detail, detail_capacity, "exit status %d", WEXITSTATUS(status));
        return 0;
    }
    return 1;
}

static int package_identity(const char *path)
{
    int pipe_fds[2];
    pid_t child;
    char output[64];
    ssize_t length;
    int status;
    if (pipe(pipe_fds) < 0) return 0;
    child = fork();
    if (child < 0) return 0;
    if (child == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[0]); close(pipe_fds[1]);
        execl("/usr/bin/dpkg-deb", "dpkg-deb", "--field", path,
              "Package", (char *)NULL);
        _exit(127);
    }
    close(pipe_fds[1]);
    length = read(pipe_fds[0], output, sizeof(output) - 1);
    close(pipe_fds[0]);
    if (waitpid(child, &status, 0) != child || length < 0 ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 0;
    output[length] = '\0';
    if (length > 0 && output[length - 1] == '\n') output[length - 1] = '\0';
    return strcmp(output, "blit") == 0;
}

static int request_update(const char *path, const char *expected,
                          char *detail, size_t detail_capacity)
{
    char output[128];
    int pipe_fds[2];
    pid_t child;
    ssize_t length;
    int status;
    if (pipe(pipe_fds) < 0) {
        snprintf(detail, detail_capacity, "cannot verify package checksum");
        return 0;
    }
    child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        snprintf(detail, detail_capacity, "cannot verify package checksum");
        return 0;
    }
    if (child == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[0]); close(pipe_fds[1]);
        execl("/usr/bin/sha256sum", "sha256sum", path, (char *)NULL);
        _exit(127);
    }
    close(pipe_fds[1]);
    length = read(pipe_fds[0], output, sizeof(output) - 1);
    close(pipe_fds[0]);
    if (waitpid(child, &status, 0) != child || length < 64 ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        strncmp(output, expected, 64) != 0) {
        snprintf(detail, detail_capacity, "SHA-256 verification failed");
        return 0;
    }
    if (!package_identity(path)) {
        snprintf(detail, detail_capacity, "package identity check failed");
        return 0;
    }
    return run_command("/usr/bin/dpkg", (char *const[]){
        (char *)"dpkg", (char *)"--install", (char *)path, NULL},
        detail, detail_capacity);
}

static int read_line(int fd, char *line, size_t capacity)
{
    size_t used = 0;
    while (used + 1 < capacity) {
        char byte;
        ssize_t count = read(fd, &byte, 1);
        if (count <= 0) return 0;
        line[used++] = byte;
        if (byte == '\n') break;
    }
    line[used] = '\0';
    return used > 0 && line[used - 1] == '\n';
}

int main(void)
{
    struct sockaddr_un address;
    int server_fd;
    mkdir("/run/blit", 0755);
    unlink(UPDATE_SOCKET);
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return 1;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", UPDATE_SOCKET);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        chmod(UPDATE_SOCKET, 0660) < 0 || listen(server_fd, 4) < 0)
        return 1;
    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        char line[512];
        char path[256];
        char sha[65];
        char detail[COMMAND_DETAIL_CAPACITY];
        int updated = 0;
        if (client_fd < 0) continue;
        detail[0] = '\0';
        if (read_line(client_fd, line, sizeof(line)) &&
            sscanf(line, "UPDATE %255s %64s", path, sha) == 2 &&
            request_update(path, sha, detail, sizeof(detail)))
            updated = 1;
        else {
            char response[COMMAND_DETAIL_CAPACITY + 8];
            if (detail[0] == '\0')
                snprintf(detail, sizeof(detail), "invalid update request");
            snprintf(response, sizeof(response), "ERROR %s\n", detail);
            write(client_fd, response, strlen(response));
        }
        if (updated)
            write(client_fd, "OK\n", 3);
        close(client_fd);
        if (updated) {
            close(server_fd);
            unlink(UPDATE_SOCKET);
            return 0;
        }
    }
}
