/*
 * Copyright (C) 2015, 2016  T+A elektroakustik GmbH & Co. KG
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
#include "messages_glib.h"
#include "versioninfo.h"

ssize_t (*os_read)(int fd, void *dest, size_t count) = read;
ssize_t (*os_write)(int fd, const void *buf, size_t count) = write;

static struct
{
    GMainLoop *loop;
}
globals;

struct parameters
{
    enum MessageVerboseLevel verbose_level;
    bool run_in_foreground;
    bool connect_to_system_dbus;
};

static void show_version_info(void)
{
    printf("%s\n"
           "Revision %s%s\n"
           "         %s+%d, %s\n",
           PACKAGE_STRING,
           VCS_FULL_HASH, VCS_WC_MODIFIED ? " (tainted)" : "",
           VCS_TAG, VCS_TICK, VCS_DATE);
}

static void log_version_info(void)
{
    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Rev %s%s, %s+%d, %s",
              VCS_FULL_HASH, VCS_WC_MODIFIED ? " (tainted)" : "",
              VCS_TAG, VCS_TICK, VCS_DATE);
}

/*!
 * Set up logging, daemonize.
 */
static int setup(const struct parameters *parameters)
{
    msg_enable_syslog(!parameters->run_in_foreground);
    msg_enable_glib_message_redirection();
    msg_set_verbose_level(parameters->verbose_level);

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

    log_version_info();

    return 0;
}

static int process_command_line(int argc, char *argv[],
                                struct parameters *parameters)
{
    parameters->verbose_level = MESSAGE_LEVEL_NORMAL;
    parameters->run_in_foreground = false;
    parameters->connect_to_system_dbus = false;

    bool show_version = false;
    char *verbose_level_name = NULL;
    bool verbose_quiet = false;

    GOptionContext *ctx = g_option_context_new("- T+A Streamplayer");
    GOptionEntry entries[] =
    {
        { "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
          "Print version information to stdout.", NULL },
        { "fg", 'f', 0, G_OPTION_ARG_NONE, &parameters->run_in_foreground,
          "Run in foreground, don't run as daemon.", NULL },
        { "verbose", 'v', 0, G_OPTION_ARG_STRING, &verbose_level_name,
          "Set verbosity level to given level.", NULL },
        { "quiet", 'q', 0, G_OPTION_ARG_NONE, &verbose_quiet,
          "Short for \"--verbose quite\".", NULL},
        { "system-dbus", 's', 0, G_OPTION_ARG_NONE,
          &parameters->connect_to_system_dbus,
          "Connect to system D-Bus instead of session D-Bus.", NULL },
        { NULL }
    };

    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    GError *err = NULL;

    if(!g_option_context_parse(ctx, &argc, &argv, &err))
    {
        g_option_context_free(ctx);
        msg_error(0, LOG_EMERG, "%s", err->message);
        g_error_free(err);
        return -1;
    }

    g_option_context_free(ctx);

    if(show_version)
        return 1;

    if(verbose_level_name != NULL)
    {
        parameters->verbose_level =
            msg_verbose_level_name_to_level(verbose_level_name);

        if(parameters->verbose_level == MESSAGE_LEVEL_IMPOSSIBLE)
        {
            fprintf(stderr,
                    "Invalid verbosity \"%s\". "
                    "Valid verbosity levels are:\n", verbose_level_name);

            const char *const *names = msg_get_verbose_level_names();

            for(const char *name = *names; name != NULL; name = *++names)
                fprintf(stderr, "    %s\n", name);
        }

        g_free(verbose_level_name);

        if(parameters->verbose_level == MESSAGE_LEVEL_IMPOSSIBLE)
            return -1;
    }

    if(verbose_quiet)
        parameters->verbose_level = MESSAGE_LEVEL_QUIET;

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
        show_version_info();
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

    static const guint soup_http_block_size = 32U * 1024U;

    if(streamer_setup(globals.loop, &soup_http_block_size) < 0)
        return EXIT_FAILURE;

    if(dbus_setup(globals.loop, !parameters.connect_to_system_dbus) < 0)
    {
        streamer_shutdown(globals.loop);
        return EXIT_FAILURE;
    }

    g_unix_signal_add(SIGINT, signal_handler, globals.loop);
    g_unix_signal_add(SIGTERM, signal_handler, globals.loop);

    g_main_loop_run(globals.loop);

    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Shutting down");

    dbus_shutdown(globals.loop);
    streamer_shutdown(globals.loop);

    g_main_loop_unref(globals.loop);

    return EXIT_SUCCESS;
}
