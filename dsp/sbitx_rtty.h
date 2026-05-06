/* sBitx RTTY (Radio Teletype) modem
 *
 * TX: Baudot encoding + FSK tone generator (DDS-based).
 * RX: SSB demod + minimodem FSK detector (FFT-based).
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SBITX_RTTY_H_
#define SBITX_RTTY_H_

#include <stdbool.h>
#include <stdint.h>

bool sbitx_rtty_init(int baud, int mark, int shift);
void sbitx_rtty_shutdown(void);

int  sbitx_rtty_encode(const char *message, float *signal, int max_n,
                       int baud, int mark, int shift);

int  sbitx_rtty_rx_samples_per_block(void);
void sbitx_rtty_rx_process(const float *audio_12k, int n,
                           int baud, int mark, int shift,
                           void (*char_cb)(char));

#endif /* SBITX_RTTY_H_ */
