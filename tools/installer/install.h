#ifndef ADAVA_INSTALLER_INSTALL_H
#define ADAVA_INSTALLER_INSTALL_H

#include "installer.h"

int installer_run_install(const InstallerConfig *cfg,
                          InstallerLogFn log_fn,
                          InstallerProgressFn progress_fn,
                          void *ctx);

#endif
