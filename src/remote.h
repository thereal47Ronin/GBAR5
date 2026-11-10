/*
 * PSXS5 - settings from a phone: a small web page on the local network.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_REMOTE_H
#define PSXS5_REMOTE_H

#include "psxs5.h"

#define REMOTE_PORT 8095

/* Starts or stops the server to match Settings > System > Settings from your phone. */
void remote_update(bool enabled);
/* Main thread, every frame: applies changes made on the phone. */
void remote_frame(void);
/* "http://192.168.1.20:8095/?t=<this console's code>" once the server runs,
 * else "". That is what the QR code and the phone card show. */
const char *remote_address(void);
/* The same address without the code, for places that just say where the page
 * is (the About tab's caption: its QR code carries the code itself). */
const char *remote_address_plain(void);

#endif
