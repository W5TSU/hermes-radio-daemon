/* hermes-radio-daemon - embedded sBitx bootstrap
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SBITX_BOOTSTRAP_H_
#define SBITX_BOOTSTRAP_H_

#include <stdbool.h>

int sbitx_bootstrap(const char *cfg_radio_path,
                    const char *cfg_user_path,
                    bool cpu_arg_provided,
                    int cpu_nr);

#endif /* SBITX_BOOTSTRAP_H_ */
