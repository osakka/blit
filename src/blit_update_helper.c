#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define UPDATE_SOCKET "/run/blit/update.sock"

static int run_command(const char *command, char *const arguments[])
{
    pid_t child = fork();
    int status;
    if (child < 0) return 0;
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execv(command, arguments);
        _exit(127);
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) return 0;
    return WEXITSTATUS(status) == 0;
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

static int request_update(const char *path, const char *expected)
{
    char output[128];
    int pipe_fds[2];
    pid_t child;
    ssize_t length;
    int status;
    if (pipe(pipe_fds) < 0) return 0;
    child = fork();
    if (child < 0) return 0;
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
        strncmp(output, expected, 64) != 0 || !package_identity(path))
        return 0;
    return run_command("/usr/bin/dpkg", (char *const[]){
        (char *)"dpkg", (char *)"--install", (char *)path, NULL});
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
        if (client_fd < 0) continue;
        if (read_line(client_fd, line, sizeof(line)) &&
            sscanf(line, "UPDATE %255s %64s", path, sha) == 2 &&
            request_update(path, sha))
            write(client_fd, "OK\n", 3);
        else
            write(client_fd, "ERROR\n", 6);
        close(client_fd);
    }
}
