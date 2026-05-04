/* hermes-radio-daemon - embedded sBitx bootstrap
 *
 * Copyright (C) 2024-2025 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "radio.h"
#include "sbitx_bootstrap.h"

extern int sbitx_main(int argc, char *argv[]);
extern void sbitx_cfg_set_runtime_paths(const char *cfg_core,
                                        const char *cfg_user,
                                        const char *web_path);

static void derive_web_path(const char *cfg_radio_path,
                            char *web_path,
                            size_t web_path_len)
{
    const char *cfg_path = cfg_radio_path;
    const char *slash;

    if (!cfg_path || cfg_path[0] == '\0')
        cfg_path = CFG_RADIO_PATH;

    slash = strrchr(cfg_path, '/');
    if (!slash)
        snprintf(web_path, web_path_len, "%s", "web");
    else if (slash == cfg_path)
        snprintf(web_path, web_path_len, "%s", "/web");
    else
        snprintf(web_path, web_path_len, "%.*s/web", (int) (slash - cfg_path), cfg_path);
}

int sbitx_bootstrap(const char *cfg_radio_path,
                    const char *cfg_user_path,
                    bool cpu_arg_provided,
                    int cpu_nr)
{
    const char *radio_path = cfg_radio_path ? cfg_radio_path : CFG_RADIO_PATH;
    const char *user_path = cfg_user_path ? cfg_user_path : CFG_USER_PATH;
    char cpu_arg[32];
    char web_path[CONFIG_PATH_MAX];
    char *argv_buf[4];
    int arg_count = 0;

    derive_web_path(radio_path, web_path, sizeof(web_path));
    sbitx_cfg_set_runtime_paths(radio_path, user_path, web_path);

    fprintf(stderr,
            "radio_daemon: hfsignals backend bootstrapping embedded controller "
            "radio=%s user=%s web=%s\n",
            radio_path, user_path, web_path);

    argv_buf[arg_count++] = (char *) "sbitx_controller";

    if (cpu_arg_provided)
    {
        snprintf(cpu_arg, sizeof(cpu_arg), "%d", cpu_nr);
        argv_buf[arg_count++] = (char *) "-c";
        argv_buf[arg_count++] = cpu_arg;
    }

    argv_buf[arg_count] = NULL;

    optind = 1;
    return sbitx_main(arg_count, argv_buf);
}
