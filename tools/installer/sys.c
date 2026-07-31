#define _POSIX_C_SOURCE 200809L

#include "sys.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void log_line(InstallerLogFn log_fn, void *ctx, const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    if (log_fn == NULL) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_fn(ctx, buf);
}

static void log_command(InstallerLogFn log_fn, void *ctx, char *const argv[])
{
    char buf[512];
    size_t pos = 0;
    int i;

    if (log_fn == NULL) {
        return;
    }

    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "$");
    for (i = 0; argv[i] != NULL && pos + 1 < sizeof(buf); i++) {
        if (strchr(argv[i], ' ') != NULL || strchr(argv[i], '\t') != NULL) {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, " '%s'", argv[i]);
        } else {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, " %s", argv[i]);
        }
    }
    buf[sizeof(buf) - 1] = '\0';
    log_fn(ctx, buf);
}

static void strip_ansi(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;
    int esc = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    while (*src != '\0' && out + 1 < dst_size) {
        unsigned char c = (unsigned char)*src++;
        if (esc == 0 && c == 033) {
            esc = 1;
            continue;
        }
        if (esc == 1) {
            if (c == '[') {
                esc = 2;
            } else {
                esc = 0;
            }
            continue;
        }
        if (esc == 2) {
            if ((c >= '@' && c <= '~')) {
                esc = 0;
            }
            continue;
        }
        dst[out++] = (char)c;
    }
    dst[out] = '\0';
}

static void emit_process_line(InstallerLogFn log_fn, void *ctx, const char *line)
{
    char clean[512];

    if (line == NULL || line[0] == '\0') {
        return;
    }
    strip_ansi(line, clean, sizeof(clean));
    if (clean[0] != '\0') {
        log_line(log_fn, ctx, "%s", clean);
    }
}

int installer_read_first_line(const char *path, char *out, size_t out_size)
{
    FILE *f;

    if (out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    if (fgets(out, (int)out_size, f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);
    out[strcspn(out, "\r\n")] = '\0';
    return 0;
}

int installer_write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }
    if (fputs(content, f) == EOF) {
        fclose(f);
        return -1;
    }
    return fclose(f);
}

int installer_is_block_device(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISBLK(st.st_mode);
}

int installer_command_exists(const char *name)
{
    char *path;
    char *copy;
    char *tok;
    char full[512];

    if (name == NULL || strchr(name, '/') != NULL) {
        return name != NULL && access(name, X_OK) == 0;
    }

    path = getenv("PATH");
    if (path == NULL) {
        path = "/bin:/sbin:/usr/bin:/usr/sbin";
    }

    copy = strdup(path);
    if (copy == NULL) {
        return 0;
    }

    for (tok = strtok(copy, ":"); tok != NULL; tok = strtok(NULL, ":")) {
        snprintf(full, sizeof(full), "%s/%s", tok[0] != '\0' ? tok : ".", name);
        if (access(full, X_OK) == 0) {
            free(copy);
            return 1;
        }
    }
    free(copy);
    return 0;
}

int installer_scan_disks(InstallerDisk *disks, size_t max_disks, size_t *count)
{
    DIR *dir;
    struct dirent *ent;
    size_t n = 0;

    if (count == NULL) {
        return -1;
    }
    *count = 0;

    dir = opendir("/sys/block");
    if (dir == NULL) {
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        char dev[64];
        char sys_path[256];
        char line[128];
        unsigned long long sectors = 0;

        if (ent->d_name[0] == '.') {
            continue;
        }
        if (strlen(ent->d_name) > 48) {
            continue;
        }
        snprintf(dev, sizeof(dev), "/dev/%.48s", ent->d_name);
        if (!installer_supported_disk(dev) || !installer_is_block_device(dev)) {
            continue;
        }
        if (n >= max_disks) {
            continue;
        }

        memset(&disks[n], 0, sizeof(disks[n]));
        snprintf(disks[n].path, sizeof(disks[n].path), "%s", dev);
        snprintf(sys_path, sizeof(sys_path), "/sys/block/%.48s/size", ent->d_name);
        if (installer_read_first_line(sys_path, line, sizeof(line)) == 0) {
            sectors = strtoull(line, NULL, 10);
            disks[n].mib = sectors / 2048ULL;
        }
        snprintf(sys_path, sizeof(sys_path), "/sys/block/%.48s/device/model", ent->d_name);
        if (installer_read_first_line(sys_path, disks[n].model, sizeof(disks[n].model)) != 0 ||
            disks[n].model[0] == '\0') {
            snprintf(disks[n].model, sizeof(disks[n].model), "Block device");
        }
        n++;
    }

    closedir(dir);
    *count = n;
    return 0;
}

static int wait_status_to_result(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

int installer_run_command(char *const argv[], InstallerLogFn log_fn, void *log_ctx)
{
    int pipefd[2];
    pid_t pid;
    char buf[256];
    char line[512];
    size_t line_len = 0;
    ssize_t got;
    int status;

    if (argv == NULL || argv[0] == NULL) {
        return 1;
    }

    log_command(log_fn, log_ctx, argv);
    if (pipe(pipefd) != 0) {
        log_line(log_fn, log_ctx, "pipe failed: %s", strerror(errno));
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        log_line(log_fn, log_ctx, "fork failed: %s", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    while ((got = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
        ssize_t i;
        for (i = 0; i < got; i++) {
            char c = buf[i];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line[line_len] = '\0';
                emit_process_line(log_fn, log_ctx, line);
                line_len = 0;
                continue;
            }
            if (line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
            } else {
                line[line_len] = '\0';
                emit_process_line(log_fn, log_ctx, line);
                line_len = 0;
            }
        }
    }
    if (line_len > 0) {
        line[line_len] = '\0';
        emit_process_line(log_fn, log_ctx, line);
    }
    close(pipefd[0]);

    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    {
        int rc = wait_status_to_result(status);
        log_line(log_fn, log_ctx, "command exit: %d", rc);
        return rc;
    }
}

int installer_run_shell(const char *command, InstallerLogFn log_fn, void *log_ctx)
{
    char *const argv[] = { "sh", "-c", (char *)command, NULL };
    return installer_run_command(argv, log_fn, log_ctx);
}

void installer_safe_umount(const char *mountpoint, InstallerLogFn log_fn, void *log_ctx)
{
    char *const argv[] = { "umount", (char *)mountpoint, NULL };
    FILE *f;
    char src[128];
    char mnt[128];
    int mounted = 0;

    f = fopen("/proc/mounts", "r");
    if (f != NULL) {
        while (fscanf(f, "%127s %127s %*s %*s %*d %*d\n", src, mnt) == 2) {
            if (strcmp(mnt, mountpoint) == 0) {
                mounted = 1;
                break;
            }
        }
        fclose(f);
    }
    if (mounted) {
        (void)installer_run_command(argv, log_fn, log_ctx);
    }
}
