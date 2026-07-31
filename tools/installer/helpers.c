#include "installer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int has_prefix(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int installer_valid_username(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0' || strcmp(name, "root") == 0) {
        return 0;
    }

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(islower(c) || isdigit(c) || c == '_' || c == '-')) {
            return 0;
        }
    }

    return 1;
}

int installer_supported_disk(const char *path)
{
    const char *name;
    size_t len;

    if (path == NULL || !has_prefix(path, "/dev/")) {
        return 0;
    }

    name = path + 5;
    len = strlen(name);

    if (len == 3 && has_prefix(name, "sd") && name[2] >= 'a' && name[2] <= 'z') {
        return 1;
    }
    if (len == 3 && has_prefix(name, "vd") && name[2] >= 'a' && name[2] <= 'z') {
        return 1;
    }
    if (len == 4 && has_prefix(name, "xvd") && name[3] >= 'a' && name[3] <= 'z') {
        return 1;
    }
    if (sscanf(name, "nvme%*un%*u") == 0 && has_prefix(name, "nvme")) {
        unsigned int ctrl = 0;
        unsigned int ns = 0;
        char extra = 0;
        return sscanf(name, "nvme%un%u%c", &ctrl, &ns, &extra) == 2;
    }
    if (has_prefix(name, "mmcblk")) {
        unsigned int disk = 0;
        char extra = 0;
        return sscanf(name, "mmcblk%u%c", &disk, &extra) == 1;
    }

    return 0;
}

int installer_partition_path(const char *disk, int part_no, char *out, size_t out_size)
{
    int written;

    if (disk == NULL || out == NULL || out_size == 0 || part_no <= 0) {
        return -1;
    }

    if (strstr(disk, "/dev/nvme") == disk || strstr(disk, "/dev/mmcblk") == disk) {
        written = snprintf(out, out_size, "%sp%d", disk, part_no);
    } else {
        written = snprintf(out, out_size, "%s%d", disk, part_no);
    }

    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }

    return 0;
}

int installer_build_root_arg(const char *root_dev, char *out, size_t out_size)
{
    int written;

    if (root_dev == NULL || out == NULL || out_size == 0 || strncmp(root_dev, "/dev/", 5) != 0) {
        return -1;
    }

    written = snprintf(out, out_size, "root=%s", root_dev);
    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    return 0;
}

int installer_build_fdisk_script(InstallerBootMode boot_mode,
                                const char *part_size,
                                char *out,
                                size_t out_size)
{
    int written;
    const char *size = part_size != NULL && part_size[0] != '\0' ? part_size : "";

    if (out == NULL || out_size == 0) {
        return -1;
    }

    if (boot_mode == INSTALLER_BOOT_UEFI) {
        written = snprintf(out, out_size,
                           "echo o; echo n; echo p; echo 1; echo 2048; echo +512M; "
                           "echo n; echo p; echo 2; echo 1050624; echo '%s'; "
                           "echo t; echo 1; echo ef; echo a; echo 1; echo w",
                           size);
    } else {
        written = snprintf(out, out_size,
                           "echo o; echo n; echo p; echo 1; echo 2048; echo '%s'; "
                           "echo a; echo 1; echo w",
                           size);
    }

    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    return 0;
}

int installer_should_redraw_progress_log(long now_ms, long last_draw_ms, long min_interval_ms)
{
    if (last_draw_ms <= 0 || now_ms < last_draw_ms) {
        return 1;
    }
    return now_ms - last_draw_ms >= min_interval_ms;
}

void installer_format_progress_bar(int percent, int width, char *out, size_t out_size)
{
    int i;
    int filled;
    size_t pos = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    if (width < 0) {
        width = 0;
    }

    filled = (percent * width + 50) / 100;

    if (pos + 1 < out_size) {
        out[pos++] = '[';
    }
    for (i = 0; i < width && pos + 1 < out_size; i++) {
        out[pos++] = i < filled ? '#' : ' ';
    }
    if (pos + 1 < out_size) {
        out[pos++] = ']';
    }
    if (pos < out_size) {
        snprintf(out + pos, out_size - pos, " %d%%", percent);
    } else {
        out[out_size - 1] = '\0';
    }
}

void installer_build_syspckg_install_argv(char *argv[6], const char *selector, int local_only)
{
    argv[0] = "syspckg";
    argv[1] = "install";
    argv[2] = (char *)selector;
    if (local_only) {
        argv[3] = "-local";
        argv[4] = "-y";
        argv[5] = NULL;
    } else {
        argv[3] = "-y";
        argv[4] = NULL;
        argv[5] = NULL;
    }
}

int installer_valid_uuid_value(const char *value)
{
    size_t i;
    int has_hex = 0;

    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    if (strstr(value, "/dev/") != NULL) {
        return 0;
    }

    for (i = 0; value[i] != '\0'; i++) {
        unsigned char c = (unsigned char)value[i];
        if (isspace(c)) {
            return 0;
        }
        if (isxdigit(c)) {
            has_hex = 1;
            continue;
        }
        if (c == '-') {
            continue;
        }
        return 0;
    }

    return has_hex;
}

int installer_format_failure_summary(const char *log_path, char *out, size_t out_size)
{
    enum { MAX_LINES = 5, LINE_LEN = 160 };
    char lines[MAX_LINES][LINE_LEN];
    int count = 0;
    char line[LINE_LEN];
    FILE *f;
    size_t pos;
    int i;

    if (out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    f = log_path != NULL ? fopen(log_path, "r") : NULL;
    if (f != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '\0' || strcmp(line, "AdavaLinux installer log") == 0) {
                continue;
            }
            if (count < MAX_LINES) {
                snprintf(lines[count++], sizeof(lines[0]), "%s", line);
            } else {
                memmove(lines[0], lines[1], sizeof(lines[0]) * (MAX_LINES - 1));
                snprintf(lines[MAX_LINES - 1], sizeof(lines[0]), "%s", line);
            }
        }
        fclose(f);
    }

    pos = (size_t)snprintf(out, out_size,
                           "The installer stopped because a command failed.\n\n"
                           "Last log lines:");
    if (pos >= out_size) {
        out[out_size - 1] = '\0';
        return 0;
    }

    if (count == 0) {
        snprintf(out + pos, out_size - pos,
                 "\nNo log output was captured.\n\nLog: /tmp/adavalinux-installer.log");
        return 0;
    }

    for (i = 0; i < count && pos + 1 < out_size; i++) {
        pos += (size_t)snprintf(out + pos, out_size - pos, "\n%s", lines[i]);
        if (pos >= out_size) {
            out[out_size - 1] = '\0';
            return 0;
        }
    }
    snprintf(out + pos, out_size - pos,
             "\n\nFull log: /tmp/adavalinux-installer.log");
    return 0;
}

int installer_build_shutdown_links_command(const char *root, char *out, size_t out_size)
{
    int written;

    if (root == NULL || root[0] == '\0' || out == NULL || out_size == 0) {
        return -1;
    }

    written = snprintf(out, out_size,
                       "mkdir -p %s/bin %s/sbin && "
                       "ln -sf busybox %s/bin/reboot && "
                       "ln -sf busybox %s/bin/poweroff && "
                       "ln -sf busybox %s/bin/halt && "
                       "ln -sf ../bin/busybox %s/sbin/reboot && "
                       "ln -sf ../bin/busybox %s/sbin/poweroff && "
                       "ln -sf ../bin/busybox %s/sbin/halt",
                       root, root,
                       root,
                       root,
                       root,
                       root,
                       root,
                       root);
    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    return 0;
}
