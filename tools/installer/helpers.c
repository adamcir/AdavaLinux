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

int installer_supported_install_media(const char *path)
{
    const char *name;
    size_t len;

    if (installer_supported_disk(path)) {
        return 1;
    }
    if (path == NULL || !has_prefix(path, "/dev/")) {
        return 0;
    }

    name = path + 5;
    len = strlen(name);
    if (len >= 3 && len <= 4 && has_prefix(name, "sr")) {
        unsigned int disk = 0;
        char extra = 0;
        return sscanf(name, "sr%u%c", &disk, &extra) == 1;
    }

    return 0;
}

int installer_partition_belongs_to_disk(const char *disk, const char *partition)
{
    const char *disk_name;
    const char *part_name;
    const char *suffix;

    if (!installer_supported_disk(disk) || partition == NULL || !has_prefix(partition, "/dev/")) {
        return 0;
    }

    disk_name = disk + 5;
    part_name = partition + 5;
    if (!has_prefix(part_name, disk_name)) {
        return 0;
    }
    suffix = part_name + strlen(disk_name);
    if (strncmp(disk_name, "nvme", 4) == 0 || strncmp(disk_name, "mmcblk", 6) == 0) {
        if (*suffix != 'p') {
            return 0;
        }
        suffix++;
    }
    if (*suffix == '\0') {
        return 0;
    }
    while (*suffix != '\0') {
        if (!isdigit((unsigned char)*suffix++)) {
            return 0;
        }
    }
    return 1;
}

int installer_target_disk_available(const char *disk, const char *install_media)
{
    if (!installer_supported_disk(disk)) {
        return 0;
    }
    if (install_media != NULL && install_media[0] != '\0' && strcmp(disk, install_media) == 0) {
        return 0;
    }
    return 1;
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
                                const char *disk,
                                char *out,
                                size_t out_size)
{
    int written;
    const char *size = part_size != NULL && part_size[0] != '\0' ? part_size : NULL;

    if (out == NULL || out_size == 0) {
        return -1;
    }

    if (size == NULL && (disk == NULL || disk[0] == '\0')) {
        return -1;
    }

    if (size == NULL && boot_mode == INSTALLER_BOOT_UEFI) {
        written = snprintf(out, out_size,
                           "{ echo o; echo n; echo p; echo 1; echo 2048; echo +512M; "
                           "echo n; echo p; echo 2; echo 1050624; echo \"$(( $(blockdev --getsz '%s') - 1 ))\"; "
                           "echo t; echo 1; echo ef; echo a; echo 1; echo w; }",
                           disk);
    } else if (size == NULL) {
        written = snprintf(out, out_size,
                           "{ echo o; echo n; echo p; echo 1; echo 2048; echo \"$(( $(blockdev --getsz '%s') - 1 ))\"; "
                           "echo a; echo 1; echo w; }",
                           disk);
    } else if (boot_mode == INSTALLER_BOOT_UEFI) {
        written = snprintf(out, out_size,
                           "{ echo o; echo n; echo p; echo 1; echo 2048; echo +512M; "
                           "echo n; echo p; echo 2; echo 1050624; echo '%s'; "
                           "echo t; echo 1; echo ef; echo a; echo 1; echo w; }",
                           size);
    } else {
        written = snprintf(out, out_size,
                           "{ echo o; echo n; echo p; echo 1; echo 2048; echo '%s'; "
                           "echo a; echo 1; echo w; }",
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

int installer_next_wrapped_log_segment(const char *line,
                                       size_t start,
                                       size_t width,
                                       char *out,
                                       size_t out_size,
                                       size_t *next)
{
    size_t len;
    size_t take;

    if (line == NULL || out == NULL || out_size == 0 || next == NULL || width == 0) {
        return 0;
    }
    len = strlen(line);
    if (start > len) {
        return 0;
    }
    take = len - start;
    if (take > width) {
        take = width;
    }
    if (take >= out_size) {
        take = out_size - 1;
    }
    memcpy(out, line + start, take);
    out[take] = '\0';
    *next = start + take;
    return 1;
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

void installer_build_syspckg_root_install_argv(char *argv[9],
                                               const char *selector,
                                               const char *root,
                                               int local_only)
{
    argv[0] = "syspckg";
    argv[1] = "install";
    argv[2] = (char *)selector;
    if (local_only) {
        argv[3] = "-local";
        argv[4] = "--root";
        argv[5] = (char *)root;
        argv[6] = "--allow-root";
        argv[7] = "-y";
        argv[8] = NULL;
    } else {
        argv[3] = "--root";
        argv[4] = (char *)root;
        argv[5] = "--allow-root";
        argv[6] = "-y";
        argv[7] = NULL;
        argv[8] = NULL;
    }
}

int installer_build_grub_mkconfig_command(const char *root, char *out, size_t out_size)
{
    int written;

    if (root == NULL || root[0] == '\0' || out == NULL || out_size == 0) {
        return -1;
    }
    written = snprintf(out, out_size, "chroot %s /usr/sbin/grub-mkconfig -o /boot/grub/grub.cfg", root);
    return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

int installer_build_default_grub_config(const char *root_arg,
                                        InstallerAcpiMode acpi_mode,
                                        char *out,
                                        size_t out_size)
{
    const char *extra;
    int written;

    if (root_arg == NULL || root_arg[0] == '\0' || out == NULL || out_size == 0) {
        return -1;
    }
    extra = acpi_mode == INSTALLER_ACPI_OFF ? " acpi=off noapic nolapic irqpoll pci=nomsi" : "";
    written = snprintf(out, out_size,
                       "GRUB_TIMEOUT=10\n"
                       "GRUB_DEFAULT=0\n"
                       "GRUB_CMDLINE_LINUX=\"%s rootfstype=ext4 rootwait rootdelay=5 rw console=ttyS0 console=tty1 libata.force=noncq%s\"\n"
                       "GRUB_CMDLINE_LINUX_DEFAULT=\"quiet\"\n",
                       root_arg, extra);
    return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

int installer_build_copy_grub_mkconfig_command(const char *root, char *out, size_t out_size)
{
    int written;

    if (root == NULL || root[0] == '\0' || out == NULL || out_size == 0) {
        return -1;
    }
    written = snprintf(out, out_size,
                       "mkdir -p %s/usr/sbin && cp -f /usr/sbin/grub-mkconfig %s/usr/sbin/grub-mkconfig && chmod 755 %s/usr/sbin/grub-mkconfig",
                       root, root, root);
    return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

int installer_build_prepare_grub_chroot_command(const char *root, char *out, size_t out_size)
{
    int written;

    if (root == NULL || root[0] == '\0' || out == NULL || out_size == 0) {
        return -1;
    }
    written = snprintf(out, out_size,
                       "mkdir -p %s/dev %s/proc %s/sys && mount --bind /dev %s/dev && mount -t proc proc %s/proc && mount -t sysfs sysfs %s/sys",
                       root, root, root, root, root, root);
    return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

int installer_build_disable_standard_grub_generators_command(const char *root, char *out, size_t out_size)
{
    int written;

    if (root == NULL || root[0] == '\0' || out == NULL || out_size == 0) {
        return -1;
    }
    written = snprintf(out, out_size,
                       "chmod a-x %s/usr/etc/grub.d/10_linux %s/usr/etc/grub.d/20_linux_xen %s/usr/etc/grub.d/25_bli %s/usr/etc/grub.d/30_os-prober %s/usr/etc/grub.d/30_uefi-firmware %s/usr/etc/grub.d/40_custom %s/usr/etc/grub.d/41_custom 2>/dev/null || true; "
                       "chmod a-x %s/etc/grub.d/10_linux %s/etc/grub.d/20_linux_xen %s/etc/grub.d/25_bli %s/etc/grub.d/30_os-prober %s/etc/grub.d/30_uefi-firmware %s/etc/grub.d/40_custom %s/etc/grub.d/41_custom 2>/dev/null || true; "
                       "chmod a+x %s/usr/etc/grub.d/00_header %s/usr/etc/grub.d/10_adavalinux %s/etc/grub.d/00_header %s/etc/grub.d/10_adavalinux 2>/dev/null || true",
                       root, root, root, root, root, root, root,
                       root, root, root, root, root, root, root,
                       root, root, root, root);
    return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

int installer_build_uefi_removable_fallback_command(const char *root, char *out, size_t out_size)
{
    int written;

    if (root == NULL || root[0] == '\0' || out == NULL || out_size == 0) {
        return -1;
    }
    written = snprintf(out, out_size,
                       "mkdir -p %s/boot/efi/EFI/BOOT && "
                       "if [ ! -f %s/boot/efi/EFI/BOOT/BOOTX64.EFI ]; then "
                       "if [ -f %s/boot/efi/EFI/AdavaLinux/grubx64.efi ]; then "
                       "cp -f %s/boot/efi/EFI/AdavaLinux/grubx64.efi %s/boot/efi/EFI/BOOT/BOOTX64.EFI; "
                       "else efi_file=\"$(find %s/boot/efi/EFI -type f -name '*.efi' | head -n 1)\"; "
                       "[ -n \"$efi_file\" ] && cp -f \"$efi_file\" %s/boot/efi/EFI/BOOT/BOOTX64.EFI; fi; fi; "
                       "test -f %s/boot/efi/EFI/BOOT/BOOTX64.EFI",
                       root, root, root, root, root, root, root, root);
    return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

int installer_confirmation_phrase_matches(const char *disk, const char *typed)
{
    char expected[96];
    int written;

    if (disk == NULL || disk[0] == '\0' || typed == NULL) {
        return 0;
    }
    written = snprintf(expected, sizeof(expected), "ERASE %s", disk);
    if (written < 0 || (size_t)written >= sizeof(expected)) {
        return 0;
    }
    return strcmp(typed, expected) == 0;
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

int installer_build_syspckg_state_cleanup_command(const char *root, char *out, size_t out_size)
{
    int written;

    if (root == NULL || root[0] == '\0' || out == NULL || out_size == 0) {
        return -1;
    }

    written = snprintf(out,
                       out_size,
                       "rm -f %s/install.sh %s/root/install.sh %s/usr/bin/installer %s/bin/installer && "
                       "rm -rf %s/usr/share/syspckg/packages %s/var/cache/syspckg %s/var/lib/syspckg/packages %s/var/lib/syspckg/installed",
                       root,
                       root,
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
