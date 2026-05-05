/* sBitx CW (Morse code) modem
 *
 * Uses unixcw library for RX decoding state machine.
 * TX is a simple DDS tone generator with raised-cosine envelope.
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SBITX_CW_H_
#define SBITX_CW_H_

#include <stdbool.h>
#include <stdint.h>

bool sbitx_cw_init(int wpm, int pitch);
void sbitx_cw_shutdown(void);

int  sbitx_cw_encode(const char *message, float *signal, int max_n,
                     int wpm, int pitch);

void sbitx_cw_set_wpm(int wpm);
void sbitx_cw_set_pitch(int pitch);

int  sbitx_cw_rx_samples_per_block(void);
int  sbitx_cw_rx_process(const float *audio_12k, int n, char *decoded, int max_len,
                          int wpm, int pitch);

#endif /* SBITX_CW_H_ */
