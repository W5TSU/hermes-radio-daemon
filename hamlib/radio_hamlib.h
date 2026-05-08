/* hermes-radio-daemon - Hamlib radio backend
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The hamlib backend implementation functions are file-local statics in
 * radio_hamlib.c, exposed only via hamlib_backend_ops (declared in
 * radio_backend.h).
 */

#ifndef RADIO_HAMLIB_H_
#define RADIO_HAMLIB_H_

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

#include "radio.h"

/* Global shutdown flag (defined in radio_daemon.c) */
extern _Atomic bool shutdown_;

/* Profile-fallback timer state (defined in radio_hamlib.c). */
extern _Atomic bool   timer_reset;
extern _Atomic time_t timeout_counter;

/* Backend vtable exposed by radio_hamlib.c. */
struct radio_backend_ops;
extern const struct radio_backend_ops hamlib_backend_ops;

#endif /* RADIO_HAMLIB_H_ */
