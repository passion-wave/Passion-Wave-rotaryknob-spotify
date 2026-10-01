// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
/* Lab-only native USB Serial/JTAG setup. The S3/companion UART1 is never touched.
 * hello is public; every other method requires the physical setup window. */
esp_err_t pw_setup_usb_init(void);
