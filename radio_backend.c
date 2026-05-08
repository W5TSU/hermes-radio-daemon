/* hermes-radio-daemon - radio backend abstraction
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "cfg_utils.h"
#include "radio_backend.h"
#include "radio_daemon_core.h"
#include "rig_server.h"

/* Timer reset flag — defined by hamlib/radio_hamlib.c (used only by hamlib's
 * profile-timeout io thread; harmless when hfsignals backend is selected). */
extern _Atomic bool timer_reset;

/* Each backend TU defines its own const radio_backend_ops; we just pick one
 * here based on the radio_backend kind from cfg. No backend-implementation
 * function is exposed by name — the vtable is the only contract. */
extern const radio_backend_ops hamlib_backend_ops;
extern const radio_backend_ops sbitx_backend_ops;

static const radio_backend_ops *radio_backend_ops_for_kind(radio_backend_kind kind)
{
    switch (kind)
    {
    case RADIO_BACKEND_HFSIGNALS:
        return &sbitx_backend_ops;
    case RADIO_BACKEND_HAMLIB:
    default:
        return &hamlib_backend_ops;
    }
}

static const radio_backend_ops *radio_backend_ops_from_radio(const radio *radio_h)
{
    if (!radio_h || !radio_h->backend_ops)
        return NULL;

    return radio_h->backend_ops;
}

bool radio_backend_detect(const char *cfg_radio_path, radio_backend_selection *selection)
{
    if (!selection)
        return false;

    selection->kind = RADIO_BACKEND_HAMLIB;
    cfg_detect_backend(cfg_radio_path, &selection->kind);
    selection->ops = radio_backend_ops_for_kind(selection->kind);
    return selection->ops != NULL;
}

void radio_backend_configure(radio *radio_h, const radio_backend_selection *selection)
{
    if (!radio_h || !selection)
        return;

    radio_h->backend_kind = selection->kind;
    radio_h->backend_ops = selection->ops;
}

int radio_backend_run(const radio_backend_selection *selection,
                      const radio_daemon_runtime *runtime)
{
    if (!selection || !selection->ops || !runtime)
        return -1;

    /* Both backends now run through the same daemon-core loop. The
     * backend's init op handles backend-specific bring-up
     * (hamlib_init / sbitx hw_init+dsp_init+sound_system_init). */
    return radio_daemon_core_run(selection, runtime);
}

bool radio_backend_init(radio *radio_h)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    bool ok = ops && ops->init ? ops->init(radio_h) : false;
    rig_server_start(radio_h);
    return ok;
}

void radio_backend_shutdown(radio *radio_h)
{
    rig_server_stop();
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->shutdown)
        ops->shutdown(radio_h);
}

void *radio_backend_io_thread(void *radio_h_v)
{
    radio *radio_h = (radio *) radio_h_v;
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);

    if (!ops || !ops->io_thread)
        return NULL;

    return ops->io_thread(radio_h_v);
}

void radio_backend_set_frequency(radio *radio_h, uint32_t frequency, uint32_t profile)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_frequency)
        ops->set_frequency(radio_h, frequency, profile);
}

void radio_backend_set_mode(radio *radio_h, uint16_t mode, uint32_t profile)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_mode)
        ops->set_mode(radio_h, mode, profile);
}

void radio_backend_set_txrx_state(radio *radio_h, bool txrx_state)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_txrx_state)
        ops->set_txrx_state(radio_h, txrx_state);
}

void radio_backend_set_bfo(radio *radio_h, uint32_t frequency)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_bfo)
        ops->set_bfo(radio_h, frequency);
}

void radio_backend_set_reflected_threshold(radio *radio_h, uint32_t ref_threshold)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_reflected_threshold)
        ops->set_reflected_threshold(radio_h, ref_threshold);
}

void radio_backend_set_speaker_volume(radio *radio_h, uint32_t speaker_level, uint32_t profile)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_speaker_volume)
        ops->set_speaker_volume(radio_h, speaker_level, profile);
}

void radio_backend_set_serial(radio *radio_h, uint32_t serial)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_serial)
        ops->set_serial(radio_h, serial);
}

void radio_backend_set_profile_timeout(radio *radio_h, int32_t timeout)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_profile_timeout)
        ops->set_profile_timeout(radio_h, timeout);
}

void radio_backend_set_power_level(radio *radio_h, uint16_t power_level, uint32_t profile)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_power_level)
        ops->set_power_level(radio_h, power_level, profile);
}

void radio_backend_set_digital_voice(radio *radio_h, bool digital_voice, uint32_t profile)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_digital_voice)
        ops->set_digital_voice(radio_h, digital_voice, profile);
}

void radio_backend_set_step_size(radio *radio_h, uint32_t step_size)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_step_size)
        ops->set_step_size(radio_h, step_size);
}

void radio_backend_set_tone_generation(radio *radio_h, bool tone_generation)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_tone_generation)
        ops->set_tone_generation(radio_h, tone_generation);
}

void radio_backend_set_profile(radio *radio_h, uint32_t profile)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->set_profile)
        ops->set_profile(radio_h, profile);
}

uint32_t radio_backend_get_fwd_power(radio *radio_h)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->get_fwd_power)
        return ops->get_fwd_power(radio_h);

    return radio_h ? radio_h->fwd_power : 0;
}

uint32_t radio_backend_get_ref_power(radio *radio_h)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->get_ref_power)
        return ops->get_ref_power(radio_h);

    return radio_h ? radio_h->ref_power : 0;
}

uint32_t radio_backend_get_swr(radio *radio_h)
{
    const radio_backend_ops *ops = radio_backend_ops_from_radio(radio_h);
    if (ops && ops->get_swr)
        return ops->get_swr(radio_h);

    return 10;
}

void radio_backend_reset_timeout_timer(void)
{
    timer_reset = true;
}
