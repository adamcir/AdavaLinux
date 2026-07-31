#ifndef ADAVA_INSTALLER_H
#define ADAVA_INSTALLER_H

#include <stddef.h>

typedef enum {
    INSTALLER_BOOT_BIOS = 0,
    INSTALLER_BOOT_UEFI = 1
} InstallerBootMode;

typedef enum {
    INSTALLER_ACPI_ON = 0,
    INSTALLER_ACPI_OFF = 1
} InstallerAcpiMode;

typedef struct {
    char path[64];
    char model[128];
    unsigned long long mib;
} InstallerDisk;

typedef struct {
    char disk[64];
    InstallerBootMode boot_mode;
    InstallerAcpiMode acpi_mode;
    char part_size[32];
    char username[64];
    char password[128];
    char root_password[128];
    char hostname[64];
} InstallerConfig;

typedef void (*InstallerLogFn)(void *ctx, const char *line);
typedef void (*InstallerProgressFn)(void *ctx, int percent, const char *step);

int installer_valid_username(const char *name);
int installer_supported_disk(const char *path);
int installer_partition_path(const char *disk, int part_no, char *out, size_t out_size);
int installer_build_root_arg(const char *root_dev, char *out, size_t out_size);
int installer_build_fdisk_script(InstallerBootMode boot_mode,
                                const char *part_size,
                                char *out,
                                size_t out_size);
void installer_format_progress_bar(int percent, int width, char *out, size_t out_size);
int installer_should_redraw_progress_log(long now_ms, long last_draw_ms, long min_interval_ms);
void installer_build_syspckg_install_argv(char *argv[6], const char *selector, int local_only);
int installer_valid_uuid_value(const char *value);
int installer_format_failure_summary(const char *log_path, char *out, size_t out_size);
int installer_build_shutdown_links_command(const char *root, char *out, size_t out_size);

#endif
