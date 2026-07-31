#ifndef ADAVA_INSTALLER_UI_H
#define ADAVA_INSTALLER_UI_H

#include "installer.h"

int installer_ui_collect_config(InstallerConfig *cfg);
int installer_ui_run_install(const InstallerConfig *cfg);

#endif
