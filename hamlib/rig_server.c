/* hermes-radio-daemon - rigctld-compatible TCP server
 *
 * Listens on configurable port (default 4532), accepts TCP connections,
 * speaks the rigctld text protocol so hamlib-compatible software can
 * control the sBitx/zBitx through its standard hamlib API.
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "radio.h"
#include "radio_backend.h"
#include "rig_server.h"

#define RIG_SERVER_DEFAULT_PORT  4532
#define RIG_SERVER_MAX_CLIENTS   8
#define RIG_SERVER_BUF_SIZE      1024

typedef struct {
    int  fd;
    char buf[RIG_SERVER_BUF_SIZE];
    int  buf_pos;
} rig_client;

static radio     *rig_radio       = NULL;
static int        rig_listen_fd   = -1;
static pthread_t  rig_thread;
static bool       rig_running     = false;
static bool       rig_thread_started = false;

static void rig_respond(int fd, const char *fmt, ...)
{
    char buf[RIG_SERVER_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        ssize_t w = write(fd, buf, (size_t) n);
        (void) w;
    }
}

/* Map our internal MODE_* (matching radio.h) to a Hamlib mode name. */
static const char *mode_to_hamlib(uint16_t mode)
{
    switch (mode) {
    case MODE_LSB:  return "LSB";
    case MODE_USB:  return "USB";
    case MODE_CW:   return "CW";
    case MODE_FM:   return "FM";
    case MODE_AM:   return "AM";
    case MODE_DRM:  return "USB";   /* DRM has no hamlib equivalent; rig sits in USB */
    case MODE_FT8:  return "PKTUSB"; /* FT8 reported as data-USB so digital apps pick it up */
    case MODE_RTTY: return "RTTY";
    default:        return "USB";
    }
}

static uint16_t hamlib_to_mode(const char *s)
{
    if (!strcmp(s, "LSB")  || !strcmp(s, "PKTLSB") || !strcmp(s, "DIGL")) return MODE_LSB;
    if (!strcmp(s, "USB")  || !strcmp(s, "PKTUSB") || !strcmp(s, "DIGU")) return MODE_USB;
    if (!strcmp(s, "CW")   || !strcmp(s, "CWR"))  return MODE_CW;
    if (!strcmp(s, "FM")   || !strcmp(s, "FMN"))  return MODE_FM;
    if (!strcmp(s, "AM")   || !strcmp(s, "AMS"))  return MODE_AM;
    if (!strcmp(s, "RTTY") || !strcmp(s, "RTTYR")) return MODE_RTTY;
    return MODE_USB;
}

/* Skip whitespace and any leading "VFOA"/"VFOB" / "Main"/"Sub" tokens
 * that hamlib clients may prepend to per-VFO commands. */
static const char *skip_vfo_arg(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, "VFOA", 4) || !strncmp(p, "VFOB", 4) ||
        !strncmp(p, "Main", 4) || !strncmp(p, "Sub",  3))
    {
        while (*p && *p != ' ' && *p != '\t') p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    return p;
}

/* --- get_level / set_level helpers ------------------------------------ */

/* The complete list of RIG_LEVEL_* tokens hamlib clients may query.
 * We answer the ones we have data for and return 0 for the rest so
 * the client doesn't see a parse error. */
static void handle_get_level(radio *radio_h, int fd, const char *arg)
{
    if (!arg || !*arg) { rig_respond(fd, "0\n"); return; }

    if (strstr(arg, "RFPOWER_METER_WATTS"))
    {
        /* Forward power in watts. radio_h->fwd_power is the raw ADC
         * reading; the hfsignals get_fwd_power op converts to W*10. */
        rig_respond(fd, "%.2f\n", radio_backend_get_fwd_power(radio_h) / 10.0);
        return;
    }
    if (strstr(arg, "RFPOWER_METER"))
    {
        /* 0..1 meter scale */
        uint32_t w10 = radio_backend_get_fwd_power(radio_h);
        double frac = w10 / 400.0; /* 40 W full scale */
        if (frac > 1.0) frac = 1.0;
        rig_respond(fd, "%.4f\n", frac);
        return;
    }
    if (strstr(arg, "RFPOWER"))
    {
        /* TX power knob, 0..1 */
        uint32_t pct = radio_h->profiles[radio_h->profile_active_idx].power_level_percentage;
        rig_respond(fd, "%.2f\n", pct / 100.0);
        return;
    }
    if (strstr(arg, "SWR"))
    {
        rig_respond(fd, "%.2f\n", radio_backend_get_swr(radio_h) / 10.0);
        return;
    }
    if (strstr(arg, "STRENGTH"))
    {
        /* S-meter, dB over S9. We don't compute it; report 0. */
        rig_respond(fd, "0\n");
        return;
    }
    if (strstr(arg, "AF"))
    {
        rig_respond(fd, "%.2f\n",
                    radio_h->profiles[radio_h->profile_active_idx].speaker_level / 100.0);
        return;
    }
    /* Unknown level — hamlib expects the value; we return 0 so the
     * client doesn't choke on a parse error. */
    rig_respond(fd, "0\n");
}

static void handle_set_level(radio *radio_h, int fd, const char *arg)
{
    if (!arg || !*arg) { rig_respond(fd, "RPRT -1\n"); return; }

    char name[32] = {0};
    double value = 0.0;
    if (sscanf(arg, "%31s %lf", name, &value) != 2)
    {
        rig_respond(fd, "RPRT -1\n");
        return;
    }

    if (!strcmp(name, "RFPOWER"))
    {
        if (value < 0) value = 0;
        if (value > 1) value = 1;
        radio_backend_set_power_level(radio_h, (uint16_t) (value * 100.0 + 0.5),
                                      radio_h->profile_active_idx);
        rig_respond(fd, "RPRT 0\n");
        return;
    }
    if (!strcmp(name, "AF"))
    {
        if (value < 0) value = 0;
        if (value > 1) value = 1;
        radio_backend_set_speaker_volume(radio_h, (uint32_t) (value * 100.0 + 0.5),
                                         radio_h->profile_active_idx);
        rig_respond(fd, "RPRT 0\n");
        return;
    }
    /* Other levels (PREAMP, ATT, AGC, ...) — accept and silently
     * ignore so clients don't error out. */
    rig_respond(fd, "RPRT 0\n");
}

/* --- main per-line dispatch ------------------------------------------ */

static void rig_handle_line(radio *radio_h, int fd, const char *line)
{
    char cmd[64] = {0};
    long val = 0;
    int n = 0;

    if (sscanf(line, "%63s%n", cmd, &n) != 1)
        return;

    /* Frequency get: f, \get_freq */
    if (!strcmp(cmd, "f") || !strcmp(cmd, "\\get_freq"))
    {
        uint32_t freq = radio_h->profiles[radio_h->profile_active_idx].freq;
        rig_respond(fd, "%u\n", freq);
        return;
    }

    /* Frequency set: F <hz>, \set_freq <hz> */
    if (!strcmp(cmd, "F") || !strcmp(cmd, "\\set_freq"))
    {
        const char *p = skip_vfo_arg(line + n);
        if (sscanf(p, "%ld", &val) == 1)
        {
            radio_backend_set_frequency(radio_h, (uint32_t) val,
                                         radio_h->profile_active_idx);
            rig_respond(fd, "RPRT 0\n");
        }
        else
        {
            rig_respond(fd, "RPRT -1\n");
        }
        return;
    }

    /* Mode get: m, \get_mode → "MODE\nBANDWIDTH\n" */
    if (!strcmp(cmd, "m") || !strcmp(cmd, "\\get_mode"))
    {
        uint16_t mode = radio_h->profiles[radio_h->profile_active_idx].mode;
        uint32_t bw   = radio_h->profiles[radio_h->profile_active_idx].bpf_high
                      - radio_h->profiles[radio_h->profile_active_idx].bpf_low;
        rig_respond(fd, "%s\n%u\n", mode_to_hamlib(mode), bw);
        return;
    }

    /* Mode set: M <MODE> <BW> */
    if (!strcmp(cmd, "M") || !strcmp(cmd, "\\set_mode"))
    {
        const char *p = skip_vfo_arg(line + n);
        char modestr[16] = {0};
        if (sscanf(p, "%15s", modestr) == 1)
        {
            radio_backend_set_mode(radio_h, hamlib_to_mode(modestr),
                                    radio_h->profile_active_idx);
            rig_respond(fd, "RPRT 0\n");
        }
        else
        {
            rig_respond(fd, "RPRT -1\n");
        }
        return;
    }

    /* PTT get / set: t / T */
    if (!strcmp(cmd, "t") || !strcmp(cmd, "\\get_ptt"))
    {
        rig_respond(fd, "%d\n", radio_h->txrx_state ? 1 : 0);
        return;
    }
    if (!strcmp(cmd, "T") || !strcmp(cmd, "\\set_ptt"))
    {
        const char *p = skip_vfo_arg(line + n);
        if (sscanf(p, "%ld", &val) == 1)
        {
            radio_backend_set_txrx_state(radio_h, val != 0);
            rig_respond(fd, "RPRT 0\n");
        }
        else
        {
            rig_respond(fd, "RPRT -1\n");
        }
        return;
    }

    /* VFO get: v, \get_vfo (single-VFO model) */
    if (!strcmp(cmd, "v") || !strcmp(cmd, "\\get_vfo"))
    {
        rig_respond(fd, "VFOA\n");
        return;
    }

    /* VFO set: V <VFO>  — accept and accept silently */
    if (!strcmp(cmd, "V") || !strcmp(cmd, "\\set_vfo"))
    {
        rig_respond(fd, "RPRT 0\n");
        return;
    }

    /* \chk_vfo */
    if (!strcmp(cmd, "\\chk_vfo"))
    {
        rig_respond(fd, "CHKVFO 1\n");
        return;
    }

    /* Get/set level: l <NAME>, L <NAME> <value> */
    if (!strcmp(cmd, "l") || !strcmp(cmd, "\\get_level"))
    {
        handle_get_level(radio_h, fd, skip_vfo_arg(line + n));
        return;
    }
    if (!strcmp(cmd, "L") || !strcmp(cmd, "\\set_level"))
    {
        handle_set_level(radio_h, fd, skip_vfo_arg(line + n));
        return;
    }

    /* Get/set squelch: s / S — we don't have a squelch knob, accept and
     * report 0 / OK so clients don't bail out. */
    if (!strcmp(cmd, "s") || !strcmp(cmd, "\\get_squelch"))
    {
        rig_respond(fd, "0\n");
        return;
    }
    if (!strcmp(cmd, "S") || !strcmp(cmd, "\\set_squelch"))
    {
        rig_respond(fd, "RPRT 0\n");
        return;
    }

    /* dump_state — capabilities probe used by WSJT-X / fldigi.
     * The TX range now mirrors RX so digital-mode apps don't refuse to
     * transmit. modes mask 0x2ef = CW|CWR|USB|LSB|AM|FM|RTTY|PKTUSB. */
    if (!strcmp(cmd, "\\dump_state") || !strcmp(cmd, "dump_state"))
    {
        rig_respond(fd,
            "0\n"     /* protocol version */
            "2\n"     /* model = NET rigctl */
            "1\n"     /* ITU region */
            /* RX freq ranges */
            "100000 30000000 0x2ef -1 -1 0x1 0x0\n"
            "0 0 0 0 0 0 0\n"
            /* TX freq ranges (matches RX so apps don't refuse to tx) */
            "100000 30000000 0x2ef 1000 100000 0x1 0x0\n"
            "0 0 0 0 0 0 0\n"
            /* tuning steps */
            "0x2ef 1\n"
            "0 0\n"
            /* filters */
            "0x82 500\n"   /* CW 500 Hz */
            "0x21 2700\n"  /* SSB 2700 Hz */
            "0x40 7000\n"  /* FM 7 kHz */
            "0x10 10000\n" /* AM 10 kHz */
            "0 0\n"
            "0\n"          /* max_rit */
            "0\n"          /* max_xit */
            "0\n"          /* max_ifshift */
            "0\n"          /* announces */
            "0 0 0 0 0\n"  /* preamp / attenuator / has-set-vfo etc. */
            "0x4400000\n"  /* has_get_level: RFPOWER_METER | SWR */
            "0x4400000\n"  /* has_set_level (subset) */
            "0\n"          /* has_get_parm */
            "0\n");        /* has_set_parm */
        return;
    }

    /* Quit */
    if (!strcmp(cmd, "q") || !strcmp(cmd, "Q") || !strcmp(cmd, "\\quit"))
    {
        return;  /* client will close */
    }

    /* Unknown command — rigctld convention is RPRT -11 (function not
     * available) for unrecognised commands rather than a generic -1. */
    rig_respond(fd, "RPRT -11\n");
}

/* --- per-client buffered read -------------------------------------- */

static int rig_handle_client(radio *radio_h, rig_client *cl)
{
    ssize_t n = read(cl->fd,
                     cl->buf + cl->buf_pos,
                     sizeof(cl->buf) - (size_t) cl->buf_pos - 1);
    if (n == 0) return -1;                    /* EOF */
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        return -1;
    }

    cl->buf_pos += (int) n;
    cl->buf[cl->buf_pos] = '\0';

    char *start = cl->buf;
    char *end;
    while ((end = strchr(start, '\n')) != NULL)
    {
        *end = '\0';
        /* trim trailing CR */
        if (end > start && *(end - 1) == '\r')
            *(end - 1) = '\0';
        rig_handle_line(radio_h, cl->fd, start);
        start = end + 1;
    }

    /* Move the partial line back to the start of the buffer. */
    cl->buf_pos = (int) (cl->buf + cl->buf_pos - start);
    if (cl->buf_pos > 0)
        memmove(cl->buf, start, (size_t) cl->buf_pos);
    cl->buf[cl->buf_pos] = '\0';

    /* If the buffer filled with no newline, drop it to avoid wedging. */
    if (cl->buf_pos == sizeof(cl->buf) - 1)
        cl->buf_pos = 0;

    return 0;
}

/* --- accept loop --------------------------------------------------- */

static void *rig_server_thread(void *arg)
{
    radio *radio_h = (radio *) arg;
    rig_client clients[RIG_SERVER_MAX_CLIENTS];
    int nclients = 0;

    for (int i = 0; i < RIG_SERVER_MAX_CLIENTS; i++)
        clients[i].fd = -1;

    while (rig_running)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(rig_listen_fd, &rfds);
        int maxfd = rig_listen_fd;

        for (int i = 0; i < nclients; i++)
        {
            FD_SET(clients[i].fd, &rfds);
            if (clients[i].fd > maxfd) maxfd = clients[i].fd;
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0)
        {
            if (errno == EINTR) continue;
            break;
        }

        /* Accept new connections */
        if (FD_ISSET(rig_listen_fd, &rfds) && nclients < RIG_SERVER_MAX_CLIENTS)
        {
            int cfd = accept(rig_listen_fd, NULL, NULL);
            if (cfd >= 0)
            {
                fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);
                clients[nclients].fd = cfd;
                clients[nclients].buf_pos = 0;
                clients[nclients].buf[0] = '\0';
                nclients++;
            }
        }

        /* Handle existing clients */
        for (int i = 0; i < nclients; i++)
        {
            if (!FD_ISSET(clients[i].fd, &rfds)) continue;
            int rc = rig_handle_client(radio_h, &clients[i]);
            if (rc < 0)
            {
                close(clients[i].fd);
                clients[i] = clients[--nclients];
                i--;
            }
        }
    }

    /* Cleanup */
    for (int i = 0; i < nclients; i++)
        close(clients[i].fd);

    return NULL;
}

bool rig_server_start(radio *radio_h)
{
    if (!radio_h || !radio_h->rig_server_enable)
        return false;

    int port = radio_h->rig_server_port > 0
               ? radio_h->rig_server_port : RIG_SERVER_DEFAULT_PORT;

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons((uint16_t) port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    rig_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (rig_listen_fd < 0) return false;

    int opt = 1;
    setsockopt(rig_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(rig_listen_fd, F_SETFL, fcntl(rig_listen_fd, F_GETFL, 0) | O_NONBLOCK);

    if (bind(rig_listen_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "rig_server: bind on port %d failed: %s\n",
                port, strerror(errno));
        close(rig_listen_fd);
        rig_listen_fd = -1;
        return false;
    }

    if (listen(rig_listen_fd, 5) < 0)
    {
        fprintf(stderr, "rig_server: listen failed: %s\n", strerror(errno));
        close(rig_listen_fd);
        rig_listen_fd = -1;
        return false;
    }

    rig_radio   = radio_h;
    rig_running = true;

    if (pthread_create(&rig_thread, NULL, rig_server_thread, radio_h) != 0)
    {
        fprintf(stderr, "rig_server: pthread_create failed\n");
        close(rig_listen_fd);
        rig_listen_fd = -1;
        rig_running = false;
        return false;
    }
    rig_thread_started = true;

    fprintf(stderr, "rig_server: listening on port %d\n", port);
    return true;
}

void rig_server_stop(void)
{
    rig_running = false;

    if (rig_listen_fd >= 0)
    {
        shutdown(rig_listen_fd, SHUT_RDWR);
        close(rig_listen_fd);
        rig_listen_fd = -1;
    }

    if (rig_thread_started)
    {
        pthread_join(rig_thread, NULL);
        rig_thread_started = false;
    }
}
