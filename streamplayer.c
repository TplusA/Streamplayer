/*
 * Copyright (C) 2015  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of T+A Streamplayer.
 *
 * T+A Streamplayer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3 as
 * published by the Free Software Foundation.
 *
 * T+A Streamplayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with T+A Streamplayer.  If not, see <http://www.gnu.org/licenses/>.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <glib-object.h>
#include <glib-unix.h>
#include <gst/gst.h>

#include "streamer.h"
#include "dbus_iface.h"
#include "messages.h"

static struct
{
    GMainLoop *loop;
}
globals;

struct parameters
{
    bool run_in_foreground;
};

/*!
 * Set up logging, daemonize.
 */
static int setup(const struct parameters *parameters)
{
    msg_enable_syslog(!parameters->run_in_foreground);

    if(!parameters->run_in_foreground)
        openlog("streamplayer", LOG_PID, LOG_DAEMON);

    if(!parameters->run_in_foreground)
    {
        if(daemon(0, 0) < 0)
        {
            msg_error(errno, LOG_EMERG, "Failed to run as daemon");
            return -1;
        }
    }

    return 0;
}

static void usage(const char *program_name)
{
    printf("Usage: %s [options]\n"
           "\n"
           "Options:\n"
           "  --help         Show this help.\n"
           "  --fg           Run in foreground, don't run as daemon.\n",
           program_name);
}

static int process_command_line(int argc, char *argv[],
                                struct parameters *parameters)
{
    GOptionContext *ctx = g_option_context_new("- T+A Streamplayer");
    GOptionEntry entries[] =
    {
        { "fg", 'f', 0, G_OPTION_ARG_NONE, &parameters->run_in_foreground,
          "Run in foreground, don't run as daemon.", NULL },
        { NULL }
    };

    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    GError *err = NULL;

    if(!g_option_context_parse(ctx, &argc, &argv, &err))
    {
        msg_error(0, LOG_EMERG, "%s", err->message);
        g_error_free(err);
        return -1;
    }

    return 0;
}

static gboolean signal_handler(gpointer user_data)
{
    g_main_loop_quit(user_data);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[])
{
#if !GLIB_CHECK_VERSION(2, 36, 0)
    g_type_init();
#endif

    gst_init(&argc, &argv);

    static struct parameters parameters;

    int ret = process_command_line(argc, argv, &parameters);

    if(ret == -1)
        return EXIT_FAILURE;
    else if(ret == 1)
    {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }

    if(setup(&parameters) < 0)
        return EXIT_FAILURE;

    globals.loop = g_main_loop_new(NULL, FALSE);
    if(globals.loop == NULL)
    {
        msg_error(ENOMEM, LOG_EMERG, "Failed creating GLib main loop");
        return -1;
    }

    if(streamer_setup(globals.loop) < 0)
        return EXIT_FAILURE;

    if(dbus_setup(globals.loop, true) < 0)
    {
        streamer_shutdown(globals.loop);
        return EXIT_FAILURE;
    }

    g_unix_signal_add(SIGINT, signal_handler, globals.loop);
    g_unix_signal_add(SIGTERM, signal_handler, globals.loop);

    g_main_loop_run(globals.loop);

    msg_info("Shutting down");

    dbus_shutdown(globals.loop);
    streamer_shutdown(globals.loop);

    g_main_loop_unref(globals.loop);

    return EXIT_SUCCESS;
}
