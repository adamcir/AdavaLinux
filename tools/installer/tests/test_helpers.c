#include "installer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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

static void test_progress_log_redraw_throttle(void)
{
    assert(installer_should_redraw_progress_log(1000, 0, 200) == 1);
    assert(installer_should_redraw_progress_log(1100, 1000, 200) == 0);
    assert(installer_should_redraw_progress_log(1200, 1000, 200) == 1);
    assert(installer_should_redraw_progress_log(900, 1000, 200) == 1);
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

static void test_uuid_value_validation(void)
{
    assert(installer_valid_uuid_value("2f1d1af6-5d86-4d9f-b86e-25dc2f2ce8af") == 1);
    assert(installer_valid_uuid_value("ABCDEF12-3456") == 1);
    assert(installer_valid_uuid_value("") == 0);
    assert(installer_valid_uuid_value("/dev/vda2") == 0);
    assert(installer_valid_uuid_value("UUID=/dev/vda2") == 0);
    assert(installer_valid_uuid_value("abc def") == 0);
}

int main(void)
{
    test_username_validation();
    test_disk_support_filter();
    test_partition_paths();
    test_progress_bar();
    test_root_arg_uses_device_path();
    test_progress_log_redraw_throttle();
    test_syspckg_install_argv_order();
    test_uuid_value_validation();
    puts("helper tests passed");
    return 0;
}
