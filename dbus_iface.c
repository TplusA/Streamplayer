/*
 * Copyright (C) 2015, 2016, 2017  T+A elektroakustik GmbH & Co. KG
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
#include "artcache_dbus.h"
#include "streamer.h"
#include "urlfifo.h"
#include "messages.h"
#include "messages_dbus.h"

static void enter_urlfifo_handler(GDBusMethodInvocation *invocation)
{
    static const char iface_name[] = "de.tahifi.Streamplayer.URLFIFO";

    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s method invocation from '%s': %s",
              iface_name, g_dbus_method_invocation_get_sender(invocation),
              g_dbus_method_invocation_get_method_name(invocation));
}

static void enter_playback_handler(GDBusMethodInvocation *invocation)
{
    static const char iface_name[] = "de.tahifi.Streamplayer.Playback";

    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s method invocation from '%s': %s",
              iface_name, g_dbus_method_invocation_get_sender(invocation),
              g_dbus_method_invocation_get_method_name(invocation));
}

static void enter_audiopath_player_handler(GDBusMethodInvocation *invocation)
{
    static const char iface_name[] = "de.tahifi.AudioPath.Player";

    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s method invocation from '%s': %s",
              iface_name, g_dbus_method_invocation_get_sender(invocation),
              g_dbus_method_invocation_get_method_name(invocation));
}

static gboolean playback_start(tdbussplayPlayback *object,
                               GDBusMethodInvocation *invocation)
{
    enter_playback_handler(invocation);

    tdbus_splay_playback_complete_start(object, invocation);
    streamer_start();

    return TRUE;
}

static gboolean playback_stop(tdbussplayPlayback *object,
                              GDBusMethodInvocation *invocation)
{
    enter_playback_handler(invocation);

    tdbus_splay_playback_complete_stop(object, invocation);
    streamer_stop();

    return TRUE;
}

static gboolean playback_pause(tdbussplayPlayback *object,
                               GDBusMethodInvocation *invocation)
{
    enter_playback_handler(invocation);

    tdbus_splay_playback_complete_pause(object, invocation);
    streamer_pause();

    return TRUE;
}

static gboolean playback_seek(tdbussplayPlayback *object,
                              GDBusMethodInvocation *invocation,
                              gint64 position, const gchar *position_units)
{
    enter_playback_handler(invocation);

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
    enter_urlfifo_handler(invocation);

    uint16_t temp;
    const uint32_t current_id =
        streamer_get_current_stream_id(&temp) ? temp : UINT32_MAX;

    uint16_t ids_removed[URLFIFO_MAX_LENGTH];
    const size_t ids_removed_count =
        (keep_first_n_entries >= 0)
        ? urlfifo_clear(keep_first_n_entries, ids_removed)
        : 0;

    uint16_t ids_in_fifo[URLFIFO_MAX_LENGTH];
    const size_t ids_in_fifo_count = urlfifo_get_queued_ids(ids_in_fifo);

    GVariant *const queued_ids =
        g_variant_new_fixed_array(G_VARIANT_TYPE_UINT16, ids_in_fifo,
                                  ids_in_fifo_count, sizeof(ids_in_fifo[0]));
    GVariant *const removed_ids =
        g_variant_new_fixed_array(G_VARIANT_TYPE_UINT16, ids_removed,
                                  ids_removed_count, sizeof(ids_removed[0]));

    tdbus_splay_urlfifo_complete_clear(object, invocation,
                                       current_id, queued_ids, removed_ids);

    return TRUE;
}

static gboolean fifo_next(tdbussplayURLFIFO *object,
                          GDBusMethodInvocation *invocation)
{
    enter_urlfifo_handler(invocation);

    uint32_t skipped_id;
    uint32_t next_id;
    const enum PlayStatus play_status = streamer_next(false, &skipped_id, &next_id);

    uint16_t temp;
        streamer_get_current_stream_id(&temp) ? temp : UINT32_MAX;

    tdbus_splay_urlfifo_complete_next(object, invocation,
                                      skipped_id, next_id, (uint8_t)play_status);

    return TRUE;
}

static gboolean fifo_push(tdbussplayURLFIFO *object,
                          GDBusMethodInvocation *invocation,
                          guint16 stream_id, const gchar *stream_url,
                          GVariant *stream_key,
                          gint64 start_position, const gchar *start_units,
                          gint64 stop_position, const gchar *stop_units,
                          gint16 keep_first_n_entries)
{
    enter_urlfifo_handler(invocation);

    msg_info("Received stream %u \"%s\", keep %d",
             stream_id, stream_url, keep_first_n_entries);

    const size_t keep =
        (keep_first_n_entries < 0)
        ? ((keep_first_n_entries == -2)
           ? 0
           : SIZE_MAX)
        : (size_t)keep_first_n_entries;
    const bool failed =
        !streamer_push_item(stream_id, stream_key, stream_url, keep);

    const gboolean is_playing = (keep_first_n_entries == -2)
        ? streamer_next(true, NULL, NULL)
        : streamer_is_playing();

    tdbus_splay_urlfifo_complete_push(object, invocation, failed, is_playing);

    msg_vinfo(MESSAGE_LEVEL_DEBUG, "Have %zu FIFO entries", urlfifo_get_size());

    return TRUE;
}

static gboolean audiopath_player_activate(tdbusaupathPlayer *object,
                                          GDBusMethodInvocation *invocation)
{
    enter_audiopath_player_handler(invocation);

    streamer_activate();
    tdbus_aupath_player_complete_activate(object, invocation);

    return TRUE;
}

static gboolean audiopath_player_deactivate(tdbusaupathPlayer *object,
                                            GDBusMethodInvocation *invocation)
{
    enter_audiopath_player_handler(invocation);

    streamer_deactivate();
    tdbus_aupath_player_complete_deactivate(object, invocation);

    return TRUE;
}

bool dbus_handle_error(GError **error)
{
    if(*error == NULL)
        return true;

    if((*error)->message != NULL)
        msg_error(0, LOG_EMERG, "Got D-Bus error: %s", (*error)->message);
    else
        msg_error(0, LOG_EMERG, "Got D-Bus error without any message");

    g_error_free(*error);
    *error = NULL;

    return false;
}

struct dbus_data
{
    guint owner_id;
    int acquired;
    tdbussplayPlayback *playback_iface;
    tdbussplayURLFIFO *urlfifo_iface;

    tdbusartcacheWrite *artcache_write_iface;

    tdbusaupathPlayer *audiopath_player_iface;
    tdbusaupathManager *audiopath_manager_proxy;

    tdbusdebugLogging *debug_logging_iface;
    tdbusdebugLoggingConfig *debug_logging_config_proxy;
};

static struct dbus_data dbus_data;

static void try_export_iface(GDBusConnection *connection,
                             GDBusInterfaceSkeleton *iface)
{
    GError *error = NULL;

    g_dbus_interface_skeleton_export(iface, connection, "/de/tahifi/Streamplayer", &error);

    dbus_handle_error(&error);
}

static void bus_acquired(GDBusConnection *connection,
                         const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    data->playback_iface = tdbus_splay_playback_skeleton_new();
    data->urlfifo_iface = tdbus_splay_urlfifo_skeleton_new();
    data->audiopath_player_iface = tdbus_aupath_player_skeleton_new();
    data->debug_logging_iface = tdbus_debug_logging_skeleton_new();

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

    g_signal_connect(data->audiopath_player_iface, "handle-activate",
                     G_CALLBACK(audiopath_player_activate), NULL);
    g_signal_connect(data->audiopath_player_iface, "handle-deactivate",
                     G_CALLBACK(audiopath_player_deactivate), NULL);

    g_signal_connect(data->debug_logging_iface,
                     "handle-debug-level",
                     G_CALLBACK(msg_dbus_handle_debug_level), NULL);

    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->playback_iface));
    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->urlfifo_iface));
    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->audiopath_player_iface));
    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->debug_logging_iface));
}

static void created_config_proxy(GObject *source_object, GAsyncResult *res,
                                 gpointer user_data)
{
    struct dbus_data *data = user_data;
    GError *error = NULL;

    data->debug_logging_config_proxy =
        tdbus_debug_logging_config_proxy_new_finish(res, &error);

    if(dbus_handle_error(&error))
        g_signal_connect(dbus_data.debug_logging_config_proxy, "g-signal",
                         G_CALLBACK(msg_dbus_handle_global_debug_level_changed),
                         NULL);
}

static void name_acquired(GDBusConnection *connection,
                          const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "D-Bus name \"%s\" acquired", name);
    data->acquired = 1;

    GError *error = NULL;

    data->artcache_write_iface =
        tdbus_artcache_write_proxy_new_sync(connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "de.tahifi.TACAMan",
                                            "/de/tahifi/TACAMan",
                                            NULL, &error);
    dbus_handle_error(&error);

    data->audiopath_manager_proxy =
        tdbus_aupath_manager_proxy_new_sync(connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "de.tahifi.TAPSwitch",
                                            "/de/tahifi/TAPSwitch",
                                            NULL, &error);
    dbus_handle_error(&error);

    data->debug_logging_config_proxy = NULL;
    tdbus_debug_logging_config_proxy_new(connection,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         "de.tahifi.Dcpd", "/de/tahifi/Dcpd",
                                         NULL, created_config_proxy, data);
}

static void name_lost(GDBusConnection *connection,
                      const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "D-Bus name \"%s\" lost", name);
    data->acquired = -1;
}

static void destroy_notification(gpointer data)
{
    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Bus destroyed.");
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
    log_assert(dbus_data.artcache_write_iface != NULL);
    log_assert(dbus_data.audiopath_player_iface != NULL);
    log_assert(dbus_data.audiopath_manager_proxy != NULL);
    log_assert(dbus_data.debug_logging_iface != NULL);

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
    g_object_unref(dbus_data.artcache_write_iface);
    g_object_unref(dbus_data.audiopath_manager_proxy);
    g_object_unref(dbus_data.audiopath_player_iface);
    g_object_unref(dbus_data.debug_logging_iface);

    if(dbus_data.debug_logging_config_proxy != NULL)
        g_object_unref(dbus_data.debug_logging_config_proxy);
}

tdbussplayURLFIFO *dbus_get_urlfifo_iface(void)
{
    return dbus_data.urlfifo_iface;
}

tdbussplayPlayback *dbus_get_playback_iface(void)
{
    return dbus_data.playback_iface;
}

tdbusartcacheWrite *dbus_artcache_get_write_iface(void)
{
    return dbus_data.artcache_write_iface;
}

tdbusaupathManager *dbus_audiopath_get_manager_iface(void)
{
    return dbus_data.audiopath_manager_proxy;
}
