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
#define RIG_SERVER_BUF_SIZE      256

static radio     *rig_radio       = NULL;
static int        rig_listen_fd   = -1;
static pthread_t  rig_thread;
static bool       rig_running     = false;

static void rig_respond(int fd, const char *fmt, ...)
{
    char buf[RIG_SERVER_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) write(fd, buf, (size_t) n);
}

static const char *mode_to_hamlib(uint16_t mode)
{
    switch (mode) {
    case 0: return "LSB";
    case 1: return "USB";
    case 2: return "CW";
    case 3: return "FM";
    case 4: return "AM";
    case 6: return "USB";   /* FT8 uses USB */
    case 7: return "RTTY";
    default: return "USB";
    }
}

static uint16_t hamlib_to_mode(const char *s)
{
    if (!strcmp(s, "LSB"))  return 0;
    if (!strcmp(s, "USB"))  return 1;
    if (!strcmp(s, "CW"))   return 2;
    if (!strcmp(s, "CWR"))  return 2;
    if (!strcmp(s, "FM"))   return 3;
    if (!strcmp(s, "AM"))   return 4;
    if (!strcmp(s, "RTTY")) return 7;
    if (!strcmp(s, "PKTUSB") || !strcmp(s, "DIGU")) return 6; /* FT8 via USB */
    return 1; /* default USB */
}

static void rig_handle_line(radio *radio_h, int fd, const char *line)
{
    char cmd[64] = {0};
    long val = 0;
    int n = 0;

    sscanf(line, "%63s%n", cmd, &n);

    /* Frequency get */
    if (!strcmp(cmd, "f") || !strcmp(cmd, "\\get_freq"))
    {
        uint32_t freq = radio_h->profiles[radio_h->profile_active_idx].freq;
        rig_respond(fd, "%u\n", freq);
        return;
    }

    /* Frequency set */
    if (!strcmp(cmd, "F") || !strcmp(cmd, "\\set_freq"))
    {
        const char *p = line + n;
        while (*p == ' ' || *p == 'V') p++;
        if (sscanf(p, "%ld", &val) == 1)
        {
            radio_backend_set_frequency(radio_h, (uint32_t) val,
                                         radio_h->profile_active_idx);
            rig_respond(fd, "RPRT 0\n");
        }
        return;
    }

    /* Mode get */
    if (!strcmp(cmd, "m") || !strcmp(cmd, "\\get_mode"))
    {
        uint16_t mode = radio_h->profiles[radio_h->profile_active_idx].mode;
        uint16_t bw   = radio_h->profiles[radio_h->profile_active_idx].bpf_high
                      - radio_h->profiles[radio_h->profile_active_idx].bpf_low;
        rig_respond(fd, "%s\n%u\n", mode_to_hamlib(mode), bw);
        return;
    }

    /* Mode set */
    if (!strcmp(cmd, "M") || !strcmp(cmd, "\\set_mode"))
    {
        const char *p = line + n;
        char modestr[16] = {0};
        if (sscanf(p, "%15s", modestr) == 1)
        {
            radio_backend_set_mode(radio_h, hamlib_to_mode(modestr),
                                    radio_h->profile_active_idx);
            rig_respond(fd, "RPRT 0\n");
        }
        return;
    }

    /* PTT get */
    if (!strcmp(cmd, "t") || !strcmp(cmd, "\\get_ptt"))
    {
        rig_respond(fd, "%d\n", radio_h->txrx_state ? 1 : 0);
        return;
    }

    /* PTT set */
    if (!strcmp(cmd, "T") || !strcmp(cmd, "\\set_ptt"))
    {
        const char *p = line + n;
        if (sscanf(p, "%ld", &val) == 1)
        {
            radio_backend_set_txrx_state(radio_h, val != 0);
            rig_respond(fd, "RPRT 0\n");
        }
        return;
    }

    /* VFO get */
    if (!strcmp(cmd, "v") || !strcmp(cmd, "V"))
    {
        rig_respond(fd, "VFOA\n");
        return;
    }

    /* Check VFO */
    if (!strcmp(cmd, "\\chk_vfo"))
    {
        rig_respond(fd, "CHKVFO 1\n");
        return;
    }

    /* Dump state (static capabilities) */
    if (!strcmp(cmd, "\\dump_state") || !strcmp(cmd, "dump_state"))
    {
        rig_respond(fd,
            "0\n"     /* protocol version */
            "2\n"     /* model = NET rigctl */
            "1\n"     /* ITU region */
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"  /* filters */
            "100000 30000000 0 0 0 0\n"  /* RX range */
            "0 0 0 0 0 0\n"             /* TX range: none */
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"  /* tuning steps */
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
            "0x2ef 0 0 0\n"   /* modes: CW|CWR|USB|LSB|AM|FM|WFM|AMS */
            "500 5000 0 0 0 0\n"
            "2700 5000 0 0 0 0\n"
            "0 0 0 0\n"       /* levels available */
            "0\n"              /* parms available */
            "\n");
        return;
    }

    /* Squelch / levels */
    if (!strcmp(cmd, "s") || !strcmp(cmd, "\\get_level"))
    {
        rig_respond(fd, "0\n");
        return;
    }

    if (!strcmp(cmd, "l") || !strcmp(cmd, "\\get_level"))
    {
        const char *p = line + n;
        if (strstr(p, "STRENGTH"))
            rig_respond(fd, "%u\n", radio_backend_get_fwd_power(radio_h));
        else
            rig_respond(fd, "0\n");
        return;
    }

    /* Quit */
    if (!strcmp(cmd, "q"))
    {
        return;  /* client will close */
    }

    /* Unknown — respond with error */
    rig_respond(fd, "RPRT -1\n");
}

static int rig_handle_client(radio *radio_h, int fd)
{
    static char buf[RIG_SERVER_BUF_SIZE];
    static int  buf_pos = 0;

    ssize_t n = read(fd, buf + buf_pos, sizeof(buf) - buf_pos - 1);
    if (n <= 0) return -1;

    buf_pos += (int) n;
    buf[buf_pos] = '\0';

    char *start = buf;
    char *end;
    while ((end = strchr(start, '\n')) != NULL)
    {
        *end = '\0';
        rig_handle_line(radio_h, fd, start);
        start = end + 1;
    }

    buf_pos = (int) (buf + buf_pos - start);
    if (buf_pos > 0) memmove(buf, start, (size_t) buf_pos);
    buf[buf_pos] = '\0';

    return 0;
}

static void *rig_server_thread(void *arg)
{
    radio *radio_h = (radio *) arg;
    int clients[RIG_SERVER_MAX_CLIENTS];
    int nclients = 0;

    for (int i = 0; i < RIG_SERVER_MAX_CLIENTS; i++)
        clients[i] = -1;

    while (rig_running)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(rig_listen_fd, &rfds);
        int maxfd = rig_listen_fd;

        for (int i = 0; i < nclients; i++)
        {
            FD_SET(clients[i], &rfds);
            if (clients[i] > maxfd) maxfd = clients[i];
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) continue;

        /* Accept new connections */
        if (FD_ISSET(rig_listen_fd, &rfds) && nclients < RIG_SERVER_MAX_CLIENTS)
        {
            int cfd = accept(rig_listen_fd, NULL, NULL);
            if (cfd >= 0)
            {
                fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);
                clients[nclients++] = cfd;
            }
        }

        /* Handle existing clients */
        for (int i = 0; i < nclients; i++)
        {
            if (!FD_ISSET(clients[i], &rfds)) continue;
            int rc = rig_handle_client(radio_h, clients[i]);
            if (rc < 0)
            {
                close(clients[i]);
                clients[i] = clients[--nclients];
                i--;
            }
        }
    }

    /* Cleanup */
    for (int i = 0; i < nclients; i++)
        close(clients[i]);

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
        close(rig_listen_fd);
        return false;
    }

    if (listen(rig_listen_fd, 5) < 0)
    {
        close(rig_listen_fd);
        return false;
    }

    rig_radio   = radio_h;
    rig_running = true;

    if (pthread_create(&rig_thread, NULL, rig_server_thread, radio_h) != 0)
    {
        close(rig_listen_fd);
        rig_running = false;
        return false;
    }

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

    pthread_join(rig_thread, NULL);
}
