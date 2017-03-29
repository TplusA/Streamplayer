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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <gst/gst.h>
#include <gst/tag/tag.h>

#include "streamer.h"
#include "urlfifo.h"
#include "dbus_iface_deep.h"
#include "messages.h"

enum stopped_reason
{
    /*! Reason not known. Should be used very rarely, if ever. */
    STOPPED_REASON_UNKNOWN,

    /*! Cannot play because URL FIFO is empty. */
    STOPPED_REASON_QUEUE_EMPTY,

    /*! Cannot stop because the player is already stopped. */
    STOPPED_REASON_ALREADY_STOPPED,

    /*! I/O error on physical medium (e.g., read error on some USB drive). */
    STOPPED_REASON_PHYSICAL_MEDIA_IO,

    /*! I/O error on the network (e.g., broken network connection). */
    STOPPED_REASON_NET_IO,

    /*! Have no URL. */
    STOPPED_REASON_URL_MISSING,

    /*! Network protocol error. */
    STOPPED_REASON_PROTOCOL,

    /*! Authentication with some external system has failed. */
    STOPPED_REASON_AUTHENTICATION,

    /*! Resource does not exist. */
    STOPPED_REASON_DOES_NOT_EXIST,

    /*! Resource has wrong type. */
    STOPPED_REASON_WRONG_TYPE,

    /*! Cannot access resource due to restricted permissions. */
    STOPPED_REASON_PERMISSION_DENIED,

    /*! Failed decoding stream because of a missing codec. */
    STOPPED_REASON_MISSING_CODEC,

    /*! Stream codec is known, but format wrong. */
    STOPPED_REASON_WRONG_STREAM_FORMAT,

    /*! Decoding failed. */
    STOPPED_REASON_BROKEN_STREAM,

    /*! Decryption key missing. */
    STOPPED_REASON_ENCRYPTED,

    /*! Cannot decrypt because this is not implemented/supported. */
    STOPPED_REASON_DECRYPTION_NOT_SUPPORTED,

    /*! Stable name for the highest-valued code. */
    STOPPED_REASON_LAST_VALUE = STOPPED_REASON_DECRYPTION_NOT_SUPPORTED,
};

struct time_data
{
    int64_t position_s;
    int64_t duration_s;
};

struct failure_data
{
    enum stopped_reason reason;
    bool clear_fifo_on_error;
    bool report_on_stream_stop;
};

struct streamer_data
{
    GMutex lock;

    GstElement *pipeline;
    guint bus_watch;
    guint progress_watcher;
    guint soup_http_block_size;
    gulong signal_handler_ids[2];

    /*!
     * The item currently played/paused.
     *
     * The item is moved from the URL FIFO into this place using
     * #urlfifo_pop_item(). Check #urlfifo_item::is_valid to tell valid from
     * invalid (unused) items.
     */
    struct urlfifo_item current_stream;

    /*!
     * The item to be played/paused next.
     *
     * The item is moved from the URL FIFO into this place using
     * #urlfifo_pop_item() soon before the currently playing stream ends. It is
     * moved to #streamer_data::current_stream when the stream actually starts
     * playing.
     *
     * Check #urlfifo_item::is_valid to tell valid from invalid (unused) items.
     */
    struct urlfifo_item next_stream;

    struct failure_data fail;

    struct time_data previous_time;
    struct time_data current_time;

    GstClock *system_clock;
    bool is_tag_update_scheduled;
    GstClockTime next_allowed_tag_update_time;

    unsigned int suppress_next_stopped_events;

    enum PlayStatus supposed_play_status;
};

#define LOCK_DATA(SD) \
    do \
    { \
        g_mutex_lock(&(SD)->lock); \
    } \
    while(0)

#define UNLOCK_DATA(SD) \
    do \
    { \
        g_mutex_unlock(&(SD)->lock); \
    } \
    while(0)

struct stream_data
{
    struct streamer_data *streamer_data;
    GstTagList *tag_list;
    GVariant *stream_key;
};

typedef enum
{
    GST_PLAY_FLAG_VIDEO             = (1 << 0),
    GST_PLAY_FLAG_AUDIO             = (1 << 1),
    GST_PLAY_FLAG_TEXT              = (1 << 2),
    GST_PLAY_FLAG_VIS               = (1 << 3),
    GST_PLAY_FLAG_SOFT_VOLUME       = (1 << 4),
    GST_PLAY_FLAG_NATIVE_AUDIO      = (1 << 5),
    GST_PLAY_FLAG_NATIVE_VIDEO      = (1 << 6),
    GST_PLAY_FLAG_DOWNLOAD          = (1 << 7),
    GST_PLAY_FLAG_BUFFERING         = (1 << 8),
    GST_PLAY_FLAG_DEINTERLACE       = (1 << 9),
    GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10),
}
GstPlayFlags;

static inline const struct stream_data *item_data_get(const struct urlfifo_item *item)
{
    return item->data;
}

static inline struct stream_data *item_data_get_nonconst(struct urlfifo_item *item)
{
    return item->data;
}

static void invalidate_position_information(struct time_data *data)
{
    data->position_s = INT64_MAX;
    data->duration_s = INT64_MAX;
}

static inline void invalidate_stream_position_information(struct streamer_data *data)
{
    invalidate_position_information(&data->previous_time);
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
                  "[%s] Failed changing state (gst_element_set_state())",
                  context);
        break;

      case GST_STATE_CHANGE_NO_PREROLL:
        msg_error(0, LOG_ERR,
                  "[%s] Failed prerolling (gst_element_set_state())",
                  context);
        break;
    }

    msg_error(0, LOG_ERR,
              "[%s] gst_element_set_state() failed (%d)", context, ret);

    return false;
}

static void emit_stopped_with_error(tdbussplayPlayback *playback_iface,
                                    const struct urlfifo_item *failed_stream,
                                    enum stopped_reason reason)
{
    if(playback_iface == NULL)
        return;

    /*!
     * String IDs that can be used as a reason as to why the stream was
     * stopped.
     *
     * Must be sorted according to values in #stopped_reason enumeration.
     */
    static const char *reasons[] =
    {
        "flow.unknown",
        "flow.nourl",
        "flow.stopped",
        "io.media",
        "io.net",
        "io.nourl",
        "io.protocol",
        "io.auth",
        "io.unavailable",
        "io.type",
        "io.denied",
        "data.codec",
        "data.format",
        "data.broken",
        "data.encrypted",
        "data.nodecrypter",
    };

    G_STATIC_ASSERT(G_N_ELEMENTS(reasons) == STOPPED_REASON_LAST_VALUE + 1U);


    if(failed_stream->is_valid)
        tdbus_splay_playback_emit_stopped_with_error(playback_iface,
                                                     failed_stream->id,
                                                     failed_stream->url,
                                                     urlfifo_get_size() == 0,
                                                     reasons[reason]);
    else
        tdbus_splay_playback_emit_stopped_with_error(playback_iface, 0, "",
                                                     urlfifo_get_size() == 0,
                                                     reasons[reason]);
}

static int rebuild_playbin(const char *context);

static void do_stop_pipeline_and_recover_from_error(struct streamer_data *data)
{
    static const char context[] = "deferred stop";

    /*
     * HACK ALERT -- HACK ALERT -- HACK ALERT
     *
     * The correct way to recover from any errors in the pipeline would be to
     * set the state to GST_STATE_NULL to flush errors, then set it to
     * GST_STATE_READY to move on. It should not even be necessary to do this
     * inside a thread or in an idle function.
     *
     * Unfortunately, there are several known deadlock problems in GStreamer
     * that have not been addressed in current versions of GStreamer (as of
     * December 2016), and it seems we have hit one of those here. Attempting
     * to flush errors the correct way in here sometimes leads to a deadlock
     * deep inside GStreamer. It also happens pretty frequently, especially in
     * case many errors occur in quick succession (e.g., while trying to play a
     * directory that contains many files which are not playable and some
     * retries are allowed).
     *
     * There is no real cure to that problem, but destroying the whole pipeline
     * and creating a new one seems to work.
     */
    rebuild_playbin(context);

    msg_info("Stop reason is %d", data->fail.reason);

    if(data->fail.clear_fifo_on_error)
        urlfifo_clear(0, NULL);

    const struct urlfifo_item *const failed_stream =
        ((data->next_stream.is_valid &&
          data->next_stream.fail_state != URLFIFO_FAIL_STATE_NOT_FAILED)
         ? &data->next_stream
         : &data->current_stream);

    invalidate_stream_position_information(data);
    emit_stopped_with_error(dbus_get_playback_iface(),
                            failed_stream, data->fail.reason);

    data->suppress_next_stopped_events = 0;

    urlfifo_free_item(&data->current_stream);
    urlfifo_free_item(&data->next_stream);
    memset(&data->fail, 0, sizeof(data->fail));
}

static gboolean stop_pipeline_and_recover_from_error(gpointer user_data)
{
    msg_vinfo(MESSAGE_LEVEL_DIAG, "Recover from error");

    struct streamer_data *data = user_data;

    LOCK_DATA(data);
    do_stop_pipeline_and_recover_from_error(data);
    UNLOCK_DATA(data);

    return G_SOURCE_REMOVE;
}

static void schedule_error_recovery(struct streamer_data *data,
                                    enum stopped_reason reason)
{
    data->fail.reason = reason;
    data->fail.clear_fifo_on_error = false;

    g_idle_add(stop_pipeline_and_recover_from_error, data);
}

static void item_data_fail(void *data, void *user_data)
{
    if(data == NULL)
        return;

    struct stream_data *const sd = data;
    const struct failure_data *const fdata = user_data;

    if(!fdata->report_on_stream_stop)
        schedule_error_recovery(sd->streamer_data, fdata->reason);
    else
        sd->streamer_data->fail = *fdata;
}

static void item_data_free(void **data)
{
    if(*data == NULL)
        return;

    struct stream_data *sd = *data;

    if(sd->tag_list != NULL)
        gst_tag_list_unref(sd->tag_list);

    if(sd->stream_key != NULL)
        g_variant_unref(sd->stream_key);

    free(sd);

    *data = NULL;
}

static uint32_t try_dequeue_next(struct streamer_data *data,
                                 bool is_queued_item_expected,
                                 const char *context)
{
    struct failure_data fdata =
    {
        .reason = STOPPED_REASON_UNKNOWN,
        .report_on_stream_stop = data->current_stream.is_valid,
    };

    if(urlfifo_pop_item(&data->next_stream, true) < 0)
    {
        if(!is_queued_item_expected)
            return UINT32_MAX;

        msg_info("[%s] Cannot dequeue, URL FIFO is empty", context);
        fdata.reason = STOPPED_REASON_QUEUE_EMPTY;
    }
    else if(data->next_stream.url == NULL)
    {
        msg_vinfo(MESSAGE_LEVEL_IMPORTANT,
                  "[%s] Cannot dequeue, URL in item is empty", context);
        fdata.reason = STOPPED_REASON_URL_MISSING;
    }
    else
        return data->next_stream.id;

    if(data->current_stream.is_valid)
        urlfifo_fail_item(&data->current_stream, &fdata);
    else if(data->next_stream.is_valid)
    {
        urlfifo_move_item(&data->current_stream, &data->next_stream);
        urlfifo_fail_item(&data->current_stream, &fdata);
    }
    else
        schedule_error_recovery(data, fdata.reason);

    return UINT32_MAX;
}

static bool play_next_stream(struct streamer_data *data, GstState next_state,
                             const char *context)
{
    g_object_set(data->pipeline, "uri", data->next_stream.url, NULL);

    urlfifo_free_item(&data->current_stream);

    const bool retval =
        set_stream_state(data->pipeline, next_state, "play queued");

    if(retval)
        invalidate_stream_position_information(data);

    return retval;
}

static void queue_stream_from_url_fifo(GstElement *elem, gpointer user_data)
{
    static const char context[] = "need next stream";

    struct streamer_data *data = user_data;

    LOCK_DATA(data);

    if(data->current_stream.is_valid && !data->next_stream.is_valid)
    {
        if(try_dequeue_next(data, false, context) != UINT32_MAX)
        {
            msg_info("Setting URL %s for next stream %u",
                     data->next_stream.url, data->next_stream.id);

            g_object_set(data->pipeline, "uri", data->next_stream.url, NULL);
        }
    }
    else
        urlfifo_move_item(&data->current_stream, &data->next_stream);

    UNLOCK_DATA(data);
}

static void handle_end_of_stream(GstMessage *message,
                                 struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    msg_info("Finished playing all streams");

    LOCK_DATA(data);

    if(set_stream_state(data->pipeline, GST_STATE_READY, "EOS"))
    {
        tdbus_splay_playback_emit_stopped(dbus_get_playback_iface(),
                                          data->current_stream.id);
        urlfifo_free_item(&data->current_stream);
    }

    UNLOCK_DATA(data);
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
        msg_error(ENOSYS, LOG_ERR, "stream tag \"%s\" is of type %s",
                  tag, G_VALUE_TYPE_NAME(value));
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
    static const char *filtered_out[] =
    {
        GST_TAG_IMAGE,
        GST_TAG_PREVIEW_IMAGE,
        GST_TAG_COMMENT,
        GST_TAG_EXTENDED_COMMENT,
        GST_TAG_COMPOSER,
        GST_TAG_DATE,
        GST_TAG_DATE_TIME,
        GST_TAG_COPYRIGHT,
        GST_TAG_COPYRIGHT_URI,
        GST_TAG_ENCODER,
        GST_TAG_ENCODER_VERSION,
        GST_TAG_ENCODED_BY,
        GST_TAG_ISRC,
        GST_TAG_ORGANIZATION,
        GST_TAG_LOCATION,
        GST_TAG_HOMEPAGE,
        GST_TAG_CONTACT,
        GST_TAG_LICENSE,
        GST_TAG_LICENSE_URI,
        GST_TAG_SERIAL,
        GST_TAG_KEYWORDS,
        GST_TAG_LYRICS,
        GST_TAG_ATTACHMENT,
        GST_TAG_APPLICATION_DATA,
        GST_TAG_TRACK_GAIN,
        GST_TAG_TRACK_PEAK,
        GST_TAG_ALBUM_GAIN,
        GST_TAG_ALBUM_PEAK,
        GST_TAG_REFERENCE_LEVEL,
        "private-id3v2-frame",          /* from Deezer */
        "private-qt-tag",               /* from certain m4a files */
    };

    for(size_t i = 0; i < sizeof(filtered_out) / sizeof(filtered_out[0]); ++i)
        gst_tag_list_remove_tag(tags, filtered_out[i]);

    GstTagList *list = item_data_get_nonconst(item)->tag_list;

    if(list != NULL)
    {
        GstTagList *merged =
            gst_tag_list_merge(list, tags, GST_TAG_MERGE_PREPEND);
        gst_tag_list_unref(list);
        item_data_get_nonconst(item)->tag_list = merged;
    }
    else
    {
        gst_tag_list_ref(tags);
        item_data_get_nonconst(item)->tag_list = tags;
    }

    return item_data_get_nonconst(item)->tag_list;
}

enum ImageTagType
{
    IMAGE_TAG_TYPE_NONE,
    IMAGE_TAG_TYPE_RAW_DATA,
    IMAGE_TAG_TYPE_URI,
};

static enum ImageTagType get_image_tag_type(const GstCaps *caps)
{
    if(caps == NULL)
        return IMAGE_TAG_TYPE_NONE;

    for(size_t i = 0; /* nothing */; ++i)
    {
        const GstStructure *caps_struct = gst_caps_get_structure(caps, i);

        if(caps_struct == NULL)
            break;

        const gchar *name = gst_structure_get_name(caps_struct);

        if(g_str_has_prefix(name, "image/"))
            return IMAGE_TAG_TYPE_RAW_DATA;
        else if(g_str_equal(name, "text/uri-list"))
            return IMAGE_TAG_TYPE_URI;
    }

    return IMAGE_TAG_TYPE_NONE;
}

static void send_image_data_to_cover_art_cache(GstSample *sample,
                                               uint8_t base_priority,
                                               struct urlfifo_item *item)
{
    GstBuffer *buffer = gst_sample_get_buffer(sample);

    if(buffer == NULL)
        return;

    const GstStructure *sample_info = gst_sample_get_info(sample);
    GstTagImageType image_type;

    if(sample_info == NULL ||
       !gst_structure_get_enum(sample_info, "image-type",
                               GST_TYPE_TAG_IMAGE_TYPE, &image_type))
        image_type = GST_TAG_IMAGE_TYPE_UNDEFINED;

    static uint8_t prio_raise_table[] =
    {
        10,     /* GST_TAG_IMAGE_TYPE_UNDEFINED */
        18,     /* GST_TAG_IMAGE_TYPE_FRONT_COVER */
        14,     /* GST_TAG_IMAGE_TYPE_BACK_COVER */
        13,     /* GST_TAG_IMAGE_TYPE_LEAFLET_PAGE */
        17,     /* GST_TAG_IMAGE_TYPE_MEDIUM */
        16,     /* GST_TAG_IMAGE_TYPE_LEAD_ARTIST */
        15,     /* GST_TAG_IMAGE_TYPE_ARTIST */
         9,     /* GST_TAG_IMAGE_TYPE_CONDUCTOR */
         8,     /* GST_TAG_IMAGE_TYPE_BAND_ORCHESTRA */
         4,     /* GST_TAG_IMAGE_TYPE_COMPOSER */
         3,     /* GST_TAG_IMAGE_TYPE_LYRICIST */
         1,     /* GST_TAG_IMAGE_TYPE_RECORDING_LOCATION */
         5,     /* GST_TAG_IMAGE_TYPE_DURING_RECORDING */
         6,     /* GST_TAG_IMAGE_TYPE_DURING_PERFORMANCE */
         7,     /* GST_TAG_IMAGE_TYPE_VIDEO_CAPTURE */
         0,     /* GST_TAG_IMAGE_TYPE_FISH */
        11,     /* GST_TAG_IMAGE_TYPE_ILLUSTRATION */
        12,     /* GST_TAG_IMAGE_TYPE_BAND_ARTIST_LOGO */
         2,     /* GST_TAG_IMAGE_TYPE_PUBLISHER_STUDIO_LOGO */
    };

    if(image_type < 0 ||
       (size_t)image_type >= sizeof(prio_raise_table) / sizeof(prio_raise_table[0]))
        return;

    const uint8_t priority = base_priority + prio_raise_table[image_type];

    if(gst_buffer_n_memory(buffer) != 1)
    {
        BUG("Image data spans multiple memory regions (not implemented)");
        return;
    }

    GstMemory *memory = gst_buffer_peek_memory(buffer, 0);
    GstMapInfo mi;

    if(!gst_memory_map(memory, &mi, GST_MAP_READ))
    {
        msg_error(0, LOG_ERR, "Failed mapping image data");
        return;
    }

    struct stream_data *sd = item_data_get_nonconst(item);

    GError *error = NULL;
    tdbus_artcache_write_call_add_image_by_data_sync(dbus_artcache_get_write_iface(),
                                                     sd->stream_key, priority,
                                                     g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                                               mi.data, mi.size,
                                                                               sizeof(mi.data[0])),
                                                     NULL, &error);
    dbus_handle_error(&error);

    gst_memory_unmap(memory, &mi);
}

struct TagAndPriority
{
    const char *const tag;
    const uint8_t priority;
};

static void update_picture_for_item(struct urlfifo_item *item,
                                    const GstTagList *tags)
{
    static const struct TagAndPriority image_tags[] =
    {
        { .tag = GST_TAG_IMAGE,         .priority = 150, },
        { .tag = GST_TAG_PREVIEW_IMAGE, .priority = 120, },
    };

    for(size_t i = 0; i < sizeof(image_tags) / sizeof(image_tags[0]); ++i)
    {
        GstSample *sample = NULL;

        if(!gst_tag_list_get_sample(tags, image_tags[i].tag, &sample))
            continue;

        GstCaps *caps = gst_sample_get_caps(sample);

        if(caps == NULL)
        {
            gst_sample_unref(sample);
            continue;
        }

        const enum ImageTagType tag_type = get_image_tag_type(caps);

        switch(tag_type)
        {
          case IMAGE_TAG_TYPE_NONE:
            break;

          case IMAGE_TAG_TYPE_RAW_DATA:
            send_image_data_to_cover_art_cache(sample, image_tags[i].priority, item);
            break;

          case IMAGE_TAG_TYPE_URI:
            BUG("Embedded image tag is URI: not implemented");
            break;
        }

        gst_sample_unref(sample);
    }
}

static void emit_tags__unlocked(struct streamer_data *data)
{
    struct stream_data *sd = item_data_get_nonconst(&data->current_stream);

    if(sd == NULL)
        return;

    GstTagList *list = sd->tag_list;
    GVariant *meta_data = tag_list_to_g_variant(list);

    tdbus_splay_playback_emit_meta_data_changed(dbus_get_playback_iface(),
                                                data->current_stream.id,
                                                meta_data);

    data->next_allowed_tag_update_time =
        gst_clock_get_time(data->system_clock) + 500UL * GST_MSECOND;
    data->is_tag_update_scheduled = false;
}

static gboolean emit_tags(gpointer user_data)
{
    struct streamer_data *data = user_data;

    LOCK_DATA(data);

    if(data->current_stream.is_valid)
        emit_tags__unlocked(data);
    else
        data->is_tag_update_scheduled = false;

    UNLOCK_DATA(data);

    return FALSE;
}

static void handle_tag__unlocked(GstMessage *message, struct streamer_data *data)
{
    if(!data->current_stream.is_valid)
        return;

    GstTagList *tags = NULL;
    gst_message_parse_tag(message, &tags);

    update_picture_for_item(&data->current_stream, tags);
    update_tags_for_item(&data->current_stream, tags);

    gst_tag_list_unref(tags);

    if(data->is_tag_update_scheduled)
        return;

    GstClockTime now = gst_clock_get_time(data->system_clock);
    GstClockTimeDiff cooldown = GST_CLOCK_DIFF(now, data->next_allowed_tag_update_time);

    if(cooldown <= 0L)
        emit_tags__unlocked(data);
    else
    {
        g_timeout_add(GST_TIME_AS_MSECONDS(cooldown), emit_tags, data);
        data->is_tag_update_scheduled = true;
    }
}

static void handle_tag(GstMessage *message, struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    LOCK_DATA(data);

    handle_tag__unlocked(message, data);

    UNLOCK_DATA(data);
}

static void emit_now_playing(tdbussplayPlayback *playback_iface,
                             struct streamer_data *data)
{
    if(playback_iface == NULL || data->current_stream.url == NULL)
        return;

    struct stream_data *sd = item_data_get_nonconst(&data->current_stream);
    GVariant *meta_data = tag_list_to_g_variant(sd->tag_list);

    tdbus_splay_playback_emit_now_playing(playback_iface,
                                          data->current_stream.id,
                                          sd->stream_key,
                                          data->current_stream.url,
                                          urlfifo_is_full(), meta_data);
}

static enum stopped_reason core_error_to_stopped_reason(GstCoreError code,
                                                        bool is_local_error)
{
    switch(code)
    {
      case GST_CORE_ERROR_MISSING_PLUGIN:
      case GST_CORE_ERROR_DISABLED:
        return STOPPED_REASON_MISSING_CODEC;

      case GST_CORE_ERROR_FAILED:
      case GST_CORE_ERROR_TOO_LAZY:
      case GST_CORE_ERROR_NOT_IMPLEMENTED:
      case GST_CORE_ERROR_STATE_CHANGE:
      case GST_CORE_ERROR_PAD:
      case GST_CORE_ERROR_THREAD:
      case GST_CORE_ERROR_NEGOTIATION:
      case GST_CORE_ERROR_EVENT:
      case GST_CORE_ERROR_SEEK:
      case GST_CORE_ERROR_CAPS:
      case GST_CORE_ERROR_TAG:
      case GST_CORE_ERROR_CLOCK:
      case GST_CORE_ERROR_NUM_ERRORS:
        break;
    }

    BUG("Failed to convert GstCoreError code %d to reason code", code);

    return STOPPED_REASON_UNKNOWN;
}

static enum stopped_reason library_error_to_stopped_reason(GstLibraryError code,
                                                           bool is_local_error)
{
    BUG("Failed to convert GstLibraryError code %d to reason code", code);
    return STOPPED_REASON_UNKNOWN;
}

static enum stopped_reason resource_error_to_stopped_reason(GstResourceError code,
                                                            bool is_local_error)
{
    switch(code)
    {
      case GST_RESOURCE_ERROR_NOT_FOUND:
        return STOPPED_REASON_DOES_NOT_EXIST;

      case GST_RESOURCE_ERROR_OPEN_READ:
        return is_local_error
            ? STOPPED_REASON_PHYSICAL_MEDIA_IO
            : STOPPED_REASON_NET_IO;

      case GST_RESOURCE_ERROR_READ:
      case GST_RESOURCE_ERROR_SEEK:
        return STOPPED_REASON_PROTOCOL;

      case GST_RESOURCE_ERROR_NOT_AUTHORIZED:
        return STOPPED_REASON_PERMISSION_DENIED;

      case GST_RESOURCE_ERROR_FAILED:
      case GST_RESOURCE_ERROR_TOO_LAZY:
      case GST_RESOURCE_ERROR_BUSY:
      case GST_RESOURCE_ERROR_OPEN_WRITE:
      case GST_RESOURCE_ERROR_OPEN_READ_WRITE:
      case GST_RESOURCE_ERROR_CLOSE:
      case GST_RESOURCE_ERROR_WRITE:
      case GST_RESOURCE_ERROR_SYNC:
      case GST_RESOURCE_ERROR_SETTINGS:
      case GST_RESOURCE_ERROR_NO_SPACE_LEFT:
      case GST_RESOURCE_ERROR_NUM_ERRORS:
        break;
    }

    BUG("Failed to convert GstResourceError code %d to reason code", code);

    return STOPPED_REASON_UNKNOWN;
}

static enum stopped_reason stream_error_to_stopped_reason(GstStreamError code,
                                                          bool is_local_error)
{
    switch(code)
    {
      case GST_STREAM_ERROR_FAILED:
      case GST_STREAM_ERROR_TYPE_NOT_FOUND:
      case GST_STREAM_ERROR_WRONG_TYPE:
        return STOPPED_REASON_WRONG_TYPE;

      case GST_STREAM_ERROR_CODEC_NOT_FOUND:
        return STOPPED_REASON_MISSING_CODEC;

      case GST_STREAM_ERROR_DECODE:
      case GST_STREAM_ERROR_DEMUX:
        return STOPPED_REASON_BROKEN_STREAM;

      case GST_STREAM_ERROR_FORMAT:
        return STOPPED_REASON_WRONG_STREAM_FORMAT;

      case GST_STREAM_ERROR_DECRYPT:
        return STOPPED_REASON_DECRYPTION_NOT_SUPPORTED;

      case GST_STREAM_ERROR_DECRYPT_NOKEY:
        return STOPPED_REASON_ENCRYPTED;

      case GST_STREAM_ERROR_TOO_LAZY:
      case GST_STREAM_ERROR_NOT_IMPLEMENTED:
      case GST_STREAM_ERROR_ENCODE:
      case GST_STREAM_ERROR_MUX:
      case GST_STREAM_ERROR_NUM_ERRORS:
        break;
    }

    BUG("Failed to convert GstStreamError code %d to reason code", code);

    return STOPPED_REASON_UNKNOWN;
}

static enum stopped_reason gerror_to_stopped_reason(GError *error,
                                                    bool is_local_error)
{
    if(error->domain == GST_CORE_ERROR)
        return core_error_to_stopped_reason((GstCoreError)error->code,
                                            is_local_error);

    if(error->domain == GST_LIBRARY_ERROR)
        return library_error_to_stopped_reason((GstLibraryError)error->code,
                                               is_local_error);

    if(error->domain == GST_RESOURCE_ERROR)
        return resource_error_to_stopped_reason((GstResourceError)error->code,
                                                is_local_error);

    if(error->domain == GST_STREAM_ERROR)
        return stream_error_to_stopped_reason((GstStreamError)error->code,
                                              is_local_error);

    BUG("Unknown error domain %u for error code %d",
        error->domain, error->code);

    return STOPPED_REASON_UNKNOWN;
}

static bool determine_is_local_error_by_url(const struct urlfifo_item *item)
{
    if(!item->is_valid)
        return true;

    if(item->url == NULL)
        return true;

#if GST_CHECK_VERSION(1, 5, 1)
    GstUri *uri = gst_uri_from_string(item->url);

    if(uri == NULL)
        return true;

    const char *scheme = gst_uri_get_scheme(uri);
    const bool retval = (scheme == NULL || strcmp(scheme, "file") == 0);

    gst_uri_unref(uri);

    return retval;
#else /* pre 1.5.1 */
    static const char protocol_prefix[] = "file://";

    return strncmp(item->url, protocol_prefix,
                   sizeof(protocol_prefix) - 1) == 0;
#endif /* use GstUri if not older than v1.5.1 */
}

static void handle_error_message(GstMessage *message, struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    GError *error = NULL;
    gchar *debug = NULL;

    gst_message_parse_error(message, &error, &debug);

    LOCK_DATA(data);

    struct urlfifo_item *const failed_stream = data->next_stream.is_valid
        ? &data->next_stream
        : &data->current_stream;

    const bool is_local_error = determine_is_local_error_by_url(failed_stream);

    GstElement *source_elem;
    g_object_get(data->pipeline, "source", &source_elem, NULL);

    struct failure_data fdata =
    {
        .reason = gerror_to_stopped_reason(error, is_local_error),
        .report_on_stream_stop = false,
    };

    msg_error(0, LOG_ERR, "ERROR code %d, domain %s from \"%s\"",
              error->code, g_quark_to_string(error->domain),
              GST_MESSAGE_SRC_NAME(message));
    msg_error(0, LOG_ERR, "ERROR message: %s", error->message);
    msg_error(0, LOG_ERR, "ERROR debug: %s", debug);
    msg_error(0, LOG_ERR, "ERROR mapped to stop reason %d, reporting %s",
              fdata.reason, fdata.report_on_stream_stop ? "on stop" : "now");

    urlfifo_fail_item(failed_stream, &fdata);

    UNLOCK_DATA(data);

    g_free(debug);
    g_error_free(error);
}

static void handle_warning_message(GstMessage *message, struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    GError *error = NULL;
    gchar *debug = NULL;

    gst_message_parse_warning(message, &error, &debug);

    msg_error(0, LOG_ERR, "WARNING code %d, domain %s from \"%s\"",
              error->code, g_quark_to_string(error->domain),
              GST_MESSAGE_SRC_NAME(message));
    msg_error(0, LOG_ERR, "WARNING message: %s", error->message);
    msg_error(0, LOG_ERR, "WARNING debug: %s", debug);

    g_free(debug);
    g_error_free(error);
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

    LOCK_DATA(data);

    if(!data->current_stream.is_valid)
    {
        data->progress_watcher = 0;
        UNLOCK_DATA(data);
        return G_SOURCE_REMOVE;
    }

    const GstState state = GST_STATE(data->pipeline);

    switch(state)
    {
      case GST_STATE_PLAYING:
      case GST_STATE_PAUSED:
        query_seconds(gst_element_query_position, data->pipeline,
                      &data->current_time.position_s);
        break;

      case GST_STATE_READY:
      case GST_STATE_NULL:
      case GST_STATE_VOID_PENDING:
        data->current_time.position_s = INT64_MAX;
        break;
    }

    if(data->current_time.position_s != data->previous_time.position_s ||
       data->current_time.duration_s != data->previous_time.duration_s)
    {
        data->previous_time = data->current_time;

        tdbussplayPlayback *playback_iface = dbus_get_playback_iface();

        if(playback_iface != NULL)
            tdbus_splay_playback_emit_position_changed(playback_iface,
                                                       data->current_stream.id,
                                                       data->current_time.position_s, "s",
                                                       data->current_time.duration_s, "s");
    }

    UNLOCK_DATA(data);

    return G_SOURCE_CONTINUE;
}

static void handle_stream_state_change(GstMessage *message,
                                       struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    LOCK_DATA(data);

    const bool is_ours =
        (GST_MESSAGE_SRC(message) == GST_OBJECT(data->pipeline));

    if(!is_ours && !msg_is_verbose(MESSAGE_LEVEL_TRACE))
    {
        UNLOCK_DATA(data);
        return;
    }

    GstState state, pending;
    gst_message_parse_state_changed(message, NULL, &state, &pending);

    msg_vinfo(MESSAGE_LEVEL_TRACE,
              "State change on %s \"%s\": state %s, pending %s",
              G_OBJECT_TYPE_NAME(GST_MESSAGE_SRC(message)),
              GST_MESSAGE_SRC_NAME(message),
              gst_element_state_get_name(state),
              gst_element_state_get_name(pending));

    /* leave now if we came here only for the trace */
    if(!is_ours)
    {
        UNLOCK_DATA(data);
        return;
    }

    /* we are currently not interested in transients */
    if(pending != GST_STATE_VOID_PENDING)
    {
        UNLOCK_DATA(data);
        return;
    }

    tdbussplayPlayback *dbus_playback_iface = dbus_get_playback_iface();

    struct urlfifo_item *const active_stream = data->current_stream.is_valid
        ? &data->current_stream
        : &data->next_stream;

    switch(state)
    {
      case GST_STATE_READY:
      case GST_STATE_NULL:
        if(data->progress_watcher != 0)
        {
            g_source_remove(data->progress_watcher);
            data->progress_watcher = 0;
        }

        if(dbus_playback_iface != NULL && active_stream->is_valid)
        {
            if(data->suppress_next_stopped_events == 0)
            {
                tdbus_splay_playback_emit_stopped(dbus_playback_iface,
                                                  active_stream->id);
                urlfifo_free_item(active_stream);
            }
            else
                --data->suppress_next_stopped_events;
        }

        break;

      case GST_STATE_PAUSED:
        if(dbus_playback_iface != NULL)
            tdbus_splay_playback_emit_paused(dbus_playback_iface,
                                             active_stream->id);
        break;

      case GST_STATE_PLAYING:
        if(dbus_playback_iface != NULL)
            emit_now_playing(dbus_playback_iface, data);

        if(data->progress_watcher == 0)
            data->progress_watcher = g_timeout_add(50, report_progress, data);

        break;

      case GST_STATE_VOID_PENDING:
        break;
    }

    UNLOCK_DATA(data);
}

static void clear_current_meta_data(struct stream_data *sd)
{
    GstTagList *list = sd->tag_list;

    if(list != NULL)
        gst_tag_list_unref(list);

    sd->tag_list = gst_tag_list_new_empty();
}

static void handle_start_of_stream(GstMessage *message,
                                   struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    LOCK_DATA(data);

    struct urlfifo_item *const stream = (data->next_stream.is_valid
                                         ? &data->next_stream
                                         : &data->current_stream);
    struct stream_data *sd = item_data_get_nonconst(stream);

    clear_current_meta_data(sd);
    invalidate_stream_position_information(data);

    if(stream == &data->next_stream)
        urlfifo_move_item(&data->current_stream, &data->next_stream);

    emit_now_playing(dbus_get_playback_iface(), data);

    UNLOCK_DATA(data);
}

static void handle_buffering(GstMessage *message, struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    gint percent = -1;
    gst_message_parse_buffering(message, &percent);

    if(percent >= 0 && percent <= 100)
        msg_vinfo(MESSAGE_LEVEL_DIAG, "Buffer level: %d%%", percent);
    else
        msg_error(ERANGE, LOG_NOTICE, "Buffering percentage is %d", percent);
}

static void handle_stream_duration(GstMessage *message, struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    LOCK_DATA(data);

    query_seconds(gst_element_query_duration, data->pipeline,
                  &data->current_time.duration_s);

    UNLOCK_DATA(data);
}

static void handle_stream_duration_async(GstMessage *message, struct streamer_data *data)
{
    GstClockTime running_time;
    gst_message_parse_async_done(message, &running_time);

    LOCK_DATA(data);

    if(running_time != GST_CLOCK_TIME_NONE)
        data->current_time.duration_s = running_time / (1000LL * 1000LL * 1000LL);
    else
        query_seconds(gst_element_query_duration, data->pipeline,
                      &data->current_time.duration_s);

    UNLOCK_DATA(data);
}

static void setup_source_element(GstElement *playbin,
                                 GstElement *source, gpointer user_data)
{
    struct streamer_data *data = user_data;

    if(strcmp(G_OBJECT_TYPE_NAME(source), "GstSoupHTTPSrc") == 0)
        g_object_set(source, "blocksize", &data->soup_http_block_size, NULL);
}

static gboolean bus_message_handler(GstBus *bus, GstMessage *message,
                                    gpointer user_data)
{
    switch(GST_MESSAGE_TYPE(message))
    {
      case GST_MESSAGE_EOS:
        handle_end_of_stream(message, user_data);
        break;

      case GST_MESSAGE_TAG:
        handle_tag(message, user_data);
        break;

      case GST_MESSAGE_STATE_CHANGED:
        handle_stream_state_change(message, user_data);
        break;

      case GST_MESSAGE_STREAM_START:
        handle_start_of_stream(message, user_data);
        break;

      case GST_MESSAGE_BUFFERING:
        handle_buffering(message, user_data);
        break;

      case GST_MESSAGE_DURATION_CHANGED:
        handle_stream_duration(message, user_data);
        break;

      case GST_MESSAGE_ASYNC_DONE:
        handle_stream_duration_async(message, user_data);
        break;

      case GST_MESSAGE_ERROR:
        handle_error_message(message, user_data);
        break;

      case GST_MESSAGE_WARNING:
        handle_warning_message(message, user_data);
        break;

      case GST_MESSAGE_NEW_CLOCK:
      case GST_MESSAGE_STREAM_STATUS:
        /* these messages are not handled, and they are explicitly ignored */
        break;

      case GST_MESSAGE_UNKNOWN:
      case GST_MESSAGE_INFO:
      case GST_MESSAGE_STATE_DIRTY:
      case GST_MESSAGE_STEP_DONE:
      case GST_MESSAGE_CLOCK_PROVIDE:
      case GST_MESSAGE_CLOCK_LOST:
      case GST_MESSAGE_STRUCTURE_CHANGE:
      case GST_MESSAGE_APPLICATION:
      case GST_MESSAGE_ELEMENT:
      case GST_MESSAGE_SEGMENT_START:
      case GST_MESSAGE_SEGMENT_DONE:
      case GST_MESSAGE_LATENCY:
      case GST_MESSAGE_ASYNC_START:
      case GST_MESSAGE_REQUEST_STATE:
      case GST_MESSAGE_STEP_START:
      case GST_MESSAGE_QOS:
      case GST_MESSAGE_PROGRESS:
      case GST_MESSAGE_TOC:
      case GST_MESSAGE_RESET_TIME:
      case GST_MESSAGE_NEED_CONTEXT:
      case GST_MESSAGE_HAVE_CONTEXT:
      case GST_MESSAGE_ANY:
#if GST_CHECK_VERSION(1, 5, 1)
      case GST_MESSAGE_EXTENDED:
      case GST_MESSAGE_DEVICE_ADDED:
      case GST_MESSAGE_DEVICE_REMOVED:
#endif /* v1.5.1 */
        BUG("UNHANDLED MESSAGE TYPE %s from %s",
            GST_MESSAGE_TYPE_NAME(message), GST_MESSAGE_SRC_NAME(message));
        break;
    }

    return G_SOURCE_CONTINUE;
}

static struct streamer_data streamer_data;

static int create_playbin(const char *context)
{
    streamer_data.pipeline = gst_element_factory_make("playbin", "play");
    streamer_data.bus_watch = 0;

    if(streamer_data.pipeline == NULL)
        return -1;

    gst_object_ref(GST_OBJECT(streamer_data.pipeline));

    streamer_data.bus_watch =
        gst_bus_add_watch(GST_ELEMENT_BUS(streamer_data.pipeline),
                          bus_message_handler, &streamer_data);

    g_object_set(streamer_data.pipeline, "flags", GST_PLAY_FLAG_AUDIO, NULL);

    streamer_data.signal_handler_ids[0] =
        g_signal_connect(streamer_data.pipeline, "about-to-finish",
                         G_CALLBACK(queue_stream_from_url_fifo), &streamer_data);

    streamer_data.signal_handler_ids[1] =
        g_signal_connect(streamer_data.pipeline, "source-setup",
                         G_CALLBACK(setup_source_element), &streamer_data);

    set_stream_state(streamer_data.pipeline, GST_STATE_READY, context);

    return 0;
}

static void teardown_playbin(void)
{
    if(streamer_data.pipeline == NULL)
        return;

    g_source_remove(streamer_data.bus_watch);
    streamer_data.bus_watch = 0;

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(streamer_data.pipeline));
    log_assert(bus != NULL);
    gst_object_unref(bus);

    for(size_t i = 0;
        i < sizeof(streamer_data.signal_handler_ids) / sizeof(streamer_data.signal_handler_ids[0]);
        ++i)
    {
        g_signal_handler_disconnect(streamer_data.pipeline,
                                    streamer_data.signal_handler_ids[i]);
        streamer_data.signal_handler_ids[i] = 0;
    }

    gst_object_unref(GST_OBJECT(streamer_data.pipeline));
    streamer_data.pipeline = NULL;
}

static int rebuild_playbin(const char *context)
{
    set_stream_state(streamer_data.pipeline, GST_STATE_NULL, "rebuild");
    teardown_playbin();
    return create_playbin(context);
}

int streamer_setup(GMainLoop *loop, guint soup_http_block_size)
{
    memset(&streamer_data, 0, sizeof(streamer_data));

    g_mutex_init(&streamer_data.lock);

    streamer_data.soup_http_block_size = soup_http_block_size;

    if(create_playbin("setup") < 0)
        return -1;

    streamer_data.system_clock = gst_system_clock_obtain();
    streamer_data.next_allowed_tag_update_time =
        gst_clock_get_time(streamer_data.system_clock);

    static bool initialized;

    if(!initialized)
        initialized = true;
    else
        log_assert(false);

    g_main_loop_ref(loop);

    return 0;
}

void streamer_shutdown(GMainLoop *loop)
{
    if(loop == NULL)
        return;

    g_main_loop_unref(loop);

    set_stream_state(streamer_data.pipeline, GST_STATE_NULL, "shutdown");

    teardown_playbin();

    gst_object_unref(GST_OBJECT(streamer_data.system_clock));
    streamer_data.system_clock = NULL;

    urlfifo_free_item(&streamer_data.current_stream);
}

void streamer_start(void)
{
    static const char context[] = "start playing";

    LOCK_DATA(&streamer_data);

    log_assert(streamer_data.pipeline != NULL);

    streamer_data.supposed_play_status = PLAY_STATUS_PLAYING;

    GstState state = GST_STATE(streamer_data.pipeline);
    const GstState pending_state = GST_STATE_PENDING(streamer_data.pipeline);

    switch(pending_state)
    {
      case GST_STATE_PLAYING:
        break;

      case GST_STATE_PAUSED:
        /* we are in progress of pausing, so let's pretend our current state is
         * paused */
        state = GST_STATE_PAUSED;

        /* fall-through */

      case GST_STATE_READY:
      case GST_STATE_NULL:
      case GST_STATE_VOID_PENDING:
        switch(state)
        {
          case GST_STATE_PLAYING:
            break;

          case GST_STATE_READY:
          case GST_STATE_NULL:
            if(try_dequeue_next(&streamer_data, true, context) != UINT32_MAX)
                play_next_stream(&streamer_data, GST_STATE_PLAYING, context);

            break;

          case GST_STATE_PAUSED:
            set_stream_state(streamer_data.pipeline, GST_STATE_PLAYING, context);
            break;

          case GST_STATE_VOID_PENDING:
            msg_error(ENOSYS, LOG_ERR,
                      "Start: pipeline is in unhandled state %d", state);
            break;
        }
    }

    UNLOCK_DATA(&streamer_data);
}

void streamer_stop(void)
{
    static const char context[] = "stop playing";

    LOCK_DATA(&streamer_data);

    msg_info("Stopping as requested");
    log_assert(streamer_data.pipeline != NULL);

    streamer_data.supposed_play_status = PLAY_STATUS_STOPPED;

    const GstState pending = GST_STATE_PENDING(streamer_data.pipeline);
    const GstState state = (pending == GST_STATE_VOID_PENDING)
        ? GST_STATE(streamer_data.pipeline)
        : pending;
    bool may_emit_stopped_with_error = true;

    switch(state)
    {
      case GST_STATE_PLAYING:
      case GST_STATE_PAUSED:
        if(set_stream_state(streamer_data.pipeline, GST_STATE_READY, context))
        {
            may_emit_stopped_with_error = false;
            urlfifo_clear(0, NULL);
        }
        else
            streamer_data.fail.clear_fifo_on_error = true;

        break;

      case GST_STATE_READY:
      case GST_STATE_NULL:
        urlfifo_clear(0, NULL);
        break;

      case GST_STATE_VOID_PENDING:
        msg_error(ENOSYS, LOG_ERR,
                  "Start: pipeline is in unhandled state %d", state);
        break;
    }

    if(may_emit_stopped_with_error &&
       (GST_STATE(streamer_data.pipeline) == GST_STATE_READY ||
        GST_STATE(streamer_data.pipeline) == GST_STATE_NULL) &&
       pending == GST_STATE_VOID_PENDING)
    {
        emit_stopped_with_error(dbus_get_playback_iface(),
                                &streamer_data.current_stream,
                                STOPPED_REASON_ALREADY_STOPPED);
    }

    UNLOCK_DATA(&streamer_data);
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
    static const char context[] = "pause stream";

    LOCK_DATA(&streamer_data);

    msg_info("Pausing as requested");
    log_assert(streamer_data.pipeline != NULL);

    streamer_data.supposed_play_status = PLAY_STATUS_PAUSED;

    const GstState state = GST_STATE(streamer_data.pipeline);

    switch(state)
    {
      case GST_STATE_PAUSED:
        break;

      case GST_STATE_NULL:
        if(try_dequeue_next(&streamer_data, true, context) != UINT32_MAX)
            play_next_stream(&streamer_data, GST_STATE_PAUSED, context);

        break;

      case GST_STATE_READY:
      case GST_STATE_PLAYING:
        set_stream_state(streamer_data.pipeline, GST_STATE_PAUSED, context);
        break;

      case GST_STATE_VOID_PENDING:
        msg_error(ENOSYS, LOG_ERR,
                  "Pause: pipeline is in unhandled state %d", state);
        break;
    }

    UNLOCK_DATA(&streamer_data);
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

enum PlayStatus streamer_next(bool skip_only_if_not_stopped,
                              uint32_t *out_skipped_id, uint32_t *out_next_id)
{
    static const char context[] = "skip to next";

    LOCK_DATA(&streamer_data);

    msg_info("Next requested");
    log_assert(streamer_data.pipeline != NULL);

    const uint32_t replaced_next_id = streamer_data.next_stream.is_valid
        ? streamer_data.next_stream.id
        : UINT32_MAX;
    uint32_t next_id =
        (streamer_data.supposed_play_status != PLAY_STATUS_STOPPED || !skip_only_if_not_stopped)
        ? try_dequeue_next(&streamer_data, true, context)
        : UINT32_MAX;
    const uint32_t skipped_id = (replaced_next_id != UINT32_MAX
                                 ? replaced_next_id
                                 : (streamer_data.current_stream.is_valid
                                    ? streamer_data.current_stream.id
                                    : UINT32_MAX));

    if(next_id == UINT32_MAX)
        streamer_data.supposed_play_status = PLAY_STATUS_STOPPED;
    else
    {
        GstState next_state = GST_STATE_READY;

        if(set_stream_state(streamer_data.pipeline, next_state, context))
        {
            switch(streamer_data.supposed_play_status)
            {
              case PLAY_STATUS_STOPPED:
                break;

              case PLAY_STATUS_PLAYING:
                next_state = GST_STATE_PLAYING;
                break;

              case PLAY_STATUS_PAUSED:
                next_state = GST_STATE_PAUSED;
                break;
            }

            const bool need_to_suppress_stop = streamer_data.current_stream.is_valid;

            if(play_next_stream(&streamer_data, next_state, context))
            {
                if(need_to_suppress_stop)
                    ++streamer_data.suppress_next_stopped_events;
            }
            else
                next_id = UINT32_MAX;
        }
        else
            next_id = UINT32_MAX;
    }

    if(out_skipped_id != NULL)
        *out_skipped_id = skipped_id;

    if(out_next_id != NULL)
        *out_next_id = next_id;

    const enum PlayStatus retval = streamer_data.supposed_play_status;

    UNLOCK_DATA(&streamer_data);

    return retval;
}

bool streamer_is_playing(void)
{
    return GST_STATE(streamer_data.pipeline) == GST_STATE_PLAYING;
}

bool streamer_get_current_stream_id(uint16_t *id)
{
    LOCK_DATA(&streamer_data);

    bool retval;

    if(streamer_data.current_stream.url == NULL)
        retval = false;
    else
    {
        *id = streamer_data.current_stream.id;
        retval = true;
    }

    UNLOCK_DATA(&streamer_data);

    return retval;
}

bool streamer_push_item(uint16_t stream_id, GVariant *stream_key,
                        const char *stream_url, size_t keep_items)
{
    static const struct urlfifo_item_data_ops streamer_urlfifo_item_data_ops =
    {
        .data_fail = item_data_fail,
        .data_free = item_data_free,
    };

    struct stream_data *sd = malloc(sizeof(*sd));

    if(sd == NULL)
    {
        msg_out_of_memory("stream data");
        return false;
    }

    sd->streamer_data = &streamer_data;
    sd->tag_list = NULL;

    if(stream_key != NULL)
    {
        g_variant_ref(stream_key);
        sd->stream_key = stream_key;
    }
    else
        sd->stream_key = NULL;

    return urlfifo_push_item(stream_id, stream_url, NULL, NULL,
                             keep_items, NULL, sd,
                             &streamer_urlfifo_item_data_ops) != 0;
}
