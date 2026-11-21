#define _GNU_SOURCE 1
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
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <sys/select.h>

#ifndef UPDATE_SOCKET
#define UPDATE_SOCKET "/run/blit/update.sock"
#endif
#ifndef UPDATE_RUNTIME_DIR
#define UPDATE_RUNTIME_DIR "/run/blit"
#endif
#ifndef UPDATE_STAGING_DIR
#define UPDATE_STAGING_DIR "/var/lib/blit/updates"
#endif
/* SR-10: the helper installs its own copy of each staged package, made
   before any check, in a directory only it can write. Not under
   /var/lib/blit: under the unprivileged profile that is the server's own
   (StateDirectory=blit), and the staging directory is group-writable. */
#ifndef UPDATE_PRIVATE_DIR
#define UPDATE_PRIVATE_DIR "/var/lib/blit-update-helper"
#endif
#define COMMAND_DETAIL_CAPACITY 384
/* SR-12: every command is bounded. dpkg --install gets ten minutes, far
   longer than any real install; the rest are quick. The server's own wait
   (BLIT_UPDATE_INSTALL_TIMEOUT_US) is longer than all of them together. */
#define UPDATE_COMMAND_SECONDS 30
#ifndef UPDATE_INSTALL_SECONDS
#define UPDATE_INSTALL_SECONDS 600
#endif
/* The server takes at most 64 MiB for the whole upload. */
#define UPDATE_MAX_PACKAGE_BYTES (64L * 1024 * 1024)

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
              byte == '(' || byte == ')' || byte == '=' ||
              byte == '+' || byte == '~'))
            detail[i] = ' ';
    }
}

static int run_command_bounded(const char *command, char *const arguments[],
                       char *detail, size_t detail_capacity, int timeout_seconds)
{
    int pipe_fds[2];
    pid_t child;
    int status;
    size_t used = 0;
    char buffer[128];
    ssize_t count;
    struct timespec started, now;
    int reaped = 0;
    int cut = 0;

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
        setpgid(0, 0);
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_fds[1], STDERR_FILENO) < 0)
            _exit(127);
        close(pipe_fds[1]);
        execv(command, arguments);
        dprintf(STDERR_FILENO, "exec failed errno=%d", errno);
        _exit(127);
    }
    setpgid(child, child);
    close(pipe_fds[1]);
    clock_gettime(CLOCK_MONOTONIC, &started);
    for (;;) {
        fd_set readable;
        struct timeval delay = {0, 100000};
        int ready;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - started.tv_sec >= timeout_seconds) goto timed_out;
        FD_ZERO(&readable); FD_SET(pipe_fds[0], &readable);
        ready = select(pipe_fds[0] + 1, &readable, NULL, NULL, &delay);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { if (ready < 0) break; continue; }
        count = read(pipe_fds[0], buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        /* SR-21: keep the last bytes, not the first. dpkg ends with its
           error ("... post-installation script subprocess returned error
           exit status 1"); it starts with "(Reading database ...". */
        {
            size_t copy = (size_t)count, room = detail_capacity - 1;
            const char *from = buffer;
            if (copy >= room) {
                cut = used > 0 || copy > room;
                from += copy - room;
                copy = room;
                used = 0;
            } else if (used + copy > room) {
                size_t drop = used + copy - room;
                memmove(detail, detail + drop, used - drop);
                used -= drop;
                cut = 1;
            }
            memcpy(detail + used, from, copy);
            used += copy;
        }
    }
    for (;;) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) { reaped = 1; break; }
        if (result < 0 && errno != EINTR) break;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - started.tv_sec >= timeout_seconds) goto timed_out;
        usleep(10000);
    }
    close(pipe_fds[0]);
    if ((!reaped && waitpid(child, &status, 0) != child) || !WIFEXITED(status)) {
        snprintf(detail, detail_capacity, "update command did not exit normally");
        return 0;
    }
    detail[used] = '\0';
    safe_detail(detail);
    if (cut && used > 3)
        memcpy(detail, "...", 3);
    while (used > 0 && detail[used - 1] == ' ')
        detail[--used] = '\0';
    if (WEXITSTATUS(status) != 0) {
        if (used == 0)
            snprintf(detail, detail_capacity, "exit status %d", WEXITSTATUS(status));
        return 0;
    }
    return 1;
timed_out:
    kill(-child, SIGKILL);
    close(pipe_fds[0]);
    /* Userspace command failures are bounded. A kernel D-state task is not
       promised recoverable; do not block this helper waiting for one. */
    for (status = 0; status < 100; ++status) {
        pid_t result = waitpid(child, NULL, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) break;
        usleep(10000);
    }
    snprintf(detail, detail_capacity,
             "%s did not finish within %d s and was stopped", arguments[0],
             timeout_seconds);
    return 0;
}
static int run_command(const char *command, char *const arguments[],
                       char *detail, size_t detail_capacity)
{
    return run_command_bounded(command, arguments, detail, detail_capacity,
                               UPDATE_COMMAND_SECONDS);
}

/* ADR 0232: an update installs blit together with every installed package
   that pins blit's version -- today blit-compositor, which depends on
   blit (= its own version). This helper is the one place that rule lives. */
#define UPDATE_MAX_PACKAGES 2

typedef struct {
    char path[256];             /* the staged file, as the server named it */
    char copy[256];             /* the helper's own copy: what it checks and installs */
    char sha256[65];
    char name[64];
    char version[128];
} UpdatePackage;

static int package_field(const char *path, const char *field,
                         char *out, size_t capacity)
{
    char detail[COMMAND_DETAIL_CAPACITY];
    if (!run_command("/usr/bin/dpkg-deb", (char *const[]){
            (char *)"dpkg-deb", (char *)"--field", (char *)path,
            (char *)field, NULL}, detail, sizeof(detail)) ||
        detail[0] == '\0' || strchr(detail, ' ') != NULL ||
        strlen(detail) >= capacity)
        return 0;
    snprintf(out, capacity, "%s", detail);
    return 1;
}

static int sha256_matches(const char *path, const char *expected)
{
    char detail[COMMAND_DETAIL_CAPACITY];
    return run_command("/usr/bin/sha256sum", (char *const[]){
            (char *)"sha256sum", (char *)path, NULL},
            detail, sizeof(detail)) &&
        strlen(expected) == 64 && strncmp(detail, expected, 64) == 0;
}

/* Present: dpkg knows the package in any state that has its files or its
   dependency on blit -- everything but not-installed and config-files. */
static int package_present(const char *name)
{
    char detail[COMMAND_DETAIL_CAPACITY];
    if (!run_command("/usr/bin/dpkg-query", (char *const[]){
            (char *)"dpkg-query", (char *)"--show",
            (char *)"--showformat=${db:Status-Status}", (char *)name, NULL},
            detail, sizeof(detail)))
        return 0;
    return detail[0] != '\0' && strcmp(detail, "not-installed") != 0 &&
        strcmp(detail, "config-files") != 0;
}

static int installed_version(const char *name, char *out, size_t capacity)
{
    char detail[COMMAND_DETAIL_CAPACITY];
    if (!run_command("/usr/bin/dpkg-query", (char *const[]){
            (char *)"dpkg-query", (char *)"--show",
            (char *)"--showformat=${Version}", (char *)name, NULL},
            detail, sizeof(detail)) ||
        detail[0] == '\0' || strchr(detail, ' ') != NULL ||
        strlen(detail) >= capacity)
        return 0;
    memcpy(out, detail, strlen(detail) + 1);
    return 1;
}

static int version_lower(const char *version, const char *than)
{
    char detail[COMMAND_DETAIL_CAPACITY];
    return run_command("/usr/bin/dpkg", (char *const[]){
            (char *)"dpkg", (char *)"--compare-versions", (char *)version,
            (char *)"lt", (char *)than, NULL}, detail, sizeof(detail));
}

/* The private directory exists, is a directory (not a link), is the
   helper's, and no one else may enter it. Whatever is in it is a copy an
   earlier request left behind (requests are served one at a time), so it
   goes. */
static int private_directory_ready(void)
{
    struct stat status;
    DIR *directory;
    struct dirent *entry;
    if (mkdir(UPDATE_PRIVATE_DIR, 0700) < 0 && errno != EEXIST) return 0;
    if (lstat(UPDATE_PRIVATE_DIR, &status) < 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != geteuid() || (status.st_mode & 077) != 0)
        return 0;
    directory = opendir(UPDATE_PRIVATE_DIR);
    if (directory == NULL) return 0;
    while ((entry = readdir(directory)) != NULL)
        if (strncmp(entry->d_name, "update-", 7) == 0)
            unlinkat(dirfd(directory), entry->d_name, 0);
    closedir(directory);
    return 1;
}

/* SR-10: one open of the staged file, never through a link, copied into
   the private directory. Every check and the install use the copy, so
   what is installed is exactly what was checked, whatever happens to the
   staged file afterwards. */
static int copy_staged(UpdatePackage *package)
{
    struct stat status;
    char buffer[65536];
    ssize_t count;
    long total = 0;
    int in, out;
    package->copy[0] = '\0';
    in = open(package->path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (in < 0) return 0;
    if (fstat(in, &status) < 0 || !S_ISREG(status.st_mode) ||
        status.st_size > UPDATE_MAX_PACKAGE_BYTES) {
        close(in);
        return 0;
    }
    snprintf(package->copy, sizeof(package->copy), "%s/update-XXXXXX",
             UPDATE_PRIVATE_DIR);
    out = mkostemp(package->copy, O_CLOEXEC);
    if (out < 0) {
        package->copy[0] = '\0';
        close(in);
        return 0;
    }
    for (;;) {
        count = read(in, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        total += count;
        if (total > UPDATE_MAX_PACKAGE_BYTES ||
            write(out, buffer, (size_t)count) != count) {
            count = -1;
            break;
        }
    }
    close(in);
    if (close(out) < 0 || count < 0) {
        unlink(package->copy);
        package->copy[0] = '\0';
        return 0;
    }
    return 1;
}

/* Only a file the server staged: UPDATE_STAGING_DIR/blit-update-<name>,
   no further directories. The helper is root, and it deletes what it was
   handed, so it must never be handed anything else. */
static int staged_path(const char *path)
{
    static const char prefix[] = UPDATE_STAGING_DIR "/blit-update-";
    const char *name = path + sizeof(prefix) - 1;
    return strncmp(path, prefix, sizeof(prefix) - 1) == 0 &&
        name[0] != '\0' && strchr(name, '/') == NULL;
}

static int request_update(UpdatePackage *packages, int count,
                          char *detail, size_t detail_capacity)
{
    char *arguments[4 + UPDATE_MAX_PACKAGES];
    char installed[128];
    int i, blit = -1, compositor = -1, argc = 0;

    for (i = 0; i < count; ++i)
        if (!staged_path(packages[i].path)) {
            snprintf(detail, detail_capacity, "not a staged update package");
            return 0;
        }
    if (!private_directory_ready()) {
        snprintf(detail, detail_capacity,
                 "the helper's private directory " UPDATE_PRIVATE_DIR
                 " is not its own");
        return 0;
    }
    for (i = 0; i < count; ++i)
        if (!copy_staged(&packages[i])) {
            snprintf(detail, detail_capacity,
                     "cannot copy the staged package (not a regular file, or too large)");
            return 0;
        }
    for (i = 0; i < count; ++i) {
        UpdatePackage *package = &packages[i];
        if (!sha256_matches(package->copy, package->sha256)) {
            snprintf(detail, detail_capacity, "SHA-256 verification failed");
            return 0;
        }
        if (!package_field(package->copy, "Package", package->name,
                           sizeof(package->name)) ||
            !package_field(package->copy, "Version", package->version,
                           sizeof(package->version))) {
            snprintf(detail, detail_capacity, "package identity check failed");
            return 0;
        }
        if (strcmp(package->name, "blit") == 0 && blit < 0) blit = i;
        else if (strcmp(package->name, "blit-compositor") == 0 && compositor < 0)
            compositor = i;
        else {
            snprintf(detail, detail_capacity, "package identity check failed");
            return 0;
        }
    }
    if (blit < 0) {
        snprintf(detail, detail_capacity, "the update must carry blit");
        return 0;
    }
    if (compositor >= 0 &&
        strcmp(packages[compositor].version, packages[blit].version) != 0) {
        snprintf(detail, detail_capacity,
                 "blit and blit-compositor versions differ");
        return 0;
    }
    if (package_present("blit-compositor")) {
        if (compositor < 0) {
            snprintf(detail, detail_capacity,
                     "blit-compositor is installed; the update must carry it");
            return 0;
        }
    } else {
        compositor = -1;   /* never add a package the host did not choose */
    }
#ifdef UPDATE_GATE_AFTER_VERIFY
    /* update_pair_gate's build only; tools/build-tcc.sh never defines it.
       The gate's command replaces the staged files here, after every check
       and before the install: the window SR-10 is about. */
    if (system(UPDATE_GATE_AFTER_VERIFY) != 0) {
        snprintf(detail, detail_capacity, "gate hook failed");
        return 0;
    }
#endif
    /* --abort-after=1: dpkg stops at the first package it refuses, so a
       refusal of the first leaves the second as it was.
       Order (SR-13): blit Breaks every blit-compositor newer than itself,
       checked against the compositor installed when blit is unpacked. When
       the installed one is not newer (an update), blit goes first, and
       anything that refuses blit (an old Hibr, a package depending on a
       retired one) leaves the host untouched. When it is newer (a
       rollback), the compositor goes first, to blit's version, and then
       blit's unpack finds it allowed. */
    arguments[argc++] = (char *)"dpkg";
    arguments[argc++] = (char *)"--install";
    arguments[argc++] = (char *)"--abort-after=1";
    if (compositor >= 0 && installed_version("blit-compositor", installed,
                                             sizeof(installed)) &&
        version_lower(packages[blit].version, installed)) {
        arguments[argc++] = packages[compositor].copy;
        arguments[argc++] = packages[blit].copy;
    } else {
        arguments[argc++] = packages[blit].copy;
        if (compositor >= 0) arguments[argc++] = packages[compositor].copy;
    }
    arguments[argc] = NULL;
    return run_command_bounded("/usr/bin/dpkg", arguments, detail,
                               detail_capacity, UPDATE_INSTALL_SECONDS);
}

static int read_line(int fd, char *line, size_t capacity)
{
    size_t used = 0;
    struct timespec started, now;
    clock_gettime(CLOCK_MONOTONIC, &started);
    while (used + 1 < capacity) {
        char byte;
        ssize_t count = read(fd, &byte, 1);
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - started.tv_sec >= 3) return 0;
        if (count <= 0) return 0;
        line[used++] = byte;
        if (byte == '\n') break;
    }
    line[used] = '\0';
    return used > 0 && line[used - 1] == '\n';
}

int main(int argc, char **argv)
{
    struct sockaddr_un address;
    int server_fd;
    signal(SIGPIPE, SIG_IGN);
    (void)argv;
    if (argc != 1) return 2;
    mkdir(UPDATE_RUNTIME_DIR, 0755);
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
        char line[1024];
        UpdatePackage packages[UPDATE_MAX_PACKAGES];
        int count;
        char detail[COMMAND_DETAIL_CAPACITY];
        int updated = 0;
        struct timeval socket_timeout = {3, 0};
        if (client_fd < 0) continue;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout, sizeof(socket_timeout));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout));
        detail[0] = '\0';
        if (!read_line(client_fd, line, sizeof(line))) {
            close(client_fd); continue;
        }
        /* UPDATE <path> <sha256> [<path> <sha256>] */
        memset(packages, 0, sizeof(packages));
        {
            int consumed = 0, fields;
            fields = sscanf(line, "UPDATE %255s %64s %n", packages[0].path,
                            packages[0].sha256, &consumed);
            count = fields == 2 ? 1 : 0;
            if (count == 1 && line[consumed] != '\0' &&
                line[consumed] != '\n') {
                int tail = 0;
                if (sscanf(line + consumed, "%255s %64s %n", packages[1].path,
                           packages[1].sha256, &tail) == 2 &&
                    line[consumed + tail] == '\0')
                    count = 2;
                else
                    count = 0;
            }
        }
        if (count > 0 && request_update(packages, count, detail, sizeof(detail)))
            updated = 1;
        /* Installed or refused, a staged package has served its purpose;
           every deployment used to leave one behind (ADR 0232). So has
           the helper's copy. */
        for (int i = 0; i < count; ++i) {
            if (staged_path(packages[i].path))
                unlink(packages[i].path);
            if (packages[i].copy[0] != '\0')
                unlink(packages[i].copy);
        }
        if (!updated) {
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
            char restart_detail[COMMAND_DETAIL_CAPACITY];
            /* The package may have changed the service unit itself. Reload
               the manager and perform a real unit restart so User=, groups,
               limits, and sandbox changes take effect; an in-process exec of
               blit-server cannot change those credentials. */
            restart_detail[0] = '\0';
            (void)run_command("/usr/bin/systemctl",
                (char *const[]){(char *)"systemctl", (char *)"daemon-reload",
                                 NULL}, restart_detail,
                sizeof(restart_detail));
            (void)run_command("/usr/bin/systemctl",
                (char *const[]){(char *)"systemctl", (char *)"restart",
                                 (char *)"--no-block",
                                 (char *)"blit.service", NULL}, restart_detail,
                sizeof(restart_detail));
            close(server_fd);
            unlink(UPDATE_SOCKET);
            return 0;
        }
    }
}
