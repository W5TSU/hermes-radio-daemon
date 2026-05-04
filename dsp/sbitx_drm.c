/* sBitx DRM (Digital Radio Mondiale) support via Dream subprocess
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define USE_FFTW
#define LIBCSDR_GPL
#include <fft_fftw.h>
#include <libcsdr.h>
#include <libcsdr_gpl.h>

#include "sbitx_drm.h"

static pid_t dream_pid = -1;
static FILE *dream_in = NULL;
static FILE *dream_out = NULL;

static rational_resampler_ff_t rs_i_state = {0, 0, 0};
static rational_resampler_ff_t rs_q_state = {0, 0, 0};
static rational_resampler_ff_t rs_up_state = {0, 0, 0};
static float *rs_dn_taps = NULL;
static float *rs_up_taps = NULL;
static int rs_dn_taps_len = 0;
static int rs_up_taps_len = 0;
static bool rs_ready = false;

static float i_48k[2048];
static float q_48k[2048];
static float audio_8k[2048];

static int dmode_audio_available = 0;

static void init_resamplers(void)
{
    if (rs_ready)
        return;

    float tbw = 0.05f;
    rs_dn_taps_len = firdes_filter_len(tbw);
    rs_dn_taps = malloc(rs_dn_taps_len * sizeof(float));
    rational_resampler_get_lowpass_f(rs_dn_taps, rs_dn_taps_len, 1, 2, WINDOW_BLACKMAN);

    rs_up_taps_len = firdes_filter_len(tbw);
    rs_up_taps = malloc(rs_up_taps_len * sizeof(float));
    rational_resampler_get_lowpass_f(rs_up_taps, rs_up_taps_len, 12, 1, WINDOW_BLACKMAN);

    rs_ready = true;
}

bool sbitx_drm_init(const char *dream_path, uint32_t sigsrate, uint32_t audsrate)
{
    char sigsrate_str[16], audsrate_str[16];
    int pipe_stdin[2], pipe_stdout[2];

    if (!dream_path || !dream_path[0])
    {
        fprintf(stderr, "DRM: dream_path not set, DRM disabled\n");
        return false;
    }

    if (pipe(pipe_stdin) < 0 || pipe(pipe_stdout) < 0)
    {
        fprintf(stderr, "DRM: pipe failed: %s\n", strerror(errno));
        return false;
    }

    dream_pid = fork();
    if (dream_pid < 0)
    {
        fprintf(stderr, "DRM: fork failed: %s\n", strerror(errno));
        close(pipe_stdin[0]); close(pipe_stdin[1]);
        close(pipe_stdout[0]); close(pipe_stdout[1]);
        return false;
    }

    if (dream_pid == 0)
    {
        dup2(pipe_stdin[0], STDIN_FILENO);
        dup2(pipe_stdout[1], STDOUT_FILENO);
        close(pipe_stdin[0]); close(pipe_stdin[1]);
        close(pipe_stdout[0]); close(pipe_stdout[1]);

        for (int fd = 3; fd < 256; fd++)
            close(fd);

        snprintf(sigsrate_str, sizeof(sigsrate_str), "%u", sigsrate);
        snprintf(audsrate_str, sizeof(audsrate_str), "%u", audsrate);

        execlp(dream_path, dream_path,
               "--console",
               "-I", "-",
               "-O", "-",
               "--sigsrate", sigsrate_str,
               "--inchansel", "6",
               "--audsrate", audsrate_str,
               (char *) NULL);

        fprintf(stderr, "DRM: exec dream failed: %s\n", strerror(errno));
        _exit(1);
    }

    close(pipe_stdin[0]);
    close(pipe_stdout[1]);

    dream_in = fdopen(pipe_stdin[1], "w");
    dream_out = fdopen(pipe_stdout[0], "r");
    if (!dream_in || !dream_out)
    {
        fprintf(stderr, "DRM: fdopen failed\n");
        sbitx_drm_shutdown();
        return false;
    }

    init_resamplers();

    fprintf(stderr, "DRM: Dream subprocess started (pid=%d, sig=%u aud=%u)\n",
            dream_pid, sigsrate, audsrate);
    return true;
}

void sbitx_drm_shutdown(void)
{
    if (dream_pid > 0)
    {
        kill(dream_pid, SIGTERM);
        int status;
        for (int i = 0; i < 20; i++)
        {
            if (waitpid(dream_pid, &status, WNOHANG) > 0)
                break;
            usleep(100000);
        }
        if (waitpid(dream_pid, &status, WNOHANG) == 0)
        {
            kill(dream_pid, SIGKILL);
            waitpid(dream_pid, &status, 0);
        }
        dream_pid = -1;
    }

    if (dream_in) { fclose(dream_in); dream_in = NULL; }
    if (dream_out) { fclose(dream_out); dream_out = NULL; }

    free(rs_dn_taps); rs_dn_taps = NULL;
    free(rs_up_taps); rs_up_taps = NULL;
    rs_ready = false;

    fprintf(stderr, "DRM: shutdown complete\n");
}

void sbitx_drm_process(const float *iq_i, const float *iq_q, int n,
                       float *audio_out, int *out_n)
{
    *out_n = 0;
    if (!dream_in || !dream_out || !rs_ready)
        return;

    float i_96k[n];
    float q_96k[n];
    for (int k = 0; k < n; k++)
    {
        i_96k[k] = iq_i[k];
        q_96k[k] = iq_q[k];
    }

    rs_i_state = rational_resampler_ff(i_96k, i_48k, n, 1, 2,
                                       rs_dn_taps, rs_dn_taps_len,
                                       rs_i_state.last_taps_delay);
    rs_q_state = rational_resampler_ff(q_96k, q_48k, n, 1, 2,
                                       rs_dn_taps, rs_dn_taps_len,
                                       rs_q_state.last_taps_delay);

    int m = rs_i_state.output_size;
    if (rs_q_state.output_size < m)
        m = rs_q_state.output_size;

    if (m > 0)
    {
        static int16_t s16_buf[4096];
        for (int k = 0; k < m; k++)
        {
            float iv = i_48k[k] * 32767.0f;
            float qv = q_48k[k] * 32767.0f;
            if (iv > 32767.0f) iv = 32767.0f;
            if (iv < -32768.0f) iv = -32768.0f;
            if (qv > 32767.0f) qv = 32767.0f;
            if (qv < -32768.0f) qv = -32768.0f;
            s16_buf[k * 2] = (int16_t) iv;
            s16_buf[k * 2 + 1] = (int16_t) qv;
        }

        fwrite(s16_buf, sizeof(int16_t), (size_t)m * 2, dream_in);
    }

    clearerr(dream_out);
    size_t audiobytes = fread(audio_8k, sizeof(float), (size_t) n, dream_out);

    if (audiobytes > 0)
    {
        int audiosamples = (int) audiobytes;

        static int16_t s16_8k[2048];
        int s16_count = 0;
        for (int k = 0; k < audiosamples && k < 2048; k++)
        {
            float v = audio_8k[k];
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            audio_8k[k] = v;
        }

        rs_up_state = rational_resampler_ff(audio_8k, audio_out, audiosamples,
                                            12, 1, rs_up_taps, rs_up_taps_len,
                                            rs_up_state.last_taps_delay);
        *out_n = rs_up_state.output_size;
        if (*out_n > n) *out_n = n;

        float max_amp = 0.01f;
        for (int k = 0; k < *out_n; k++)
        {
            float a = audio_out[k] > 0 ? audio_out[k] : -audio_out[k];
            if (a > max_amp) max_amp = a;
        }
        if (max_amp < 0.001f) max_amp = 0.001f;
        float gain = 0.9f / max_amp;
        for (int k = 0; k < *out_n; k++)
            audio_out[k] *= gain;

        dmode_audio_available = 1;
    }
}

int sbitx_drm_audio_available(void)
{
    return dmode_audio_available;
}
