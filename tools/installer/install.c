#define _POSIX_C_SOURCE 200809L

#include "install.h"

#include "sys.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOT_MNT "/mnt/root"
#define INSTALL_MNT "/mnt/install"
#define FS_TYPE "ext4"
#define INSTALLER_LOG_PATH "/tmp/adavalinux-installer.log"

static void emit_log(InstallerLogFn log_fn, void *ctx, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    if (log_fn == NULL) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_fn(ctx, buf);
}

static int step(InstallerProgressFn progress_fn, void *ctx, int percent, const char *label)
{
    if (progress_fn != NULL) {
        progress_fn(ctx, percent, label);
    }
    return 0;
}

static int run_checked(char *const argv[], InstallerLogFn log_fn, void *ctx)
{
    int rc = installer_run_command(argv, log_fn, ctx);
    if (rc != 0) {
        emit_log(log_fn, ctx, "command failed with exit code %d", rc);
    }
    return rc;
}

static int shell_checked(const char *cmd, InstallerLogFn log_fn, void *ctx)
{
    int rc = installer_run_shell(cmd, log_fn, ctx);
    if (rc != 0) {
        emit_log(log_fn, ctx, "command failed with exit code %d", rc);
    }
    return rc;
}

static int capture_command(char *const argv[], char *out, size_t out_size)
{
    int pipefd[2];
    pid_t pid;
    ssize_t got;
    size_t pos = 0;
    int status;

    if (out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    while ((got = read(pipefd[0], out + pos, out_size - pos - 1)) > 0) {
        pos += (size_t)got;
        if (pos + 1 >= out_size) {
            break;
        }
    }
    close(pipefd[0]);
    out[pos] = '\0';
    out[strcspn(out, "\r\n")] = '\0';
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int hash_password(const char *password, char *out, size_t out_size)
{
    if (installer_command_exists("mkpasswd")) {
        char *const argv[] = { "mkpasswd", "-m", "sha-512", (char *)password, NULL };
        if (capture_command(argv, out, out_size) == 0 && out[0] != '\0') {
            return 0;
        }
    }
    if (installer_command_exists("openssl")) {
        char *const argv[] = { "openssl", "passwd", "-6", (char *)password, NULL };
        if (capture_command(argv, out, out_size) == 0 && out[0] != '\0') {
            return 0;
        }
    }
    return -1;
}

static int mount_dev_at(const char *mountpoint, char *out, size_t out_size)
{
    FILE *f = fopen("/proc/mounts", "r");
    char src[128];
    char mnt[128];

    if (out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (f == NULL) {
        return -1;
    }
    while (fscanf(f, "%127s %127s %*s %*s %*d %*d\n", src, mnt) == 2) {
        if (strcmp(mnt, mountpoint) == 0) {
            snprintf(out, out_size, "%s", src);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

static int parent_disk(const char *dev, char *out, size_t out_size)
{
    size_t len;

    if (dev == NULL || out == NULL || out_size == 0 || strncmp(dev, "/dev/sr", 7) == 0) {
        return -1;
    }
    snprintf(out, out_size, "%s", dev);
    len = strlen(out);
    if (strstr(out, "/dev/nvme") == out || strstr(out, "/dev/mmcblk") == out) {
        char *p = strrchr(out, 'p');
        if (p != NULL && p[1] >= '0' && p[1] <= '9') {
            *p = '\0';
        }
        return 0;
    }
    while (len > 0 && out[len - 1] >= '0' && out[len - 1] <= '9') {
        out[--len] = '\0';
    }
    return 0;
}

static int find_first_glob(const char *pattern, char *out, size_t out_size)
{
    glob_t g;
    int rc;

    rc = glob(pattern, 0, NULL, &g);
    if (rc != 0 || g.gl_pathc == 0) {
        globfree(&g);
        return -1;
    }
    snprintf(out, out_size, "%s", g.gl_pathv[0]);
    globfree(&g);
    return 0;
}

static int resolve_grub_platform_dir(const char *platform, char *out, size_t out_size,
                                     InstallerLogFn log_fn, void *ctx)
{
    const char *bases[] = {
        INSTALL_MNT "/grub-install-modules",
        "/boot/grub",
        "/usr/lib/grub",
        "/usr/lib64/grub",
        "/lib/grub",
        "/lib64/grub",
        ROOT_MNT "/boot/grub",
        ROOT_MNT "/usr/lib/grub",
        ROOT_MNT "/usr/lib64/grub",
        ROOT_MNT "/lib/grub",
        ROOT_MNT "/lib64/grub",
        INSTALL_MNT "/usr/lib/grub",
        INSTALL_MNT "/usr/lib64/grub",
        INSTALL_MNT "/lib/grub",
        INSTALL_MNT "/lib64/grub",
        INSTALL_MNT "/packages",
        INSTALL_MNT "/syspckg"
    };
    size_t i;

    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        char cand[PATH_MAX];
        char modinfo[PATH_MAX];
        char kernel[PATH_MAX];

        if (snprintf(cand, sizeof(cand), "%s/%s", bases[i], platform) >= (int)sizeof(cand)) {
            continue;
        }
        if (snprintf(modinfo, sizeof(modinfo), "%s/modinfo.sh", cand) >= (int)sizeof(modinfo)) {
            continue;
        }
        if (snprintf(kernel, sizeof(kernel), "%s/kernel.img", cand) >= (int)sizeof(kernel)) {
            continue;
        }
        if (access(modinfo, F_OK) == 0 && access(kernel, F_OK) == 0) {
            snprintf(out, out_size, "%s", cand);
            emit_log(log_fn, ctx, "Using GRUB %s directory: %s", platform, out);
            return 0;
        }
    }

    emit_log(log_fn, ctx, "GRUB %s install modules not found; available GRUB files:", platform);
    (void)installer_run_shell("find /boot /usr /usr/lib64 /lib /lib64 " ROOT_MNT " " INSTALL_MNT " \\( -name modinfo.sh -o -name kernel.img -o -name normal.mod \\) 2>/dev/null",
                              log_fn,
                              ctx);
    return -1;
}

static int install_grub_pkg(const char *pkg, const char *target_root, InstallerLogFn log_fn, void *ctx)
{
    const char *bases[] = { INSTALL_MNT "/packages", INSTALL_MNT "/syspckg", INSTALL_MNT };
    char pattern[256];
    char local_pkg[256];
    int target_install = target_root != NULL && target_root[0] != '\0';
    size_t i;

    emit_log(log_fn, ctx, "Looking for local %s package on installer media", pkg);
    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        snprintf(pattern, sizeof(pattern), "%s/%s-*.syspckg", bases[i], pkg);
        emit_log(log_fn, ctx, "Checking %s", pattern);
        if (find_first_glob(pattern, local_pkg, sizeof(local_pkg)) == 0) {
            char *argv[9];
            emit_log(log_fn, ctx, "Using local package: %s", local_pkg);
            if (target_install) {
                installer_build_syspckg_root_install_argv(argv, local_pkg, target_root, 1);
            } else {
                installer_build_syspckg_install_argv(argv, local_pkg, 1);
            }
            return run_checked(argv, log_fn, ctx);
        }
        snprintf(pattern, sizeof(pattern), "%s/%s.syspckg", bases[i], pkg);
        emit_log(log_fn, ctx, "Checking %s", pattern);
        if (find_first_glob(pattern, local_pkg, sizeof(local_pkg)) == 0) {
            char *argv[9];
            emit_log(log_fn, ctx, "Using local package: %s", local_pkg);
            if (target_install) {
                installer_build_syspckg_root_install_argv(argv, local_pkg, target_root, 1);
            } else {
                installer_build_syspckg_install_argv(argv, local_pkg, 1);
            }
            return run_checked(argv, log_fn, ctx);
        }
    }

    {
        char *argv[9];
        emit_log(log_fn, ctx, "No local package found; using syspckg repository install");
        if (target_install) {
            installer_build_syspckg_root_install_argv(argv, pkg, target_root, 0);
        } else {
            installer_build_syspckg_install_argv(argv, pkg, 0);
        }
        return run_checked(argv, log_fn, ctx);
    }
}

static int wait_for_partitions(const char *root_part, const char *efi_part,
                               InstallerLogFn log_fn, void *ctx)
{
    int attempt;

    for (attempt = 0; attempt < 10; attempt++) {
        int root_ok = installer_is_block_device(root_part);
        int efi_ok = efi_part == NULL || efi_part[0] == '\0' || installer_is_block_device(efi_part);

        if (root_ok && efi_ok) {
            return 0;
        }
        emit_log(log_fn, ctx, "Waiting for partition devices: root=%s%s efi=%s%s",
                 root_part,
                 root_ok ? " ready" : " missing",
                 efi_part != NULL && efi_part[0] != '\0' ? efi_part : "-",
                 efi_ok ? " ready" : " missing");
        {
            char *const mdev[] = { "mdev", "-s", NULL };
            (void)installer_run_command(mdev, log_fn, ctx);
        }
        sleep(1);
    }

    emit_log(log_fn, ctx, "Partition devices did not appear after fdisk");
    return -1;
}

static int detect_media(const InstallerConfig *cfg, char *media_dev, size_t media_dev_size,
                        InstallerLogFn log_fn, void *ctx)
{
    const char *env_media = getenv("INSTALL_MEDIA");
    const char *cands[] = { "/dev/sr0", "/dev/sda1", "/dev/sda" };
    size_t i;

    media_dev[0] = '\0';
    if (mount_dev_at(INSTALL_MNT, media_dev, media_dev_size) == 0 &&
        access(INSTALL_MNT "/boot/initramfs-installer.gz", F_OK) == 0) {
        return 0;
    }
    installer_safe_umount(INSTALL_MNT, log_fn, ctx);

    if (env_media != NULL && env_media[0] != '\0' && strcmp(env_media, "auto") != 0) {
        char *const argv[] = { "mount", "-o", "ro", (char *)env_media, INSTALL_MNT, NULL };
        if (!installer_is_block_device(env_media)) {
            emit_log(log_fn, ctx, "Installer media is not a block device: %s", env_media);
            return -1;
        }
        if (run_checked(argv, log_fn, ctx) == 0) {
            snprintf(media_dev, media_dev_size, "%s", env_media);
            return 0;
        }
        return -1;
    }

    (void)cfg;
    for (i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
        char *const argv[] = { "mount", "-o", "ro", (char *)cands[i], INSTALL_MNT, NULL };
        if (!installer_is_block_device(cands[i])) {
            continue;
        }
        if (installer_run_command(argv, log_fn, ctx) == 0) {
            if (access(INSTALL_MNT "/boot/initramfs-installer.gz", F_OK) == 0) {
                snprintf(media_dev, media_dev_size, "%s", cands[i]);
                return 0;
            }
            installer_safe_umount(INSTALL_MNT, log_fn, ctx);
        }
    }
    emit_log(log_fn, ctx, "Installer media not found. Set INSTALL_MEDIA=/dev/...");
    return -1;
}

static int write_user_files(const InstallerConfig *cfg, InstallerLogFn log_fn, void *ctx)
{
    char user_hash[256];
    char root_hash[256];
    char path[256];
    char content[1024];
    char *const chmod_passwd[] = { "chmod", "644", ROOT_MNT "/etc/passwd", ROOT_MNT "/etc/group", NULL };
    char *const chmod_shadow[] = { "chmod", "600", ROOT_MNT "/etc/shadow", NULL };

    if (hash_password(cfg->password, user_hash, sizeof(user_hash)) != 0 ||
        hash_password(cfg->root_password, root_hash, sizeof(root_hash)) != 0) {
        emit_log(log_fn, ctx, "Cannot hash passwords; need mkpasswd or openssl");
        return -1;
    }

    snprintf(path, sizeof(path), ROOT_MNT "/home/%s", cfg->username);
    {
        char *const mkdirs[] = { "mkdir", "-p", ROOT_MNT "/root", path, NULL };
        if (run_checked(mkdirs, log_fn, ctx) != 0) {
            return -1;
        }
    }
    {
        char *const chown_home[] = { "chown", "1000:1000", path, NULL };
        char *const chmod_home[] = { "chmod", "700", path, NULL };
        char *const chmod_root[] = { "chmod", "700", ROOT_MNT "/root", NULL };
        if (run_checked(chown_home, log_fn, ctx) != 0 ||
            run_checked(chmod_home, log_fn, ctx) != 0 ||
            run_checked(chmod_root, log_fn, ctx) != 0) {
            return -1;
        }
    }

    snprintf(content, sizeof(content),
             "root:x:0:0:root:/root:/bin/sh\n"
             "%s:x:1000:1000:%s:/home/%s:/bin/sh\n",
             cfg->username, cfg->username, cfg->username);
    if (installer_write_file(ROOT_MNT "/etc/passwd", content) != 0) {
        return -1;
    }
    snprintf(content, sizeof(content),
             "root:%s:0:0:99999:7:::\n"
             "%s:%s:0:0:99999:7:::\n",
             root_hash, cfg->username, user_hash);
    if (installer_write_file(ROOT_MNT "/etc/shadow", content) != 0) {
        return -1;
    }
    snprintf(content, sizeof(content), "root:x:0:\n%s:x:1000:\n", cfg->username);
    if (installer_write_file(ROOT_MNT "/etc/group", content) != 0) {
        return -1;
    }
    snprintf(content, sizeof(content), "%s\n", cfg->hostname);
    if (installer_write_file(ROOT_MNT "/etc/hostname", content) != 0) {
        return -1;
    }
    snprintf(content, sizeof(content), "127.0.0.1 localhost\n127.0.1.1 %s\n", cfg->hostname);
    if (installer_write_file(ROOT_MNT "/etc/hosts", content) != 0) {
        return -1;
    }
    (void)installer_run_command(chmod_passwd, log_fn, ctx);
    (void)installer_run_command(chmod_shadow, log_fn, ctx);
    return 0;
}

static int find_kernel(char *kernel_name, size_t kernel_name_size)
{
    const char *env_k = getenv("K_VERSION");
    char path[256];
    char found[256];
    char *base;

    if (env_k != NULL && env_k[0] != '\0') {
        snprintf(path, sizeof(path), INSTALL_MNT "/boot/vmlinuz-%s", env_k);
        if (access(path, F_OK) == 0) {
            snprintf(kernel_name, kernel_name_size, "vmlinuz-%s", env_k);
            return 0;
        }
        snprintf(path, sizeof(path), INSTALL_MNT "/boot/vmlinuz-%s.gz", env_k);
        if (access(path, F_OK) == 0) {
            snprintf(kernel_name, kernel_name_size, "vmlinuz-%s.gz", env_k);
            return 0;
        }
    }

    if (find_first_glob(INSTALL_MNT "/boot/vmlinuz-*", found, sizeof(found)) != 0) {
        return -1;
    }
    base = strrchr(found, '/');
    snprintf(kernel_name, kernel_name_size, "%.120s", base != NULL ? base + 1 : found);
    return 0;
}

int installer_run_install(const InstallerConfig *cfg,
                          InstallerLogFn log_fn,
                          InstallerProgressFn progress_fn,
                          void *ctx)
{
    char media_dev[64];
    char media_parent[64];
    char root_src[128];
    char root_parent[128];
    char part[64];
    char efi_part[64] = "";
    char root_arg[192];
    char kernel_name[128];
    char cmd[1024];
    int boot_uefi;

    if (cfg == NULL || cfg->disk[0] == '\0') {
        return 1;
    }
    boot_uefi = cfg->boot_mode == INSTALLER_BOOT_UEFI;
    if (installer_partition_path(cfg->disk, boot_uefi ? 2 : 1, part, sizeof(part)) != 0) {
        return 1;
    }
    if (boot_uefi && installer_partition_path(cfg->disk, 1, efi_part, sizeof(efi_part)) != 0) {
        return 1;
    }

    step(progress_fn, ctx, 2, "Preparing devices");
    {
        char *const mount_devtmpfs[] = { "mount", "-t", "devtmpfs", "devtmpfs", "/dev", NULL };
        char *const mdev[] = { "mdev", "-s", NULL };
        (void)installer_run_command(mount_devtmpfs, log_fn, ctx);
        (void)installer_run_command(mdev, log_fn, ctx);
    }
    installer_safe_umount(ROOT_MNT "/boot/efi", log_fn, ctx);
    installer_safe_umount(ROOT_MNT "/sys", log_fn, ctx);
    installer_safe_umount(ROOT_MNT "/proc", log_fn, ctx);
    installer_safe_umount(ROOT_MNT "/dev", log_fn, ctx);
    installer_safe_umount(ROOT_MNT, log_fn, ctx);
    installer_safe_umount(INSTALL_MNT, log_fn, ctx);

    if (!installer_is_block_device(cfg->disk) || !installer_supported_disk(cfg->disk)) {
        emit_log(log_fn, ctx, "Target disk is not supported: %s", cfg->disk);
        return 1;
    }

    step(progress_fn, ctx, 10, "Mounting installer media");
    {
        char *const mkdirs[] = { "mkdir", "-p", INSTALL_MNT, ROOT_MNT, NULL };
        if (run_checked(mkdirs, log_fn, ctx) != 0 || detect_media(cfg, media_dev, sizeof(media_dev), log_fn, ctx) != 0) {
            return 1;
        }
    }
    if (strcmp(media_dev, cfg->disk) == 0) {
        emit_log(log_fn, ctx, "Refusing to erase installer media: %s", cfg->disk);
        return 1;
    }
    if (parent_disk(media_dev, media_parent, sizeof(media_parent)) == 0 && strcmp(media_parent, cfg->disk) == 0) {
        emit_log(log_fn, ctx, "Refusing to erase the physical disk that contains installer media");
        return 1;
    }
    if (mount_dev_at("/", root_src, sizeof(root_src)) == 0 &&
        strncmp(root_src, "/dev/", 5) == 0 &&
        parent_disk(root_src, root_parent, sizeof(root_parent)) == 0 &&
        strcmp(root_parent, cfg->disk) == 0) {
        emit_log(log_fn, ctx, "Refusing to erase current root disk: %s", cfg->disk);
        return 1;
    }

    if (cfg->action == INSTALLER_ACTION_UPDATE) {
        step(progress_fn, ctx, 24, "Mounting target");
        if (wait_for_partitions(part, NULL, log_fn, ctx) != 0) {
            return 1;
        }
        {
            char *const mount_root[] = { "mount", "-t", FS_TYPE, part, ROOT_MNT, NULL };
            if (run_checked(mount_root, log_fn, ctx) != 0) {
                return 1;
            }
        }
        step(progress_fn, ctx, 50, "Updating installed system");
        {
            char *const update_all[] = {
                "syspckg",
                "update",
                "--all",
                "--root",
                ROOT_MNT,
                "--allow-root",
                "-y",
                NULL
            };
            if (run_checked(update_all, log_fn, ctx) != 0) {
                installer_safe_umount(ROOT_MNT, log_fn, ctx);
                return 1;
            }
        }
        step(progress_fn, ctx, 98, "Syncing and unmounting");
        {
            char *const sync_cmd[] = { "sync", NULL };
            (void)installer_run_command(sync_cmd, log_fn, ctx);
        }
        installer_safe_umount(ROOT_MNT, log_fn, ctx);
        installer_safe_umount(INSTALL_MNT, log_fn, ctx);
        step(progress_fn, ctx, 100, "Done");
        emit_log(log_fn, ctx, "Done. Updated installed system on %s.", part);
        return 0;
    }

    if (cfg->action == INSTALLER_ACTION_INSTALL) {
        step(progress_fn, ctx, 18, "Installing GRUB package");
        if (install_grub_pkg(boot_uefi ? "grub-efi" : "grub-bios", NULL, log_fn, ctx) != 0) {
            return 1;
        }
    }

    step(progress_fn, ctx, 23, "Installing e2fsprogs package");
    if (install_grub_pkg("e2fsprogs", NULL, log_fn, ctx) != 0) {
        return 1;
    }

    step(progress_fn, ctx, 28, "Partitioning disk");
    if (installer_build_fdisk_script(cfg->boot_mode, cfg->part_size, cmd, sizeof(cmd)) != 0 ||
        snprintf(cmd + strlen(cmd), sizeof(cmd) - strlen(cmd), " | fdisk '%s'", cfg->disk) < 0) {
        emit_log(log_fn, ctx, "Failed to build fdisk command");
        return 1;
    }
    if (shell_checked(cmd, log_fn, ctx) != 0) {
        return 1;
    }
    {
        char *const sync_cmd[] = { "sync", NULL };
        char *const reread[] = { "blockdev", "--rereadpt", (char *)cfg->disk, NULL };
        char *const partprobe[] = { "partprobe", (char *)cfg->disk, NULL };
        char *const mdev[] = { "mdev", "-s", NULL };
        (void)installer_run_command(sync_cmd, log_fn, ctx);
        (void)installer_run_command(reread, log_fn, ctx);
        (void)installer_run_command(partprobe, log_fn, ctx);
        (void)installer_run_command(mdev, log_fn, ctx);
    }
    if (wait_for_partitions(part, efi_part, log_fn, ctx) != 0) {
        return 1;
    }
    {
        char *const fdisk_list[] = { "fdisk", "-l", (char *)cfg->disk, NULL };
        (void)installer_run_command(fdisk_list, log_fn, ctx);
    }

    step(progress_fn, ctx, 38, "Formatting partitions");
    if (boot_uefi) {
        char *const mkfs_vfat[] = { "mkfs.vfat", "-F", "32", efi_part, NULL };
        if (run_checked(mkfs_vfat, log_fn, ctx) != 0) {
            return 1;
        }
    }
    {
        char *const mkfs_ext4[] = { "mkfs.ext4", "-F", "-E", "nodiscard", part, NULL };
        char *const mkfs_ext4_basic[] = { "mkfs.ext4", "-F", part, NULL };
        char *const fsck_ext4[] = { "fsck.ext4", "-f", "-y", part, NULL };
        if (installer_run_command(mkfs_ext4, log_fn, ctx) != 0 &&
            run_checked(mkfs_ext4_basic, log_fn, ctx) != 0) {
            return 1;
        }
        if (installer_command_exists("fsck.ext4")) {
            (void)installer_run_command(fsck_ext4, log_fn, ctx);
        }
    }

    if (cfg->action == INSTALLER_ACTION_FORMAT_ONLY) {
        step(progress_fn, ctx, 98, "Syncing");
        {
            char *const sync_cmd[] = { "sync", NULL };
            (void)installer_run_command(sync_cmd, log_fn, ctx);
        }
        installer_safe_umount(INSTALL_MNT, log_fn, ctx);
        step(progress_fn, ctx, 100, "Done");
        emit_log(log_fn, ctx, "Done. Disk was partitioned and formatted.");
        return 0;
    }

    step(progress_fn, ctx, 48, "Mounting target");
    {
        char *const mount_root[] = { "mount", "-t", FS_TYPE, part, ROOT_MNT, NULL };
        if (run_checked(mount_root, log_fn, ctx) != 0) {
            return 1;
        }
    }
    step(progress_fn, ctx, 58, "Extracting root filesystem");
    if (shell_checked("rm -rf " ROOT_MNT "/* " ROOT_MNT "/.[!.]* " ROOT_MNT "/..?*", log_fn, ctx) != 0 ||
        shell_checked("cd " ROOT_MNT " && gzip -dc " INSTALL_MNT "/boot/initramfs-installer.gz | cpio -idmv", log_fn, ctx) != 0) {
        return 1;
    }
    if (installer_build_syspckg_state_cleanup_command(ROOT_MNT, cmd, sizeof(cmd)) != 0 ||
        shell_checked(cmd, log_fn, ctx) != 0) {
        return 1;
    }
    if (access(ROOT_MNT "/bin/busybox", X_OK) == 0) {
        char *const busybox_install[] = { ROOT_MNT "/bin/busybox", "--install", "-s", ROOT_MNT "/bin", NULL };
        (void)installer_run_command(busybox_install, log_fn, ctx);
    }
    {
        char cmd[1024];
        if (installer_build_shutdown_links_command(ROOT_MNT, cmd, sizeof(cmd)) != 0 ||
            shell_checked(cmd, log_fn, ctx) != 0) {
            return 1;
        }
    }
    {
        char *const mkdir_boot[] = { "mkdir", "-p", ROOT_MNT "/boot", NULL };
        char *const cp_initrd[] = { "cp", INSTALL_MNT "/boot/initramfs-disk.gz", ROOT_MNT "/boot/initramfs-disk.gz", NULL };
        if (run_checked(mkdir_boot, log_fn, ctx) != 0 || run_checked(cp_initrd, log_fn, ctx) != 0) {
            return 1;
        }
    }

    step(progress_fn, ctx, 68, "Creating user account");
    if (write_user_files(cfg, log_fn, ctx) != 0) {
        return 1;
    }

    step(progress_fn, ctx, 72, "Installing target GRUB package");
    if (install_grub_pkg(boot_uefi ? "grub-efi" : "grub-bios", ROOT_MNT, log_fn, ctx) != 0) {
        return 1;
    }
    if (installer_build_copy_grub_mkconfig_command(ROOT_MNT, cmd, sizeof(cmd)) != 0 ||
        shell_checked(cmd, log_fn, ctx) != 0) {
        emit_log(log_fn, ctx, "Failed to copy grub-mkconfig into target root");
        return 1;
    }
    if (installer_build_syspckg_state_cleanup_command(ROOT_MNT, cmd, sizeof(cmd)) != 0 ||
        shell_checked(cmd, log_fn, ctx) != 0) {
        return 1;
    }

    step(progress_fn, ctx, 76, "Copying kernel");
    if (find_kernel(kernel_name, sizeof(kernel_name)) != 0) {
        emit_log(log_fn, ctx, "Kernel not found on installer media");
        return 1;
    }
    snprintf(cmd, sizeof(cmd), "cp '" INSTALL_MNT "/boot/%s' '" ROOT_MNT "/boot/%s' && ln -sf '%s' '" ROOT_MNT "/boot/vmlinuz'",
             kernel_name, kernel_name, kernel_name);
    if (shell_checked(cmd, log_fn, ctx) != 0) {
        return 1;
    }

    step(progress_fn, ctx, 84, "Writing bootloader config");
    {
        char *const mkdir_grub[] = { "mkdir", "-p", ROOT_MNT "/boot/grub", NULL };
        if (run_checked(mkdir_grub, log_fn, ctx) != 0) {
            return 1;
        }
    }
    if (installer_build_root_arg(part, root_arg, sizeof(root_arg)) != 0) {
        emit_log(log_fn, ctx, "Failed to build root kernel argument for %s", part);
        return 1;
    }
    emit_log(log_fn, ctx, "Using root kernel argument: %s", root_arg);
    {
        char default_grub[1024];
        if (installer_build_default_grub_config(root_arg, cfg->acpi_mode, default_grub, sizeof(default_grub)) != 0 ||
            shell_checked("mkdir -p " ROOT_MNT "/etc/default", log_fn, ctx) != 0 ||
            installer_write_file(ROOT_MNT "/etc/default/grub", default_grub) != 0) {
            emit_log(log_fn, ctx, "Failed to write /etc/default/grub: %s", strerror(errno));
            return 1;
        }
    }
    if (installer_build_disable_standard_grub_generators_command(ROOT_MNT, cmd, sizeof(cmd)) != 0 ||
        shell_checked(cmd, log_fn, ctx) != 0 ||
        installer_build_prepare_grub_chroot_command(ROOT_MNT, cmd, sizeof(cmd)) != 0 ||
        shell_checked(cmd, log_fn, ctx) != 0 ||
        installer_build_grub_mkconfig_command(ROOT_MNT, cmd, sizeof(cmd)) != 0 ||
        shell_checked(cmd, log_fn, ctx) != 0) {
        emit_log(log_fn, ctx, "Failed to run grub-mkconfig");
        installer_safe_umount(ROOT_MNT "/sys", log_fn, ctx);
        installer_safe_umount(ROOT_MNT "/proc", log_fn, ctx);
        installer_safe_umount(ROOT_MNT "/dev", log_fn, ctx);
        return 1;
    }

    step(progress_fn, ctx, 94, "Installing bootloader");
    if (boot_uefi) {
        char *const mkdir_efi[] = { "mkdir", "-p", ROOT_MNT "/boot/efi", NULL };
        char *const mount_efi[] = { "mount", "-t", "vfat", efi_part, ROOT_MNT "/boot/efi", NULL };
        char grub_dir[PATH_MAX];
        char dir_arg[PATH_MAX + 16];
        char *const grub_efi[] = {
            "grub-install",
            dir_arg,
            "--target=x86_64-efi",
            "--efi-directory=" ROOT_MNT "/boot/efi",
            "--bootloader-id=AdavaLinux",
            "--removable",
            "--boot-directory=" ROOT_MNT "/boot",
            NULL
        };

        if (run_checked(mkdir_efi, log_fn, ctx) != 0 || run_checked(mount_efi, log_fn, ctx) != 0) {
            return 1;
        }
        if (resolve_grub_platform_dir("x86_64-efi", grub_dir, sizeof(grub_dir), log_fn, ctx) != 0) {
            return 1;
        }
        snprintf(dir_arg, sizeof(dir_arg), "--directory=%s", grub_dir);
        if (run_checked(grub_efi, log_fn, ctx) != 0) {
            return 1;
        }
        if (installer_build_uefi_removable_fallback_command(ROOT_MNT, cmd, sizeof(cmd)) != 0 ||
            shell_checked(cmd, log_fn, ctx) != 0) {
            emit_log(log_fn, ctx, "Failed to create UEFI removable fallback: EFI/BOOT/BOOTX64.EFI");
            return 1;
        }
    } else {
        char grub_dir[PATH_MAX];
        char dir_arg[PATH_MAX + 16];
        char *const grub_bios[] = {
            "grub-install",
            dir_arg,
            "--target=i386-pc",
            "--boot-directory=" ROOT_MNT "/boot",
            "--force",
            "--recheck",
            "--no-floppy",
            "--compress=no",
            "--core-compress=none",
            "--no-rs-codes",
            "--disk-module=biosdisk",
            "--modules=biosdisk part_msdos ext2",
            (char *)cfg->disk,
            NULL
        };

        if (resolve_grub_platform_dir("i386-pc", grub_dir, sizeof(grub_dir), log_fn, ctx) != 0) {
            return 1;
        }
        snprintf(dir_arg, sizeof(dir_arg), "--directory=%s", grub_dir);
        if (run_checked(grub_bios, log_fn, ctx) != 0) {
            return 1;
        }
        if (installer_command_exists("grub-bios-setup")) {
            snprintf(cmd, sizeof(cmd), "grub-bios-setup -v -d " ROOT_MNT "/boot/grub/i386-pc '%s'", cfg->disk);
            if (shell_checked(cmd, log_fn, ctx) != 0) {
                return 1;
            }
        }
        if (access(ROOT_MNT "/boot/grub/i386-pc/core.img", F_OK) != 0) {
            emit_log(log_fn, ctx, "Missing BIOS GRUB core.img after grub-install");
            return 1;
        }
    }

    step(progress_fn, ctx, 98, "Syncing and unmounting");
    if (access(INSTALLER_LOG_PATH, F_OK) == 0) {
        (void)shell_checked("mkdir -p " ROOT_MNT "/var/log && cp " INSTALLER_LOG_PATH " " ROOT_MNT "/var/log/adavalinux-installer.log",
                            log_fn,
                            ctx);
    }
    {
        char *const sync_cmd[] = { "sync", NULL };
        (void)installer_run_command(sync_cmd, log_fn, ctx);
    }
    installer_safe_umount(ROOT_MNT "/boot/efi", log_fn, ctx);
    installer_safe_umount(ROOT_MNT "/sys", log_fn, ctx);
    installer_safe_umount(ROOT_MNT "/proc", log_fn, ctx);
    installer_safe_umount(ROOT_MNT "/dev", log_fn, ctx);
    installer_safe_umount(ROOT_MNT, log_fn, ctx);
    installer_safe_umount(INSTALL_MNT, log_fn, ctx);

    step(progress_fn, ctx, 100, "Done");
    emit_log(log_fn, ctx, "Done. Detach ISO and reboot.");
    return 0;
}
