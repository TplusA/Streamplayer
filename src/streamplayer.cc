/*
 * Copyright (C) 2015--2018, 2020--2022  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of T+A Streamplayer.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
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

#include "streamer.hh"
#include "dbus_iface.hh"
#include "dbus_iface_deep.hh"
#include "messages.h"
#include "messages_glib.h"
#include "gstringwrapper.hh"
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
    gboolean run_in_foreground;
    gboolean connect_to_system_dbus;
    gint soup_http_blocksize_kb;
    gint alsa_latency_ms;
    gint alsa_buffer_ms;
};

static void show_version_info(void)
{
    gchar *temp = gst_version_string();
    printf("%s\n"
           "Revision %s%s\n"
           "         %s+%d, %s\n"
           "%s\n"
           "GLib %u.%u.%u\n",
           PACKAGE_STRING,
           VCS_FULL_HASH, VCS_WC_MODIFIED ? " (tainted)" : "",
           VCS_TAG, VCS_TICK, VCS_DATE, temp,
           glib_major_version, glib_minor_version, glib_micro_version);
    g_free(temp);
}

static void log_version_info(void)
{
    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Rev %s%s, %s+%d, %s",
              VCS_FULL_HASH, VCS_WC_MODIFIED ? " (tainted)" : "",
              VCS_TAG, VCS_TICK, VCS_DATE);

    gchar *temp = gst_version_string();
    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "%s", temp);
    g_free(temp);

    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "GLib %u.%u.%u",
              glib_major_version, glib_minor_version, glib_micro_version);
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
    {
        openlog("streamplayer", LOG_PID, LOG_DAEMON);

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
    parameters->run_in_foreground = FALSE;
    parameters->connect_to_system_dbus = FALSE;
    parameters->soup_http_blocksize_kb = 0;
    parameters->alsa_latency_ms = 0;
    parameters->alsa_buffer_ms = 0;

    static bool show_version = false;
    gchar *verbose_level_name_raw = nullptr;
    static bool verbose_quiet = false;

    GOptionContext *ctx = g_option_context_new("- T+A Streamplayer");
    GOptionEntry entries[] =
    {
        { "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
          "Print version information to stdout.", nullptr },
        { "fg", 'f', 0, G_OPTION_ARG_NONE, &parameters->run_in_foreground,
          "Run in foreground, don't run as daemon.", nullptr },
        { "verbose", 'v', 0, G_OPTION_ARG_STRING, &verbose_level_name_raw,
          "Set verbosity level to given level.", nullptr },
        { "quiet", 'q', 0, G_OPTION_ARG_NONE, &verbose_quiet,
          "Short for \"--verbose quite\".", nullptr},
        { "system-dbus", 's', 0, G_OPTION_ARG_NONE,
          &parameters->connect_to_system_dbus,
          "Connect to system D-Bus instead of session D-Bus.", nullptr },
        { "soup-blocksize", 0, 0,
          G_OPTION_ARG_INT, &parameters->soup_http_blocksize_kb,
          "Block size in kiB for GstSoupHTTPSrc elements", nullptr },
        { "alsa-latency", 0, 0,
          G_OPTION_ARG_INT, &parameters->alsa_latency_ms,
          "ALSA latency in ms", nullptr },
        { "alsa-buffer", 0, 0,
          G_OPTION_ARG_INT, &parameters->alsa_buffer_ms,
          "ALSA buffer size in ms", nullptr },
        {}
    };

    g_option_context_add_main_entries(ctx, entries, nullptr);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    GError *err = nullptr;

    if(!g_option_context_parse(ctx, &argc, &argv, &err))
    {
        g_option_context_free(ctx);
        msg_error(0, LOG_EMERG, "%s", err->message);
        g_error_free(err);
        return -1;
    }

    g_option_context_free(ctx);

    GLibString verbose_level_name(std::move(verbose_level_name_raw));

    if(show_version)
        return 1;

    if(verbose_level_name != nullptr)
    {
        parameters->verbose_level =
            msg_verbose_level_name_to_level(verbose_level_name.get());

        if(parameters->verbose_level == MESSAGE_LEVEL_IMPOSSIBLE)
        {
            fprintf(stderr,
                    "Invalid verbosity \"%s\". "
                    "Valid verbosity levels are:\n", verbose_level_name.get());

            const char *const *names = msg_get_verbose_level_names();

            for(const char *name = *names; name != nullptr; name = *++names)
                fprintf(stderr, "    %s\n", name);

            return -1;
        }
    }

    if(parameters->soup_http_blocksize_kb < 0)
    {
        fprintf(stderr, "Invalid block size %d.\n",
                parameters->soup_http_blocksize_kb);
        return -1;
    }

    if(parameters->alsa_latency_ms < 0)
    {
        fprintf(stderr, "Invalid ALSA latency %d.\n",
                parameters->alsa_latency_ms);
        return -1;
    }

    if(parameters->alsa_buffer_ms < 0)
    {
        fprintf(stderr, "Invalid ALSA buffer size %d.\n",
                parameters->alsa_buffer_ms);
        return -1;
    }

    if(verbose_quiet)
        parameters->verbose_level = MESSAGE_LEVEL_QUIET;

    return 0;
}

static void show_buffer_parameter(const char *what, const char *fmt, gint value)
{
    if(value > 0)
        msg_vinfo(MESSAGE_LEVEL_NORMAL, fmt, what, value);
    else
        msg_vinfo(MESSAGE_LEVEL_NORMAL, "%s: default", what);
}

static gboolean signal_handler(gpointer user_data)
{
    g_main_loop_quit(static_cast<GMainLoop *>(user_data));
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

    globals.loop = g_main_loop_new(nullptr, FALSE);
    if(globals.loop == nullptr)
    {
        msg_error(ENOMEM, LOG_EMERG, "Failed creating GLib main loop");
        return -1;
    }

    if(parameters.soup_http_blocksize_kb <= 0 &&
       parameters.alsa_latency_ms <= 0 && parameters.alsa_buffer_ms <= 0)
        msg_vinfo(MESSAGE_LEVEL_NORMAL,
                  "Using GStreamer default buffer settings");
    else
    {
        show_buffer_parameter("SOUP block size ", "%s: %d kiB", parameters.soup_http_blocksize_kb);
        show_buffer_parameter("ALSA latency    ", "%s: %d ms", parameters.alsa_latency_ms);
        show_buffer_parameter("ALSA buffer size", "%s: %d ms", parameters.alsa_buffer_ms);
    }

    if(Streamer::setup(globals.loop,
                       parameters.soup_http_blocksize_kb * 1024U,
                       parameters.alsa_latency_ms * 1000U,
                       parameters.alsa_buffer_ms * 1000U) < 0)
        return EXIT_FAILURE;

    if(dbus_setup(globals.loop, !parameters.connect_to_system_dbus) < 0)
    {
        Streamer::shutdown(globals.loop);
        return EXIT_FAILURE;
    }

    tdbus_aupath_manager_call_register_player(dbus_audiopath_get_manager_iface(),
                                              "strbo",
                                              "T+A Streaming Board streamplayer",
                                              "/de/tahifi/Streamplayer",
                                              nullptr, nullptr, nullptr);

    g_unix_signal_add(SIGINT, signal_handler, globals.loop);
    g_unix_signal_add(SIGTERM, signal_handler, globals.loop);

    g_main_loop_run(globals.loop);

    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Shutting down");

    dbus_shutdown(globals.loop);
    Streamer::shutdown(globals.loop);

    g_main_loop_unref(globals.loop);

    return EXIT_SUCCESS;
}
