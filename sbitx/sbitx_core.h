/* sbitx hardware backend - HERMES
 *
 * Copyright (C) 2023-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Embedded sBitx hardware-internal API. Public types and constants live in
 * the canonical radio.h. This header now contains only the entry points
 * the embedded HW/DSP/ALSA/IO code shares between its TUs. The backend
 * implementation functions (set_frequency, set_mode, get_fwd_power, ...)
 * are file-local statics in sbitx_core.c and exposed only through the
 * radio_backend_ops vtable.
 */

#ifndef SBITX_CORE_H_
#define SBITX_CORE_H_

#include "radio.h"

/* Hardware init / shutdown — called from the hfsignals backend init op. */
bool hw_init(radio *radio_h, pthread_t *hw_tids);
bool hw_shutdown(radio *radio_h, pthread_t *hw_tids);

/* Profile defaults + hw variant detection helpers used at startup. */
void radio_apply_defaults(radio *radio_h);
bool radio_is_zbitx(const radio *radio_h);

/* Audio bridge ring helpers (sbitx_bridge.c). The ring buffers themselves
 * live in radio_h->rx_audio_ring / tx_audio_ring (declared in radio.h),
 * shared with the websocket layer. */
bool   sbitx_bridge_init(radio *radio_h);
void   sbitx_bridge_shutdown(radio *radio_h);
void   sbitx_bridge_push_rx(radio *radio_h, const int16_t *samples, size_t nsamples);
size_t sbitx_bridge_pop_tx(radio *radio_h, int16_t *samples, size_t max_samples);

/* HW I/O thread + tick. */
void *hw_thread(void *radio_h_v);
void  io_tick(radio *radio_h);

/* LPF band switching. */
void lpf_off(radio *radio_h);
void lpf_set(radio *radio_h);

/* Periodic-timer helpers (used by hw_thread). */
void wait_next_activation(void);
int  start_periodic_timer(uint64_t offset);

/* Backend implementation entry points. These will be marked static + exposed
 * only via sbitx_backend_ops in a follow-up commit; for now they are
 * extern so the embedded sbitx_shm.c / sbitx_websocket.c (also slated for
 * deletion) can link against them. */
void set_frequency(radio *radio_h, uint32_t frequency, uint32_t profile);
void set_mode(radio *radio_h, uint16_t mode, uint32_t profile);
void set_bfo(radio *radio_h, uint32_t frequency);
void set_reflected_threshold(radio *radio_h, uint32_t ref_threshold);
void set_speaker_volume(radio *radio_h, uint32_t speaker_level, uint32_t profile);
void set_profile(radio *radio_h, uint32_t profile);
void set_serial(radio *radio_h, uint32_t serial);
void set_profile_timeout(radio *radio_h, int32_t timeout);
void set_power_knob(radio *radio_h, uint16_t power_level, uint32_t profile);
void set_digital_voice(radio *radio_h, bool digital_voice, uint32_t profile);
void tr_switch(radio *radio_h, bool txrx_state);
bool update_power_measurements(radio *radio_h);
uint32_t get_fwd_power(radio *radio_h);
uint32_t get_ref_power(radio *radio_h);
uint32_t get_swr(radio *radio_h);
void swr_protection_check(radio *radio_h);

/* Backend vtable exposed by sbitx_core.c. The struct definition lives in
 * radio_backend.h; here it's a forward decl so we don't need that header. */
struct radio_backend_ops;
extern const struct radio_backend_ops sbitx_backend_ops;

#endif /* SBITX_CORE_H_ */
