/* sBitx DRM (Digital Radio Mondiale) support via Dream subprocess
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SBITX_DRM_H_
#define SBITX_DRM_H_

#include <stdbool.h>
#include <stdint.h>

bool sbitx_drm_init(const char *dream_path, uint32_t sigsrate, uint32_t audsrate);
void sbitx_drm_shutdown(void);
void sbitx_drm_process(const float *iq_i, const float *iq_q, int n,
                       float *audio_out, int *out_n);
int  sbitx_drm_audio_available(void);

#endif /* SBITX_DRM_H_ */
