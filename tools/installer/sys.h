#ifndef ADAVA_INSTALLER_SYS_H
#define ADAVA_INSTALLER_SYS_H

#include "installer.h"

#include <stddef.h>

int installer_scan_disks(InstallerDisk *disks, size_t max_disks, size_t *count);
int installer_run_command(char *const argv[], InstallerLogFn log_fn, void *log_ctx);
int installer_run_shell(const char *command, InstallerLogFn log_fn, void *log_ctx);
int installer_write_file(const char *path, const char *content);
int installer_read_first_line(const char *path, char *out, size_t out_size);
int installer_is_block_device(const char *path);
int installer_command_exists(const char *name);
void installer_safe_umount(const char *mountpoint, InstallerLogFn log_fn, void *log_ctx);

#endif
