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

#include <string.h>
#include <errno.h>

#include "dbus_iface.h"
#include "dbus_iface_deep.h"
#include "streamplayer_dbus.h"
#include "streamer.h"
#include "urlfifo.h"
#include "messages.h"

static gboolean playback_start(tdbussplayPlayback *object,
                               GDBusMethodInvocation *invocation)
{
    msg_info("Got Playback.Start message");
    tdbus_splay_playback_complete_start(object, invocation);
    streamer_start();
    return TRUE;
}

static gboolean playback_stop(tdbussplayPlayback *object,
                              GDBusMethodInvocation *invocation)
{
    msg_info("Got Playback.Stop message");
    tdbus_splay_playback_complete_stop(object, invocation);
    streamer_stop();
    return TRUE;
}

static gboolean playback_pause(tdbussplayPlayback *object,
                               GDBusMethodInvocation *invocation)
{
    msg_info("Got Playback.Pause message");
    tdbus_splay_playback_complete_pause(object, invocation);
    streamer_pause();
    return TRUE;
}

static gboolean playback_seek(tdbussplayPlayback *object,
                              GDBusMethodInvocation *invocation,
                              gint64 position, const gchar *position_units)
{
    msg_info("Got Playback.Seek message");

    if(streamer_seek(position, position_units))
        tdbus_splay_playback_complete_seek(object, invocation);
    else
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                              G_DBUS_ERROR_FAILED,
                                              "Seek failed");

    return TRUE;
}

static gboolean fifo_clear(tdbussplayURLFIFO *object,
                           GDBusMethodInvocation *invocation,
                           gint16 keep_first_n_entries)
{
    msg_info("Got URLFIFO.Clear message");
    tdbus_splay_urlfifo_complete_clear(object, invocation);

    if(keep_first_n_entries >= 0)
        urlfifo_clear(keep_first_n_entries, NULL);

    return TRUE;
}

static gboolean fifo_next(tdbussplayURLFIFO *object,
                          GDBusMethodInvocation *invocation)
{
    msg_info("Got URLFIFO.Next message");
    tdbus_splay_urlfifo_complete_next(object, invocation);
    streamer_next(false);
    return TRUE;
}

static gboolean fifo_push(tdbussplayURLFIFO *object,
                          GDBusMethodInvocation *invocation,
                          guint16 stream_id, const gchar *stream_url,
                          gint64 start_position, const gchar *start_units,
                          gint64 stop_position, const gchar *stop_units,
                          gint16 keep_first_n_entries)
{
    msg_info("Got URLFIFO.Push message %u \"%s\", keep %d", stream_id, stream_url, keep_first_n_entries);

    const size_t keep =
        (keep_first_n_entries < 0)
        ? ((keep_first_n_entries == -2)
           ? 0
           : SIZE_MAX)
        : (size_t)keep_first_n_entries;
    const bool failed =
        (urlfifo_push_item(stream_id, stream_url, NULL, NULL,
                           keep, NULL,
                           NULL, &streamer_urlfifo_item_data_ops) == 0);

    const gboolean is_playing = (keep_first_n_entries == -2)
        ? streamer_next(true)
        : streamer_is_playing();

    tdbus_splay_urlfifo_complete_push(object, invocation, failed, is_playing);

    msg_info("Have %zu FIFO entries", urlfifo_get_size());

    return TRUE;
}

struct dbus_data
{
    guint owner_id;
    int acquired;
    tdbussplayPlayback *playback_iface;
    tdbussplayURLFIFO *urlfifo_iface;
};

static struct dbus_data dbus_data;

static void try_export_iface(GDBusConnection *connection,
                             GDBusInterfaceSkeleton *iface)
{
    GError *error = NULL;

    g_dbus_interface_skeleton_export(iface, connection, "/de/tahifi/Streamplayer", &error);

    if(error)
    {
        msg_error(0, LOG_EMERG, "%s", error->message);
        g_error_free(error);
    }
}

static void bus_acquired(GDBusConnection *connection,
                         const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    data->playback_iface = tdbus_splay_playback_skeleton_new();
    data->urlfifo_iface = tdbus_splay_urlfifo_skeleton_new();

    g_signal_connect(data->playback_iface, "handle-start",
                     G_CALLBACK(playback_start), NULL);
    g_signal_connect(data->playback_iface, "handle-stop",
                     G_CALLBACK(playback_stop), NULL);
    g_signal_connect(data->playback_iface, "handle-pause",
                     G_CALLBACK(playback_pause), NULL);
    g_signal_connect(data->playback_iface, "handle-seek",
                     G_CALLBACK(playback_seek), NULL);

    g_signal_connect(data->urlfifo_iface, "handle-clear",
                     G_CALLBACK(fifo_clear), NULL);
    g_signal_connect(data->urlfifo_iface, "handle-next",
                     G_CALLBACK(fifo_next), NULL);
    g_signal_connect(data->urlfifo_iface, "handle-push",
                     G_CALLBACK(fifo_push), NULL);

    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->playback_iface));
    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->urlfifo_iface));
}

static void name_acquired(GDBusConnection *connection,
                          const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_info("D-Bus name \"%s\" acquired", name);
    data->acquired = 1;
}

static void name_lost(GDBusConnection *connection,
                      const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_info("D-Bus name \"%s\" lost", name);
    data->acquired = -1;
}

static void destroy_notification(gpointer data)
{
    msg_info("Bus destroyed.");
}

int dbus_setup(GMainLoop *loop, bool connect_to_session_bus)
{
    memset(&dbus_data, 0, sizeof(dbus_data));

    GBusType bus_type =
        connect_to_session_bus ? G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM;

    static const char bus_name[] = "de.tahifi.Streamplayer";

    dbus_data.owner_id =
        g_bus_own_name(bus_type, bus_name, G_BUS_NAME_OWNER_FLAGS_NONE,
                       bus_acquired, name_acquired, name_lost, &dbus_data,
                       destroy_notification);

    while(dbus_data.acquired == 0)
    {
        /* do whatever has to be done behind the scenes until one of the
         * guaranteed callbacks gets called */
        g_main_context_iteration(NULL, TRUE);
    }

    if(dbus_data.acquired < 0)
    {
        msg_error(EPIPE, LOG_EMERG, "Failed acquiring D-Bus name");
        return -1;
    }

    log_assert(dbus_data.playback_iface != NULL);
    log_assert(dbus_data.urlfifo_iface != NULL);

    g_main_loop_ref(loop);

    return 0;
}

void dbus_shutdown(GMainLoop *loop)
{
    if(loop == NULL)
        return;

    g_bus_unown_name(dbus_data.owner_id);

    g_main_loop_unref(loop);
    g_object_unref(dbus_data.playback_iface);
    g_object_unref(dbus_data.urlfifo_iface);
}

tdbussplayURLFIFO *dbus_get_urlfifo_iface(void)
{
    return dbus_data.urlfifo_iface;
}

tdbussplayPlayback *dbus_get_playback_iface(void)
{
    return dbus_data.playback_iface;
}
