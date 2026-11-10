/* PSXS5 - the file system the emulators are given (libretro VFS v3): local
 * files, and games on network shares (netfs.h).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "libretro.h"

#include <stdbool.h>

/* RETRO_ENVIRONMENT_GET_VFS_INTERFACE */
bool vfs_interface(struct retro_vfs_interface_info *info);
