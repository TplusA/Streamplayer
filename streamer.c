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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <gst/gst.h>

#include "streamer.h"
#include "urlfifo.h"
#include "dbus_iface_deep.h"
#include "messages.h"

enum queue_mode
{
    QUEUEMODE_JUST_UPDATE_URI,
    QUEUEMODE_START_PLAYING,
    QUEUEMODE_FORCE_SKIP,
};

struct time_data
{
    int64_t position_s;
    int64_t duration_s;
};

struct streamer_data
{
    GstElement *pipeline;

    struct urlfifo_item current_stream;
    struct time_data previous_time;

    unsigned int suppress_next_stopped_events;
};

static void item_data_init(void **data)
{
    *(GstTagList **)data = NULL;
    *data = NULL;
}

static void item_data_free(void **data)
{
    if(*data != NULL)
    {
        gst_tag_list_unref(*(GstTagList **)data);
        *data = NULL;
    }
}

static inline GstTagList **item_data_get(struct urlfifo_item *item)
{
    return (GstTagList **)&item->data;
}

const struct urlfifo_item_data_ops streamer_urlfifo_item_data_ops =
{
    .data_init = item_data_init,
    .data_free = item_data_free,
};

static void invalidate_position_information(struct time_data *data)
{
    data->position_s = INT64_MAX;
    data->duration_s = INT64_MAX;
}

static void invalidate_current_stream(struct streamer_data *data)
{
    urlfifo_free_item(&data->current_stream);
    invalidate_position_information(&data->previous_time);
}

static bool get_stream_state(GstElement *pipeline, GstState *state,
                             const char *context, bool must_be_real_state)
{
    if(!must_be_real_state)
    {
        *state = GST_STATE_PENDING(pipeline);

        if(*state != GST_STATE_VOID_PENDING)
            return true;
    }

    GstStateChangeReturn ret = gst_element_get_state(pipeline, state, NULL, 0);

    switch(ret)
    {
      case GST_STATE_CHANGE_SUCCESS:
      case GST_STATE_CHANGE_ASYNC:
        return true;

      case GST_STATE_CHANGE_FAILURE:
        msg_error(0, LOG_ERR,
                  "%s: Failed changing state (gst_element_get_state())",
                  context);
        break;

      case GST_STATE_CHANGE_NO_PREROLL:
        msg_error(0, LOG_ERR,
                  "%s: Failed prerolling (gst_element_get_state())",
                  context);
        break;
    }

    msg_error(0, LOG_ERR,
              "%s: gst_element_get_state() failed (%d)", context, ret);

    return false;
}

static bool set_stream_state(GstElement *pipeline, GstState next_state,
                             const char *context)
{
    GstStateChangeReturn ret = gst_element_set_state(pipeline, next_state);

    switch(ret)
    {
      case GST_STATE_CHANGE_SUCCESS:
      case GST_STATE_CHANGE_ASYNC:
        return true;

      case GST_STATE_CHANGE_FAILURE:
        msg_error(0, LOG_ERR,
                  "%s: Failed changing state (gst_element_set_state())",
                  context);
        break;

      case GST_STATE_CHANGE_NO_PREROLL:
        msg_error(0, LOG_ERR,
                  "%s: Failed prerolling (gst_element_set_state())",
                  context);
        break;
    }

    msg_error(0, LOG_ERR,
              "%s: gst_element_set_state() failed (%d)", context, ret);

    return false;
}

static uint32_t try_queue_next_stream(GstElement *pipeline,
                                      struct streamer_data *data,
                                      enum queue_mode queue_mode,
                                      GstState next_state,
                                      const char *what)
{
    size_t tries = 0;
    bool maybe_suppress_stop_event = false;

    while(urlfifo_pop_item(&data->current_stream, true) >= 0)
    {
        ++tries;

        msg_info("Queuing stream %u due to %s request: \"%s\"",
                 data->current_stream.id, what, data->current_stream.url);

        if(queue_mode == QUEUEMODE_FORCE_SKIP)
        {
            if(!set_stream_state(pipeline, GST_STATE_READY, "Force skip"))
                return UINT32_MAX;

            invalidate_position_information(&data->previous_time);
            maybe_suppress_stop_event = true;
        }

        g_object_set(G_OBJECT(pipeline), "uri", data->current_stream.url, NULL);

        switch(queue_mode)
        {
          case QUEUEMODE_START_PLAYING:
          case QUEUEMODE_FORCE_SKIP:
            if(set_stream_state(pipeline, next_state, "Play queued"))
            {
                /*
                 * ATTENTION: This only works because the signal handler for
                 *            stream state changes runs in the same context as
                 *            this code. */
                if(maybe_suppress_stop_event)
                    ++data->suppress_next_stopped_events;

                return data->current_stream.id;
            }

            break;

          case QUEUEMODE_JUST_UPDATE_URI:
            return data->current_stream.id;
        }
    }

    if(tries == 0)
        msg_info("Got %s request, but URL FIFO is empty", what);
    else
        msg_info("Tried all URLs in FIFO, have no more streams to try");

    return UINT32_MAX;
}

static void queue_stream_from_url_fifo(GstElement *elem, gpointer user_data)
{
    try_queue_next_stream(elem, user_data,
                          QUEUEMODE_JUST_UPDATE_URI, GST_STATE_NULL,
                          "need next stream");
}

static void handle_end_of_stream(GstBus *bus, GstMessage *message,
                                 gpointer user_data)
{
    msg_info("Finished playing all streams");

    struct streamer_data *data = user_data;

    if(set_stream_state(data->pipeline, GST_STATE_READY, "EOS"))
        invalidate_current_stream(data);
}

static void add_tuple_to_tags_variant_builder(const GstTagList *list,
                                              const gchar *tag,
                                              gpointer user_data)
{
    GVariantBuilder *builder = user_data;
    const GValue *value = gst_tag_list_get_value_index(list, tag, 0);

    if(value == NULL)
        return;

    if(G_VALUE_HOLDS_STRING(value))
        g_variant_builder_add(builder, "(ss)", tag, g_value_get_string(value));
    else if(G_VALUE_HOLDS_BOOLEAN(value))
        g_variant_builder_add(builder, "(ss)", tag,
                              g_value_get_boolean(value) ? "true" : "false");
    else if(G_VALUE_HOLDS_UINT(value))
    {
        char buffer[256];

        snprintf(buffer, sizeof(buffer), "%u", g_value_get_uint(value));
        g_variant_builder_add(builder, "(ss)", tag, buffer);
    }
    else
        msg_error(ENOSYS, LOG_ERR, "stream tag \"%s\" is not a string", tag);
}

/*!
 * \todo Check embedded comment. How should we go about the GVariant format
 *     string(s)?
 */
static GVariant *tag_list_to_g_variant(const GstTagList *list)
{
    /*
     * The proper way to get at the GVariant format string would be to call
     * #tdbus_splay_playback_interface_info() and inspect the
     * \c GDBusInterfaceInfo introspection data to find the string deeply
     * buried inside the signal description.
     *
     * I think this is too much work just for retrieving a known value. Maybe a
     * unit test should be written to ensure that the hard-coded string here is
     * indeed correct. Maybe the retrieval should really be implemented in
     * production code, but only be done in the startup code.
     */
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ss)"));
    if(list != NULL)
        gst_tag_list_foreach(list, add_tuple_to_tags_variant_builder, &builder);

    return g_variant_builder_end(&builder);
}

static GstTagList *update_tags_for_item(struct urlfifo_item *item,
                                        GstTagList *tags)
{
    GstTagList **list = item_data_get(item);

    if(*list != NULL)
    {
        GstTagList *merged =
            gst_tag_list_merge(*list, tags, GST_TAG_MERGE_PREPEND);
        gst_tag_list_unref(*list);
        *list = merged;
    }
    else
    {
        *list = tags;
        gst_tag_list_ref(*list);
    }

    return *list;
}

static void handle_tag(GstBus *bus, GstMessage *message, gpointer user_data)
{
    GstTagList *tags = NULL;
    gst_message_parse_tag(message, &tags);

    struct streamer_data *data = user_data;

    GstTagList *list = update_tags_for_item(&data->current_stream, tags);

    if(list != NULL)
    {
        GVariant *meta_data = tag_list_to_g_variant(list);

        tdbus_splay_playback_emit_meta_data_changed(dbus_get_playback_iface(),
                                                    meta_data);
    }

    gst_tag_list_unref(tags);
}

static void emit_now_playing(tdbussplayPlayback *playback_iface,
                             struct streamer_data *data)
{
    if(playback_iface == NULL)
        return;

    GstTagList **list = item_data_get(&data->current_stream);

    GVariant *meta_data = tag_list_to_g_variant(*list);

    tdbus_splay_playback_emit_now_playing(playback_iface,
                                          data->current_stream.id,
                                          data->current_stream.url,
                                          urlfifo_is_full(), meta_data);
}

static void handle_stream_state_change(GstBus *bus, GstMessage *message,
                                       gpointer user_data)
{
    struct streamer_data *data = user_data;

    if(GST_MESSAGE_SRC(message) != GST_OBJECT(data->pipeline))
        return;

    GstState state, pending;
    gst_message_parse_state_changed(message, NULL, &state, &pending);

    /* we are currently not interested in transients */
    if(pending != GST_STATE_VOID_PENDING)
        return;

    tdbussplayPlayback *dbus_playback_iface = dbus_get_playback_iface();

    switch(state)
    {
      case GST_STATE_READY:
      case GST_STATE_NULL:
        if(dbus_playback_iface != NULL)
        {
            if(data->suppress_next_stopped_events == 0)
                tdbus_splay_playback_emit_stopped(dbus_playback_iface);
            else
                --data->suppress_next_stopped_events;
        }

        break;

      case GST_STATE_PAUSED:
        if(dbus_playback_iface != NULL)
            tdbus_splay_playback_emit_paused(dbus_playback_iface);
        break;

      case GST_STATE_PLAYING:
        emit_now_playing(dbus_playback_iface, data);
        break;

      case GST_STATE_VOID_PENDING:
        break;
    }
}

static void start_of_new_stream(GstElement *elem, gpointer user_data)
{
    struct streamer_data *data = user_data;

    GstState state;
    if(!get_stream_state(data->pipeline, &state, "New stream", true))
        return;

    if(state != GST_STATE_PLAYING)
        return;

    emit_now_playing(dbus_get_playback_iface(), data);
}

static void setup_source_element(GstElement *playbin,
                                 GstElement *source, gpointer user_data)
{
    if(strcmp(G_OBJECT_TYPE_NAME(source), "GstSoupHTTPSrc") == 0)
        g_object_set(G_OBJECT(source), "blocksize", *(guint *)user_data, NULL);
}

static void query_seconds(gboolean (*query)(GstElement *, GstFormat, gint64 *),
                          GstElement *element, int64_t *seconds)
{
    *seconds = -1;

    gint64 t_ns;

    if(!query(element, GST_FORMAT_TIME, &t_ns))
        return;

    if(t_ns < 0)
        return;

    /*
     * Rounding: simple cut to whole seconds, no arithmetic rounding.
     */
    *seconds = t_ns / (1000LL * 1000LL * 1000LL);
}

/*!
 * \bug There is a bug in GStreamer that leads to the wrong position being
 *     displayed in pause mode for internet streams. How to trigger: play some
 *     URL, then pause; skip to next URL; the position queried from the playbin
 *     pipeline is still the paused time, but should be 0.
 */
static gboolean report_progress(gpointer user_data)
{
    struct streamer_data *data = user_data;

    GstState state;
    if(!get_stream_state(data->pipeline, &state, "Progress", true))
    {
        if(set_stream_state(data->pipeline, GST_STATE_READY, "Progress"))
            invalidate_current_stream(data);

        return TRUE;
    }

    struct time_data new_time;

    if(state == GST_STATE_PLAYING || state == GST_STATE_PAUSED)
    {
        query_seconds(gst_element_query_position, data->pipeline,
                      &new_time.position_s);
        query_seconds(gst_element_query_duration, data->pipeline,
                      &new_time.duration_s);
    }
    else
    {
        invalidate_position_information(&new_time);
    }

    if(new_time.position_s == data->previous_time.position_s &&
       new_time.duration_s == data->previous_time.duration_s)
        return TRUE;

    data->previous_time = new_time;

    tdbussplayPlayback *playback_iface = dbus_get_playback_iface();

    if(playback_iface == NULL)
        return TRUE;

    tdbus_splay_playback_emit_position_changed(playback_iface,
                                               new_time.position_s, "s",
                                               new_time.duration_s, "s");

    return TRUE;
}

static struct streamer_data streamer_data;

int streamer_setup(GMainLoop *loop, const guint *soup_http_block_size)
{
    item_data_init(&streamer_data.current_stream.data);

    streamer_data.pipeline = gst_element_factory_make("playbin", "play");

    if(streamer_data.pipeline == NULL)
        return -1;

    g_signal_connect(streamer_data.pipeline, "about-to-finish",
                     G_CALLBACK(queue_stream_from_url_fifo), &streamer_data);

    g_signal_connect(streamer_data.pipeline, "audio-changed",
                     G_CALLBACK(start_of_new_stream), &streamer_data);

    g_signal_connect(streamer_data.pipeline, "source-setup",
                     G_CALLBACK(setup_source_element),
                     (guint *)soup_http_block_size);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(streamer_data.pipeline));
    log_assert(bus != NULL);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message::eos",
                     G_CALLBACK(handle_end_of_stream), &streamer_data);
    g_signal_connect(bus, "message::tag",
                     G_CALLBACK(handle_tag), &streamer_data);
    g_signal_connect(bus, "message::state-changed",
                     G_CALLBACK(handle_stream_state_change), &streamer_data);
    gst_object_unref(bus);

    g_main_loop_ref(loop);

    if(set_stream_state(streamer_data.pipeline, GST_STATE_READY, "Setup"))
        invalidate_current_stream(&streamer_data);

    g_timeout_add(50, report_progress, &streamer_data);

    return 0;
}

void streamer_shutdown(GMainLoop *loop)
{
    if(loop == NULL)
        return;

    g_main_loop_unref(loop);

    set_stream_state(streamer_data.pipeline, GST_STATE_NULL, "Shutdown");

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(streamer_data.pipeline));
    log_assert(bus != NULL);
    gst_bus_remove_signal_watch(bus);
    gst_object_unref(bus);

    gst_object_unref(GST_OBJECT(streamer_data.pipeline));
    streamer_data.pipeline = NULL;

    invalidate_current_stream(&streamer_data);
}

void streamer_start(void)
{
    GstState state;
    if(!get_stream_state(streamer_data.pipeline, &state, "Start", false))
        return;

    switch(state)
    {
      case GST_STATE_PLAYING:
        break;

      case GST_STATE_READY:
        try_queue_next_stream(streamer_data.pipeline, &streamer_data,
                              QUEUEMODE_START_PLAYING, GST_STATE_PLAYING,
                              "start playing");
        break;

      case GST_STATE_PAUSED:
        set_stream_state(streamer_data.pipeline, GST_STATE_PLAYING, "Start");
        break;

      case GST_STATE_NULL:
      case GST_STATE_VOID_PENDING:
        msg_error(ENOSYS, LOG_ERR,
                  "Start: pipeline is in unhandled state %d", state);
        break;
    }
}

void streamer_stop(void)
{
    msg_info("Stopping as requested");
    log_assert(streamer_data.pipeline != NULL);

    if(set_stream_state(streamer_data.pipeline, GST_STATE_READY, "Stop"))
    {
        invalidate_current_stream(&streamer_data);
        urlfifo_clear(0, NULL);
    }
}

/*!
 * \bug Call it a bug in or a feature of GStreamer playbin, but the following
 *     is anyway inconvenient: pausing an internet stream for a long time
 *     causes skipping to the next stream in the FIFO when trying to resume.
 *     There is probably some buffer overflow and connection timeout involved,
 *     but playbin won't tell us. It is therefore not easy to determine if we
 *     should reconnect or really take the next URL when asked to.
 */
void streamer_pause(void)
{
    msg_info("Pausing as requested");
    log_assert(streamer_data.pipeline != NULL);

    GstState state;
    if(!get_stream_state(streamer_data.pipeline, &state, "Pause", true))
        return;

    switch(state)
    {
      case GST_STATE_PAUSED:
        return;

      case GST_STATE_READY:
        try_queue_next_stream(streamer_data.pipeline, &streamer_data,
                              QUEUEMODE_START_PLAYING, GST_STATE_PAUSED,
                              "stream pause");
        break;

      case GST_STATE_PLAYING:
        set_stream_state(streamer_data.pipeline, GST_STATE_PAUSED, "Pause");
        break;

      case GST_STATE_NULL:
      case GST_STATE_VOID_PENDING:
        msg_error(ENOSYS, LOG_ERR,
                  "Pause: pipeline is in unhandled state %d", state);
        break;
    }
}

bool streamer_seek(guint64 position, const char *units)
{
    msg_info("Seek position %llu %s requested\n",
             (unsigned long long)position, units);

    if(strcmp(units, "ms") != 0)
        BUG("Seek units other than ms are not implemented yet");

    static const GstSeekFlags seek_flags =
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE;

    return gst_element_seek_simple(streamer_data.pipeline, GST_FORMAT_TIME,
                                   seek_flags, position * GST_MSECOND);
}

bool streamer_next(bool skip_only_if_playing, uint32_t *out_next_id)
{
    msg_info("Next requested");
    log_assert(streamer_data.pipeline != NULL);

    bool is_playing;
    uint32_t next_id = UINT32_MAX;

    GstState state;
    if(!get_stream_state(streamer_data.pipeline, &state, "Next", true))
        is_playing = false;
    else
    {
        is_playing = (state == GST_STATE_PLAYING);

        if(!skip_only_if_playing || is_playing)
            next_id = try_queue_next_stream(streamer_data.pipeline, &streamer_data,
                                            QUEUEMODE_FORCE_SKIP, state,
                                            "skip to next");
    }

    if(out_next_id != NULL)
        *out_next_id = next_id;

    return is_playing;
}

bool streamer_is_playing(void)
{
    GstState state;
    if(!get_stream_state(streamer_data.pipeline, &state, "Next", true))
        return false;
    else
        return state == GST_STATE_PLAYING;
}

bool streamer_get_current_stream_id(uint16_t *id)
{
    if(streamer_data.current_stream.url[0] == '\0')
        return false;

    *id = streamer_data.current_stream.id;

    return true;
}
