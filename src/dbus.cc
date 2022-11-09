/*
 * Copyright (C) 2015--2022  T+A elektroakustik GmbH & Co. KG
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

#include <cstring>
#include <cerrno>

#include "dbus.hh"
#include "de_tahifi_streamplayer.hh"
#include "de_tahifi_mounta.hh"
#include "de_tahifi_artcache.hh"
#include "de_tahifi_audiopath.hh"
#include "dbus/de_tahifi_debug.hh"
#include "streamer.hh"
#include "messages.h"
#include "messages_dbus.h"
#include "gerrorwrapper.hh"

namespace TDBus
{

template <>
Proxy<tdbusartcacheWrite> &get_singleton()
{
    static auto proxy(Proxy<tdbusartcacheWrite>::make_proxy(
                      "de.tahifi.TACAMan", "/de/tahifi/TACAMan"));
    return proxy;
}

template <>
Proxy<tdbusaupathManager> &get_singleton()
{
    static auto proxy(Proxy<tdbusaupathManager>::make_proxy(
                      "de.tahifi.TAPSwitch", "/de/tahifi/TAPSwitch"));
    return proxy;
}

template <>
Proxy<tdbusMounTA> &get_singleton()
{
    static auto proxy(Proxy<tdbusMounTA>::make_proxy(
                      "de.tahifi.MounTA", "/de/tahifi/MounTA"));
    return proxy;
}

template <>
Iface<tdbussplayURLFIFO> &get_exported_iface()
{
    static Iface<tdbussplayURLFIFO> iface("/de/tahifi/Streamplayer");
    return iface;
}

template <>
Iface<tdbussplayPlayback> &get_exported_iface()
{
    static Iface<tdbussplayPlayback> iface("/de/tahifi/Streamplayer");
    return iface;
}

}

/*
 * Logging levels, directly on /de/tahifi/Streamplayer and from DCPD via signal.
 */
static void debugging_and_logging(TDBus::Bus &bus)
{
    static TDBus::Iface<tdbusdebugLogging> logging_iface("/de/tahifi/Streamplayer");
    logging_iface.connect_default_method_handler<TDBus::DebugLoggingDebugLevel>();
    bus.add_auto_exported_interface(logging_iface);

    static auto logging_config_proxy(
        TDBus::Proxy<tdbusdebugLoggingConfig>::make_proxy("de.tahifi.Dcpd",
                                                          "/de/tahifi/Dcpd"));

    bus.add_watcher("de.tahifi.Dcpd",
        [] (GDBusConnection *connection, const char *name)
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Connecting to DCPD (debugging)");
            logging_config_proxy.connect_proxy(connection,
                [] (TDBus::Proxy<tdbusdebugLoggingConfig> &proxy, bool succeeded)
                {
                    if(succeeded)
                        proxy.connect_default_signal_handler<TDBus::DebugLoggingConfigGlobalDebugLevelChanged>();
                });
        },
        [] (GDBusConnection *connection, const char *name)
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Lost DCPD (debugging)");
        }
    );
}

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

static gboolean
playback_start(tdbussplayPlayback *object, GDBusMethodInvocation *invocation,
               const char *reason, gpointer)
{
    enter_playback_handler(invocation);

    tdbus_splay_playback_complete_start(object, invocation);
    Streamer::start(reason);

    return TRUE;
}

static gboolean
playback_stop(tdbussplayPlayback *object, GDBusMethodInvocation *invocation,
              const char *reason, gpointer)
{
    enter_playback_handler(invocation);

    tdbus_splay_playback_complete_stop(object, invocation);
    Streamer::stop(reason);

    return TRUE;
}

static gboolean
playback_pause(tdbussplayPlayback *object, GDBusMethodInvocation *invocation,
               const char *reason, gpointer)
{
    enter_playback_handler(invocation);

    tdbus_splay_playback_complete_pause(object, invocation);
    Streamer::pause(reason);

    return TRUE;
}

static gboolean
playback_seek(tdbussplayPlayback *object, GDBusMethodInvocation *invocation,
              gint64 position, const gchar *position_units,
              gpointer)
{
    enter_playback_handler(invocation);

    if(Streamer::seek(position, position_units))
        tdbus_splay_playback_complete_seek(object, invocation);
    else
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                              G_DBUS_ERROR_FAILED,
                                              "Seek failed");

    return TRUE;
}

static gboolean
playback_set_speed(tdbussplayPlayback *object, GDBusMethodInvocation *invocation,
                   double speed_factor, gpointer)
{
    enter_playback_handler(invocation);
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_FAILED,
                                          "Set speed not implemented");
    return TRUE;
}

static gboolean fifo_clear(tdbussplayURLFIFO *object,
                           GDBusMethodInvocation *invocation,
                           gint16 keep_first_n_entries, void *user_data)
{
    enter_urlfifo_handler(invocation);

    stream_id_t temp;
    const uint32_t current_id =
        Streamer::get_current_stream_id(temp) ? temp : UINT32_MAX;

    GVariantWrapper queued_ids;
    GVariantWrapper dropped_ids;
    Streamer::clear_queue(keep_first_n_entries, queued_ids, dropped_ids);

    tdbus_splay_urlfifo_complete_clear(object, invocation, current_id,
                                       GVariantWrapper::move(queued_ids),
                                       GVariantWrapper::move(dropped_ids));
    return TRUE;
}

static gboolean fifo_next(tdbussplayURLFIFO *object,
                          GDBusMethodInvocation *invocation, void *user_data)
{
    enter_urlfifo_handler(invocation);

    uint32_t skipped_id;
    uint32_t next_id;
    const auto play_status = Streamer::next(false, skipped_id, next_id);

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
                          gint16 keep_first_n_entries, GVariant *meta_data,
                          void *user_data)
{
    enter_urlfifo_handler(invocation);

    msg_info("Received stream %u \"%s\", keep %d",
             stream_id, stream_url, keep_first_n_entries);

    /*
     * The values of keep_first_n_entries:
     *
     *    0 - replace whole queue by new item
     *   -1 - keep all items in queue, enqueue new item
     *   -2 - replace whole queue by new item and skip to the new item
     *    n - remove all but the first n items in the queue, enqueue new item
     */
    const size_t keep =
        (keep_first_n_entries < 0)
        ? ((keep_first_n_entries == -2)
           ? 0
           : SIZE_MAX)
        : (size_t)keep_first_n_entries;
    const bool failed =
        !Streamer::push_item(stream_id, GVariantWrapper(stream_key),
                             stream_url, GVariantWrapper(meta_data),
                             keep);

    uint32_t dummy_skipped;
    uint32_t dummy_next = 0;
    const gboolean is_playing = (keep_first_n_entries == -2)
        ? Streamer::next(true, dummy_skipped, dummy_next) == Streamer::PlayStatus::PLAYING
        : Streamer::is_playing();

    tdbus_splay_urlfifo_complete_push(object, invocation, failed, is_playing);

    return TRUE;
}

static gboolean audiopath_player_activate(tdbusaupathPlayer *object,
                                          GDBusMethodInvocation *invocation,
                                          GVariant *request_data,
                                          gpointer user_data)
{
    enter_audiopath_player_handler(invocation);

    Streamer::activate();
    tdbus_aupath_player_complete_activate(object, invocation);

    return TRUE;
}

static gboolean audiopath_player_deactivate(tdbusaupathPlayer *object,
                                            GDBusMethodInvocation *invocation,
                                            GVariant *request_data,
                                            gpointer user_data)
{
    enter_audiopath_player_handler(invocation);

    Streamer::deactivate();
    tdbus_aupath_player_complete_deactivate(object, invocation);

    return TRUE;
}

static void mounta_device_will_be_removed(tdbusMounTA *mounta_proxy,
                                          guint16 id,
                                          const gchar *uuid,
                                          const gchar *root_path,
                                          gpointer)
{
    msg_vinfo(MESSAGE_LEVEL_IMPORTANT,
              "Received device removal notification: path='%s', id=%u",
              root_path, id);
    Streamer::remove_items_for_root_path(root_path);
}

static void player_ifaces(TDBus::Bus &bus)
{
    auto &pb_iface(TDBus::get_exported_iface<tdbussplayPlayback>());
    pb_iface
        .connect_method_handler<TDBus::StreamplayerPlaybackStart>(playback_start)
        .connect_method_handler<TDBus::StreamplayerPlaybackStop>(playback_stop)
        .connect_method_handler<TDBus::StreamplayerPlaybackPause>(playback_pause)
        .connect_method_handler<TDBus::StreamplayerPlaybackSeek>(playback_seek)
        .connect_method_handler<TDBus::StreamplayerPlaybackSetSpeed>(playback_set_speed);
    bus.add_auto_exported_interface(pb_iface);

    auto &uf_iface(TDBus::get_exported_iface<tdbussplayURLFIFO>());
    uf_iface
        .connect_method_handler<TDBus::StreamplayerURLFIFOClear>(fifo_clear)
        .connect_method_handler<TDBus::StreamplayerURLFIFONext>(fifo_next)
        .connect_method_handler<TDBus::StreamplayerURLFIFOPush>(fifo_push);
    bus.add_auto_exported_interface(uf_iface);
}

static void audio_player(TDBus::Bus &bus)
{
    static TDBus::Iface<tdbusaupathPlayer> player_iface("/de/tahifi/Streamplayer");
    player_iface
        .connect_method_handler<TDBus::AudioPathPlayerActivate>(audiopath_player_activate)
        .connect_method_handler<TDBus::AudioPathPlayerDeactivate>(audiopath_player_deactivate);
    bus.add_auto_exported_interface(player_iface);

    bus.add_watcher("de.tahifi.TAPSwitch",
        [] (GDBusConnection *connection, const char *name)
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Connecting to TAPSwitch");
            TDBus::get_singleton<tdbusaupathManager>().connect_proxy(connection,
                [] (TDBus::Proxy<tdbusaupathManager> &proxy, bool succeeded)
                {
                    if(!succeeded)
                        return;

                    proxy.call_and_forget<TDBus::AudioPathManagerRegisterPlayer>(
                        "strbo", "T+A Streaming Board streamplayer",
                        "/de/tahifi/Streamplayer");
                });
        },
        [] (GDBusConnection *connection, const char *name)
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Lost TAPSwitch");
        }
    );
}

static void mounta_watch(TDBus::Bus &bus)
{
    bus.add_watcher("de.tahifi.MounTA",
        [] (GDBusConnection *connection, const char *name)
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Connecting to MounTA");
            TDBus::get_singleton<tdbusMounTA>().connect_proxy(connection,
                [] (TDBus::Proxy<tdbusMounTA> &proxy, bool succeeded)
                {
                    if(succeeded)
                        proxy.connect_signal_handler<TDBus::MounTADeviceWillBeRemoved>(mounta_device_will_be_removed);
                });
        },
        [] (GDBusConnection *connection, const char *name)
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Lost MounTA");
        }
    );
}

void TDBus::setup(TDBus::Bus &bus)
{
    player_ifaces(bus);
    debugging_and_logging(bus);
    audio_player(bus);
    mounta_watch(bus);

    bus.connect(
        [] (GDBusConnection *connection)
        {
            msg_info("Session bus: Connected");
        },
        [] (GDBusConnection *)
        {
            msg_info("Session bus: Name acquired");
        },
        [] (GDBusConnection *)
        {
            msg_info("Session bus: Name lost");
        }
    );
}

TDBus::Bus &TDBus::session_bus()
{
    static TDBus::Bus bus("de.tahifi.Streamplayer", TDBus::Bus::Type::SESSION);
    return bus;
}

TDBus::Bus &TDBus::system_bus()
{
    static TDBus::Bus bus("de.tahifi.Streamplayer", TDBus::Bus::Type::SYSTEM);
    return bus;
}
