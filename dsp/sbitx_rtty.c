/* sBitx RTTY (Radio Teletype) modem
 *
 * TX: Baudot encoding + FSK tone generator at 96 kHz.
 * RX: SSB demod → 12 kHz → minimodem FSK detector → Baudot decode.
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsk.h"
#include "baudot.h"

#include "sbitx_rtty.h"

#define RTTY_RX_SAMPLE_RATE 12000

static fsk_plan *rtty_plan = NULL;
static int rtty_baud_rate = 45;
static int rtty_mark_hz = 1585;
static int rtty_shift_hz = 170;

static int rtty_rx_bit_count = 0;
static int rtty_rx_frame_bits = 0;
static unsigned long long rtty_rx_frame = 0;
static int rtty_samples_per_bit = 0;
static int rtty_rx_since_bit = 0;

bool sbitx_rtty_init(int baud, int mark, int shift)
{
    rtty_baud_rate = baud;
    rtty_mark_hz = mark;
    rtty_shift_hz = shift;

    if (rtty_plan)
    {
        fsk_plan_destroy(rtty_plan);
        rtty_plan = NULL;
    }

    float bw = 50.0f;
    rtty_plan = fsk_plan_new(RTTY_RX_SAMPLE_RATE, (float) mark,
                             (float) (mark - shift), bw);
    if (!rtty_plan)
        return false;

    rtty_rx_bit_count = 0;
    rtty_rx_frame_bits = 0;
    rtty_rx_frame = 0;
    rtty_samples_per_bit = RTTY_RX_SAMPLE_RATE / baud;
    rtty_rx_since_bit = 0;

    baudot_reset();
    return true;
}

void sbitx_rtty_shutdown(void)
{
    if (rtty_plan)
    {
        fsk_plan_destroy(rtty_plan);
        rtty_plan = NULL;
    }
}

int sbitx_rtty_encode(const char *message, float *signal, int max_n,
                      int baud, int mark, int shift)
{
    int space = mark - shift;
    int samples_per_bit = 96000 / baud;
    int slope_n = samples_per_bit / 8;
    float phase_mark = 0.0f;
    float phase_space = 0.0f;
    float dds_mark = 2.0f * (float) M_PI * mark / 96000.0f;
    float dds_space = 2.0f * (float) M_PI * space / 96000.0f;
    int idx = 0;

    for (const char *p = message; *p && idx < max_n; p++)
    {
        unsigned int bits = 0;
        int nbits = baudot_encode(&bits, *p);
        if (nbits < 1) continue;

        for (int b = 0; b < nbits && idx < max_n; b++)
        {
            unsigned char word = (unsigned char)((bits >> (5 * (nbits - 1 - b))) & 0x1F);

            // start bit (space)
            for (int i = 0; i < samples_per_bit && idx < max_n; i++, idx++)
            {
                phase_space += dds_space;
                if (phase_space > 2.0f * (float) M_PI) phase_space -= 2.0f * (float) M_PI;
                signal[idx] = 0.4f * sinf(phase_space);
            }

            // 5 data bits (LSB first)
            for (int d = 0; d < 5 && idx < max_n; d++)
            {
                bool is_mark = ((word >> d) & 1) ? false : true;

                for (int i = 0; i < samples_per_bit && idx < max_n; i++, idx++)
                {
                    float env = 1.0f;
                    if (i < slope_n) env = 0.5f - 0.5f * cosf((float) M_PI * i / slope_n);
                    else if (i >= samples_per_bit - slope_n) env = 0.5f + 0.5f * cosf((float) M_PI * (i - samples_per_bit + slope_n) / slope_n);

                    if (is_mark)
                    {
                        phase_mark += dds_mark;
                        if (phase_mark > 2.0f * (float) M_PI) phase_mark -= 2.0f * (float) M_PI;
                        signal[idx] = env * 0.4f * sinf(phase_mark);
                    }
                    else
                    {
                        phase_space += dds_space;
                        if (phase_space > 2.0f * (float) M_PI) phase_space -= 2.0f * (float) M_PI;
                        signal[idx] = env * 0.4f * sinf(phase_space);
                    }
                }
            }

            // 1.5 stop bits (mark)
            for (int i = 0; i < samples_per_bit + samples_per_bit / 2 && idx < max_n; i++, idx++)
            {
                float env = 1.0f;
                if (i < slope_n) env = 0.5f - 0.5f * cosf((float) M_PI * i / slope_n);
                phase_mark += dds_mark;
                if (phase_mark > 2.0f * (float) M_PI) phase_mark -= 2.0f * (float) M_PI;
                signal[idx] = env * 0.4f * sinf(phase_mark);
            }
        }
    }

    return idx;
}

int sbitx_rtty_rx_samples_per_block(void)
{
    return rtty_samples_per_bit * 8;
}

void sbitx_rtty_rx_process(const float *audio_12k, int n,
                           int baud, int mark, int shift,
                           void (*char_cb)(char))
{
    (void) baud;
    (void) mark;
    (void) shift;

    if (!rtty_plan || !char_cb)
        return;

    float *samples = (float *) audio_12k;
    unsigned int frame_nsamples = rtty_samples_per_bit * 8;
    unsigned long long bits = 0;
    float ampl = 0.0f;
    unsigned int frame_start = 0;

    float conf = fsk_find_frame(rtty_plan, samples, (unsigned int) n,
                                0, (unsigned int) n - frame_nsamples,
                                rtty_samples_per_bit / 4,
                                0.8f, "10ddddd1",
                                &bits, &ampl, &frame_start);

    if (conf > 1.5f)
    {
        unsigned char word = (unsigned char)(bits & 0x1F);
        char ch = 0;
        if (baudot_decode(&ch, word))
            char_cb(ch);
    }
}
