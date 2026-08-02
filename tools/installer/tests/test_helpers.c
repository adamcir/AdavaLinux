#define _POSIX_C_SOURCE 200809L

#include "installer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_username_validation(void)
{
    assert(installer_valid_username("adam") == 1);
    assert(installer_valid_username("user_1-name") == 1);
    assert(installer_valid_username("") == 0);
    assert(installer_valid_username("root") == 0);
    assert(installer_valid_username("Adam") == 0);
    assert(installer_valid_username("bad.name") == 0);
}

static void test_disk_support_filter(void)
{
    assert(installer_supported_disk("/dev/sda") == 1);
    assert(installer_supported_disk("/dev/vda") == 1);
    assert(installer_supported_disk("/dev/xvda") == 1);
    assert(installer_supported_disk("/dev/nvme0n1") == 1);
    assert(installer_supported_disk("/dev/mmcblk0") == 1);
    assert(installer_supported_disk("/dev/sda1") == 0);
    assert(installer_supported_disk("/dev/loop0") == 0);
    assert(installer_supported_disk("/dev/sr0") == 0);
}

static void test_install_media_support_filter(void)
{
    assert(installer_supported_install_media("/dev/sr0") == 1);
    assert(installer_supported_install_media("/dev/sda") == 1);
    assert(installer_supported_install_media("/dev/vda") == 1);
    assert(installer_supported_install_media("/dev/xvda") == 1);
    assert(installer_supported_install_media("/dev/nvme0n1") == 1);
    assert(installer_supported_install_media("/dev/mmcblk0") == 1);
    assert(installer_supported_install_media("/dev/sda1") == 0);
    assert(installer_supported_install_media("/dev/loop0") == 0);
}

static void test_target_disk_excludes_selected_install_media(void)
{
    assert(installer_target_disk_available("/dev/sda", "/dev/sr0") == 1);
    assert(installer_target_disk_available("/dev/sda", "/dev/sda") == 0);
    assert(installer_target_disk_available("/dev/sda", "") == 1);
    assert(installer_target_disk_available("/dev/sda1", "/dev/sr0") == 0);
}

static void test_partition_paths(void)
{
    char out[64];

    assert(installer_partition_path("/dev/sda", 1, out, sizeof(out)) == 0);
    assert(strcmp(out, "/dev/sda1") == 0);

    assert(installer_partition_path("/dev/nvme0n1", 2, out, sizeof(out)) == 0);
    assert(strcmp(out, "/dev/nvme0n1p2") == 0);

    assert(installer_partition_path("/dev/mmcblk0", 1, out, sizeof(out)) == 0);
    assert(strcmp(out, "/dev/mmcblk0p1") == 0);
}

static void test_progress_bar(void)
{
    char out[32];

    installer_format_progress_bar(50, 10, out, sizeof(out));
    assert(strcmp(out, "[#####     ] 50%") == 0);

    installer_format_progress_bar(150, 4, out, sizeof(out));
    assert(strcmp(out, "[####] 100%") == 0);
}

static void test_root_arg_uses_device_path(void)
{
    char out[64];

    assert(installer_build_root_arg("/dev/vda2", out, sizeof(out)) == 0);
    assert(strcmp(out, "root=/dev/vda2") == 0);

    assert(installer_build_root_arg("/dev/sda1", out, sizeof(out)) == 0);
    assert(strcmp(out, "root=/dev/sda1") == 0);
}

static void test_fdisk_script_uses_part_size_for_bios_and_uefi(void)
{
    char out[256];

    assert(installer_build_fdisk_script(INSTALLER_BOOT_BIOS, "+20G", out, sizeof(out)) == 0);
    assert(strcmp(out, "echo o; echo n; echo p; echo 1; echo 2048; echo '+20G'; echo a; echo 1; echo w") == 0);

    assert(installer_build_fdisk_script(INSTALLER_BOOT_UEFI, "+20G", out, sizeof(out)) == 0);
    assert(strcmp(out, "echo o; echo n; echo p; echo 1; echo 2048; echo +512M; echo n; echo p; echo 2; echo 1050624; echo '+20G'; echo t; echo 1; echo ef; echo a; echo 1; echo w") == 0);
}

static void test_progress_log_redraw_throttle(void)
{
    assert(installer_should_redraw_progress_log(1000, 0, 200) == 1);
    assert(installer_should_redraw_progress_log(1100, 1000, 200) == 0);
    assert(installer_should_redraw_progress_log(1200, 1000, 200) == 1);
    assert(installer_should_redraw_progress_log(900, 1000, 200) == 1);
}

static void test_log_line_wraps_to_next_visual_row(void)
{
    char out[64];
    size_t next;

    assert(installer_next_wrapped_log_segment("Takhle pokracuje radek", 0, 20, out, sizeof(out), &next) == 1);
    assert(strcmp(out, "Takhle pokracuje rad") == 0);
    assert(next == 20);
    assert(installer_next_wrapped_log_segment("Takhle pokracuje radek", next, 20, out, sizeof(out), &next) == 1);
    assert(strcmp(out, "ek") == 0);
    assert(next == strlen("Takhle pokracuje radek"));
    assert(installer_next_wrapped_log_segment("short", 0, 20, out, sizeof(out), &next) == 1);
    assert(strcmp(out, "short") == 0);
}

static void test_syspckg_install_argv_order(void)
{
    char *argv[6];

    installer_build_syspckg_install_argv(argv, "grub-bios", 0);
    assert(strcmp(argv[0], "syspckg") == 0);
    assert(strcmp(argv[1], "install") == 0);
    assert(strcmp(argv[2], "grub-bios") == 0);
    assert(strcmp(argv[3], "-y") == 0);
    assert(argv[4] == NULL);

    installer_build_syspckg_install_argv(argv, "/mnt/install/packages/grub-bios.syspckg", 1);
    assert(strcmp(argv[0], "syspckg") == 0);
    assert(strcmp(argv[1], "install") == 0);
    assert(strcmp(argv[2], "/mnt/install/packages/grub-bios.syspckg") == 0);
    assert(strcmp(argv[3], "-local") == 0);
    assert(strcmp(argv[4], "-y") == 0);
    assert(argv[5] == NULL);
}

static void test_syspckg_root_install_argv_order(void)
{
    char *argv[9];

    installer_build_syspckg_root_install_argv(argv, "grub-bios", "/mnt/root", 0);
    assert(strcmp(argv[0], "syspckg") == 0);
    assert(strcmp(argv[1], "install") == 0);
    assert(strcmp(argv[2], "grub-bios") == 0);
    assert(strcmp(argv[3], "--root") == 0);
    assert(strcmp(argv[4], "/mnt/root") == 0);
    assert(strcmp(argv[5], "--allow-root") == 0);
    assert(strcmp(argv[6], "-y") == 0);
    assert(argv[7] == NULL);

    installer_build_syspckg_root_install_argv(argv, "/mnt/install/packages/grub-bios.syspckg", "/mnt/root", 1);
    assert(strcmp(argv[0], "syspckg") == 0);
    assert(strcmp(argv[1], "install") == 0);
    assert(strcmp(argv[2], "/mnt/install/packages/grub-bios.syspckg") == 0);
    assert(strcmp(argv[3], "-local") == 0);
    assert(strcmp(argv[4], "--root") == 0);
    assert(strcmp(argv[5], "/mnt/root") == 0);
    assert(strcmp(argv[6], "--allow-root") == 0);
    assert(strcmp(argv[7], "-y") == 0);
    assert(argv[8] == NULL);
}

static void test_confirmation_phrase_matches_exact_disk_phrase(void)
{
    assert(installer_confirmation_phrase_matches("/dev/sda", "ERASE /dev/sda") == 1);
    assert(installer_confirmation_phrase_matches("/dev/sda", "ERASE /dev/vda") == 0);
    assert(installer_confirmation_phrase_matches("/dev/sda", "erase /dev/sda") == 0);
    assert(installer_confirmation_phrase_matches("", "ERASE ") == 0);
}

static void test_uuid_value_validation(void)
{
    assert(installer_valid_uuid_value("2f1d1af6-5d86-4d9f-b86e-25dc2f2ce8af") == 1);
    assert(installer_valid_uuid_value("ABCDEF12-3456") == 1);
    assert(installer_valid_uuid_value("") == 0);
    assert(installer_valid_uuid_value("/dev/vda2") == 0);
    assert(installer_valid_uuid_value("UUID=/dev/vda2") == 0);
    assert(installer_valid_uuid_value("abc def") == 0);
}

static void test_failure_summary_includes_recent_log_lines(void)
{
    char path[] = "/tmp/installer-log-test.XXXXXX";
    char out[512];
    int fd = mkstemp(path);
    FILE *f;

    assert(fd >= 0);
    f = fdopen(fd, "w");
    assert(f != NULL);
    fputs("AdavaLinux installer log\n", f);
    fputs("$ mkfs.ext4 /dev/vda1\n", f);
    fputs("mkfs.ext4: Device size reported as zero\n", f);
    fputs("command exit: 1\n", f);
    fputs("command failed with exit code 1\n", f);
    fclose(f);

    assert(installer_format_failure_summary(path, out, sizeof(out)) == 0);
    assert(strstr(out, "The installer stopped because a command failed.") != NULL);
    assert(strstr(out, "$ mkfs.ext4 /dev/vda1") != NULL);
    assert(strstr(out, "mkfs.ext4: Device size reported as zero") != NULL);
    assert(strstr(out, "command failed with exit code 1") != NULL);

    unlink(path);
}

static void test_shutdown_links_command_covers_bin_and_sbin(void)
{
    char out[512];

    assert(installer_build_shutdown_links_command("/mnt/root", out, sizeof(out)) == 0);
    assert(strstr(out, "mkdir -p /mnt/root/bin /mnt/root/sbin") != NULL);
    assert(strstr(out, "ln -sf busybox /mnt/root/bin/reboot") != NULL);
    assert(strstr(out, "ln -sf busybox /mnt/root/bin/poweroff") != NULL);
    assert(strstr(out, "ln -sf busybox /mnt/root/bin/halt") != NULL);
    assert(strstr(out, "ln -sf ../bin/busybox /mnt/root/sbin/reboot") != NULL);
    assert(strstr(out, "ln -sf ../bin/busybox /mnt/root/sbin/poweroff") != NULL);
    assert(strstr(out, "ln -sf ../bin/busybox /mnt/root/sbin/halt") != NULL);
}

static void test_grub_mkconfig_command_targets_mounted_root(void)
{
    char out[512];

    assert(installer_build_grub_mkconfig_command("/mnt/root", out, sizeof(out)) == 0);
    assert(strcmp(out, "chroot /mnt/root /usr/sbin/grub-mkconfig -o /boot/grub/grub.cfg") == 0);
}

static void test_default_grub_config_keeps_kms_enabled(void)
{
    char out[512];

    assert(installer_build_default_grub_config("root=/dev/vda2", INSTALLER_ACPI_ON, out, sizeof(out)) == 0);
    assert(strcmp(out,
                  "GRUB_TIMEOUT=10\n"
                  "GRUB_DEFAULT=0\n"
                  "GRUB_CMDLINE_LINUX=\"root=/dev/vda2 rootfstype=ext4 rootwait rootdelay=5 rw console=ttyS0 console=tty1 libata.force=noncq\"\n"
                  "GRUB_CMDLINE_LINUX_DEFAULT=\"quiet\"\n") == 0);
    assert(strstr(out, "nomodeset") == NULL);
}

static void test_copy_grub_mkconfig_command_installs_target_file(void)
{
    char out[512];

    assert(installer_build_copy_grub_mkconfig_command("/mnt/root", out, sizeof(out)) == 0);
    assert(strcmp(out, "mkdir -p /mnt/root/usr/sbin && cp -f /usr/sbin/grub-mkconfig /mnt/root/usr/sbin/grub-mkconfig && chmod 755 /mnt/root/usr/sbin/grub-mkconfig") == 0);
}

static void test_prepare_grub_chroot_mounts_command_mounts_runtime_filesystems(void)
{
    char out[512];

    assert(installer_build_prepare_grub_chroot_command("/mnt/root", out, sizeof(out)) == 0);
    assert(strcmp(out, "mkdir -p /mnt/root/dev /mnt/root/proc /mnt/root/sys && mount --bind /dev /mnt/root/dev && mount -t proc proc /mnt/root/proc && mount -t sysfs sysfs /mnt/root/sys") == 0);
}

static void test_disable_standard_grub_generators_command_keeps_adavalinux_only(void)
{
    char out[768];

    assert(installer_build_disable_standard_grub_generators_command("/mnt/root", out, sizeof(out)) == 0);
    assert(strstr(out, "chmod a-x /mnt/root/usr/etc/grub.d/10_linux") != NULL);
    assert(strstr(out, "/mnt/root/usr/etc/grub.d/20_linux_xen") != NULL);
    assert(strstr(out, "/mnt/root/usr/etc/grub.d/30_os-prober") != NULL);
    assert(strstr(out, "chmod a+x /mnt/root/usr/etc/grub.d/00_header") != NULL);
    assert(strstr(out, "/mnt/root/usr/etc/grub.d/10_adavalinux") != NULL);
    assert(strstr(out, "/mnt/root/etc/grub.d/10_adavalinux") != NULL);
}

static void test_uefi_removable_fallback_command_creates_bootx64(void)
{
    char out[768];

    assert(installer_build_uefi_removable_fallback_command("/mnt/root", out, sizeof(out)) == 0);
    assert(strstr(out, "mkdir -p /mnt/root/boot/efi/EFI/BOOT") != NULL);
    assert(strstr(out, "/mnt/root/boot/efi/EFI/BOOT/BOOTX64.EFI") != NULL);
    assert(strstr(out, "/mnt/root/boot/efi/EFI/AdavaLinux/grubx64.efi") != NULL);
    assert(strstr(out, "find /mnt/root/boot/efi/EFI") != NULL);
}

static void test_syspckg_state_cleanup_removes_installer_and_manifests(void)
{
    char out[768];

    assert(installer_build_syspckg_state_cleanup_command("/mnt/root", out, sizeof(out)) == 0);
    assert(strstr(out, "/mnt/root/usr/bin/installer") != NULL);
    assert(strstr(out, "/mnt/root/usr/share/syspckg/packages") != NULL);
    assert(strstr(out, "/mnt/root/var/cache/syspckg") != NULL);
    assert(strstr(out, "/mnt/root/var/lib/syspckg/packages") != NULL);
    assert(strstr(out, "/mnt/root/var/lib/syspckg/installed") != NULL);
}

int main(void)
{
    test_username_validation();
    test_disk_support_filter();
    test_install_media_support_filter();
    test_target_disk_excludes_selected_install_media();
    test_partition_paths();
    test_progress_bar();
    test_root_arg_uses_device_path();
    test_fdisk_script_uses_part_size_for_bios_and_uefi();
    test_progress_log_redraw_throttle();
    test_log_line_wraps_to_next_visual_row();
    test_syspckg_install_argv_order();
    test_confirmation_phrase_matches_exact_disk_phrase();
    test_syspckg_root_install_argv_order();
    test_uuid_value_validation();
    test_failure_summary_includes_recent_log_lines();
    test_shutdown_links_command_covers_bin_and_sbin();
    test_grub_mkconfig_command_targets_mounted_root();
    test_default_grub_config_keeps_kms_enabled();
    test_copy_grub_mkconfig_command_installs_target_file();
    test_prepare_grub_chroot_mounts_command_mounts_runtime_filesystems();
    test_disable_standard_grub_generators_command_keeps_adavalinux_only();
    test_uefi_removable_fallback_command_creates_bootx64();
    test_syspckg_state_cleanup_removes_installer_and_manifests();
    puts("helper tests passed");
    return 0;
}
