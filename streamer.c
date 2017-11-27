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

    bool is_player_activated;

    GstElement *pipeline;
    guint bus_watch;
    guint progress_watcher;
    guint soup_http_block_size;
    gulong signal_handler_ids[2];

    /*!
     * The item currently played/paused/handled.
     *
     * The item is moved from the URL FIFO into this place using
     * #urlfifo_pop_item() before the item is actually playing. Check
     * #urlfifo_item::state to tell what is supposed to be done with the item.
     */
    struct urlfifo_item current_stream;

    bool is_failing;
    struct failure_data fail;

    struct time_data previous_time;
    struct time_data current_time;

    GstClock *system_clock;
    bool is_tag_update_scheduled;
    GstClockTime next_allowed_tag_update_time;

    bool stream_has_just_started;

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

struct image_sent_data
{
    uint8_t *data;
    size_t size;
    uint8_t priority;
};

struct stream_data
{
    struct streamer_data *streamer_data;
    GstTagList *tag_list;
    GVariant *stream_key;
    struct image_sent_data big_image;
    struct image_sent_data preview_image;
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

static void emit_stopped(tdbussplayPlayback *playback_iface,
                         struct streamer_data *data)
{
    data->supposed_play_status = PLAY_STATUS_STOPPED;

    if(playback_iface != NULL)
        tdbus_splay_playback_emit_stopped(dbus_get_playback_iface(),
                                          data->current_stream.id);
}

static void emit_stopped_with_error(tdbussplayPlayback *playback_iface,
                                    struct streamer_data *data,
                                    enum stopped_reason reason)
{
    data->supposed_play_status = PLAY_STATUS_STOPPED;

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

    const struct urlfifo_item *failed_stream = &data->current_stream;

    switch(failed_stream->state)
    {
      case URLFIFO_ITEM_STATE_INVALID:
        tdbus_splay_playback_emit_stopped_with_error(playback_iface, 0, "",
                                                     urlfifo_get_size() == 0,
                                                     reasons[reason]);
        break;

      case URLFIFO_ITEM_STATE_IN_QUEUE:
      case URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE:
      case URLFIFO_ITEM_STATE_ACTIVE:
      case URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT:
      case URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED:
        tdbus_splay_playback_emit_stopped_with_error(playback_iface,
                                                     failed_stream->id,
                                                     failed_stream->url,
                                                     urlfifo_get_size() == 0,
                                                     reasons[reason]);
        break;
    }
}

static int rebuild_playbin(struct streamer_data *data, const char *context);

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
    rebuild_playbin(data, context);

    msg_info("Stop reason is %d", data->fail.reason);

    if(data->fail.clear_fifo_on_error)
        urlfifo_clear(0, NULL);

    invalidate_stream_position_information(data);
    emit_stopped_with_error(dbus_get_playback_iface(), data,
                            data->fail.reason);

    data->stream_has_just_started = false;
    data->is_failing = false;

    urlfifo_free_item(&data->current_stream);
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
    data->is_failing = true;
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

static struct urlfifo_item *pick_next_item(struct streamer_data *data,
                                           bool *next_stream_is_in_fifo)
{
    switch(data->current_stream.state)
    {
      case URLFIFO_ITEM_STATE_IN_QUEUE:
        *next_stream_is_in_fifo = false;
        return &data->current_stream;

      case URLFIFO_ITEM_STATE_INVALID:
      case URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE:
      case URLFIFO_ITEM_STATE_ACTIVE:
      case URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT:
      case URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED:
        break;
    }

    struct urlfifo_item *const it = urlfifo_peek();
    *next_stream_is_in_fifo = (it != NULL);

    return it;
}

/*!
 * Return next item from queue, if any.
 *
 * The function takes a look at the URL FIFO. In case it is empty, this
 * function returns \c NULL to indicate that there is no next item. Depending
 * on context, error recovery might be required to handle this case (see
 * parameter \p is_queued_item_expected).
 *
 * In case the URL FIFO is not empty, it will return a pointer to the head
 * element. The head element will remain in the URL FIFO in case the current
 * stream in \p data is still valid, otherwise the element will be moved to the
 * current stream structure. That is, the returned pointer will point either to
 * a URL FIFO element or to the current stream structure.
 * See \p replaced_current_stream for how to distinguish these two cases.
 *
 * \param data
 *     Structure to take the current stream from, also used for error recovery
 *     (see #schedule_error_recovery()).
 *
 * \param is_queued_item_expected
 *     If this parameter is \c true, then error recovery is scheduled in case
 *     the queue is empty. If it is \c false and the queue is empty, then
 *     nothing special happens and the function simply returns \c NULL.
 *
 * \param[out] replaced_current_stream
 *     If this function returns a non-NULL pointer, then \c true is returned
 *     through this parameter in case the pointer points to the current stream
 *     structure in \p data, and \c false is returned in case the current
 *     stream was not changed and the pointer points directly to an item in the
 *     URL FIFO. If this function returns a NULL pointer, then \c true is
 *     returned through this parameter in case the current stream has been
 *     replaced by the next item from the URL FIFO (which also will have been
 *     marked as failed), and \c false is returned in case the current stream
 *     has been marked as failed (if any).
 *
 * \param[out] current_stream_is_just_in_queue
 *     If this function returns a non-NULL pointer, then \c true is returned in
 *     case the currently active stream's state is equal to
 *     #URLFIFO_ITEM_STATE_IN_QUEUE. In all other cases, \c false is returned.
 *
 * \param context
 *     For better logs.
 *
 * \returns
 *     A pointer to the next stream information, or \c NULL in case there is no
 *     such stream.
 */
static struct urlfifo_item *try_take_next(struct streamer_data *data,
                                          bool is_queued_item_expected,
                                          bool *replaced_current_stream,
                                          bool *current_stream_is_just_in_queue,
                                          const char *context)
{
    struct failure_data fdata =
    {
        .reason = STOPPED_REASON_UNKNOWN,
        .report_on_stream_stop = urlfifo_is_item_valid(&data->current_stream),
    };

    struct urlfifo_item *queued = urlfifo_peek();
    struct urlfifo_item *next = pick_next_item(data, replaced_current_stream);

    *current_stream_is_just_in_queue = false;

    if(next == NULL)
    {
        if(!is_queued_item_expected)
            return NULL;

        msg_info("[%s] Cannot dequeue, URL FIFO is empty", context);
        fdata.reason = STOPPED_REASON_QUEUE_EMPTY;
    }
    else if(next->url == NULL)
    {
        msg_vinfo(MESSAGE_LEVEL_IMPORTANT,
                  "[%s] Cannot dequeue, URL in item is empty", context);
        fdata.reason = STOPPED_REASON_URL_MISSING;
    }
    else
    {
        if(*replaced_current_stream)
        {
            urlfifo_pop_item(&data->current_stream, false);
            next = &data->current_stream;
        }

        switch(data->current_stream.state)
        {
          case URLFIFO_ITEM_STATE_IN_QUEUE:
            *current_stream_is_just_in_queue = true;
            break;

          case URLFIFO_ITEM_STATE_INVALID:
          case URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT:
          case URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED:
          case URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE:
          case URLFIFO_ITEM_STATE_ACTIVE:
            break;
        }

        return next;
    }

    /* error, failure handling below */
    *replaced_current_stream = urlfifo_is_item_valid(&data->current_stream);

    if(*replaced_current_stream || queued != NULL)
    {
        if(*replaced_current_stream)
            urlfifo_pop_item(&data->current_stream, false);
        else
            urlfifo_pop_item(NULL, false);

        urlfifo_fail_item(&data->current_stream, &fdata);
    }
    else
    {
        *replaced_current_stream = false;
        schedule_error_recovery(data, fdata.reason);
    }

    return NULL;
}

static bool play_next_stream(struct streamer_data *data,
                             struct urlfifo_item *replaced_stream,
                             struct urlfifo_item *next_stream,
                             GstState next_state, bool is_skipping,
                             bool is_prefetching_for_gapless, const char *context)
{
    log_assert(next_stream != NULL);
    log_assert(urlfifo_is_item_valid(next_stream));

    switch(next_stream->state)
    {
      case URLFIFO_ITEM_STATE_INVALID:
      case URLFIFO_ITEM_STATE_ACTIVE:
      case URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT:
      case URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED:
        BUG("[%s] Unexpected stream state %s",
            context, urlfifo_state_name(next_stream->state));
        return false;

      case URLFIFO_ITEM_STATE_IN_QUEUE:
        urlfifo_set_item_state(next_stream,
                               URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE);
        break;

      case URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE:
        /* already in the making, don't be so hasty */
        return true;
    }

    if(replaced_stream != NULL)
        urlfifo_set_item_state(replaced_stream,
                               is_skipping
                               ? URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED
                               : URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT);

    msg_info("Setting URL %s for next stream %u",
             next_stream->url, next_stream->id);

    g_object_set(data->pipeline, "uri", next_stream->url, NULL);

    if(is_prefetching_for_gapless)
        return true;

    const bool retval = set_stream_state(data->pipeline, next_state, "play queued");

    if(retval)
        invalidate_stream_position_information(data);

    return retval;
}

static inline void queue_stream_from_url_fifo__unlocked(GstElement *elem,
                                                        struct streamer_data *data)
{
    static const char context[] = "need next stream";

    bool is_next_current;
    bool is_just_queued;
    struct urlfifo_item *const next_stream =
        try_take_next(data, false, &is_next_current, &is_just_queued, context);

    if(!urlfifo_is_item_valid(&data->current_stream) && next_stream == NULL)
    {
        BUG("Having nothing to play, have nothing in queue, "
            "but GStreamer is asking for more");
        return;
    }

    if(next_stream == NULL)
    {
        /* we are done here */
        urlfifo_set_item_state(&data->current_stream,
                               URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT);
        return;
    }

    play_next_stream(data, is_next_current ? NULL : &data->current_stream,
                     next_stream, GST_STATE_NULL, false, true, context);
}

static void queue_stream_from_url_fifo(GstElement *elem, struct streamer_data *data)
{
    LOCK_DATA(data);
    if(!data->is_failing)
        queue_stream_from_url_fifo__unlocked(elem, data);
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
        emit_stopped(dbus_get_playback_iface(), data);
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

        snprintf(buffer, sizeof(buffer), "%" PRIu32, g_value_get_uint(value));
        g_variant_builder_add(builder, "(ss)", tag, buffer);
    }
    else if(G_VALUE_HOLDS_UINT64(value))
    {
        char buffer[256];

        snprintf(buffer, sizeof(buffer), "%" PRIu64, g_value_get_uint64(value));
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
                                               bool is_big_image,
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

    struct stream_data *sd = item_data_get_nonconst(item);
    struct image_sent_data *const sent_data =
        is_big_image ? &sd->big_image : &sd->preview_image;

    if(sent_data->priority > priority)
        return;

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

    if(sent_data->data == mi.data && sent_data->size == mi.size)
        return;

    sent_data->data = mi.data;
    sent_data->size = mi.size;
    sent_data->priority = priority;

    tdbus_artcache_write_call_add_image_by_data(dbus_artcache_get_write_iface(),
                                                sd->stream_key, priority,
                                                g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                                          mi.data, mi.size,
                                                                          sizeof(mi.data[0])),
                                                NULL, NULL, NULL);

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
            send_image_data_to_cover_art_cache(sample, i == 0,
                                               image_tags[i].priority, item);
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

    if(urlfifo_is_item_valid(&data->current_stream))
        emit_tags__unlocked(data);
    else
        data->is_tag_update_scheduled = false;

    UNLOCK_DATA(data);

    return FALSE;
}

static void handle_tag__unlocked(GstMessage *message, struct streamer_data *data)
{
    if(!urlfifo_is_item_valid(&data->current_stream))
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
    if(!urlfifo_is_item_valid(item))
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

static struct urlfifo_item *get_failed_item(struct streamer_data *data)
{
    switch(data->current_stream.state)
    {
      case URLFIFO_ITEM_STATE_INVALID:
        return NULL;

      case URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT:
      case URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED:
        if(urlfifo_pop_item(&data->current_stream, true) < 0)
            return NULL;

        break;

      case URLFIFO_ITEM_STATE_IN_QUEUE:
      case URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE:
      case URLFIFO_ITEM_STATE_ACTIVE:
        break;
    }

    return &data->current_stream;
}

static void handle_error_message(GstMessage *message, struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    GError *error = NULL;
    gchar *debug = NULL;

    gst_message_parse_error(message, &error, &debug);

    LOCK_DATA(data);

    struct urlfifo_item *const failed_stream = get_failed_item(data);

    if(failed_stream == NULL)
        BUG("Supposed to handle error, but have no item");
    else
    {
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
    }

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

    if(!urlfifo_is_item_valid(&data->current_stream))
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

static bool activate_stream(struct streamer_data *data,
                            GstState pipeline_state)
{
    switch(data->current_stream.state)
    {
      case URLFIFO_ITEM_STATE_INVALID:
        BUG("Current item is invalid, switched to %s",
            gst_element_state_get_name(pipeline_state));
        break;

      case URLFIFO_ITEM_STATE_ACTIVE:
        return true;

      case URLFIFO_ITEM_STATE_IN_QUEUE:
        BUG("Unexpected state %s for stream switched to %s",
            urlfifo_state_name(data->current_stream.state),
            gst_element_state_get_name(pipeline_state));
        urlfifo_set_item_state(&data->current_stream,
                               URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED);

        /* fall-through */

      case URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT:
      case URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED:
        break;

      case URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE:
        urlfifo_set_item_state(&data->current_stream,
                               URLFIFO_ITEM_STATE_ACTIVE);
        return true;
    }

    return false;
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

    GstState oldstate, state, pending;
    gst_message_parse_state_changed(message, &oldstate, &state, &pending);

    msg_vinfo(MESSAGE_LEVEL_TRACE,
              "State change on %s \"%s\": state %s -> %s, pending %s, target %s (%sours)",
              G_OBJECT_TYPE_NAME(GST_MESSAGE_SRC(message)),
              GST_MESSAGE_SRC_NAME(message),
              gst_element_state_get_name(oldstate),
              gst_element_state_get_name(state),
              gst_element_state_get_name(pending),
              gst_element_state_get_name(GST_STATE_TARGET(data->pipeline)),
              is_ours ? "" : "not ");

    /* leave now if we came here only for the trace */
    if(!is_ours)
    {
        UNLOCK_DATA(data);
        return;
    }

    if(state == oldstate)
    {
        /* Why, oh GStreamer, are you doing this to me? The fucking GstMessage
         * was clearly labeled GST_MESSAGE_STATE_CHANGED, so why for fucking
         * Christ's sake is the new state THE FUCKING SAME AS THE OLD STATE?!
         * If this is intended, then why is there not A SINGLE FUCKING WORD
         * ABOUT IT IN THE FUCKING API DOCUMENTATION? Why do I have to spend
         * DAYS (literally!) just to find this fuckery being the cause for our
         * various problems with skipping through streams? */
        if(state == GST_STATE_READY || state == GST_STATE_NULL)
        {
            UNLOCK_DATA(data);
            return;
        }
    }

    tdbussplayPlayback *dbus_playback_iface = dbus_get_playback_iface();

    switch(state)
    {
      case GST_STATE_NULL:
      case GST_STATE_READY:
        if(data->progress_watcher != 0)
        {
            g_source_remove(data->progress_watcher);
            data->progress_watcher = 0;
        }

        break;

      case GST_STATE_PAUSED:
        if((oldstate == GST_STATE_READY || oldstate == GST_STATE_NULL) &&
           pending == GST_STATE_PLAYING)
        {
            data->stream_has_just_started = true;
        }

        break;

      case GST_STATE_PLAYING:
      case GST_STATE_VOID_PENDING:
        break;
    }


    switch(GST_STATE_TARGET(data->pipeline))
    {
      case GST_STATE_READY:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to stop, but not there yet */
            break;
        }

        if(urlfifo_is_item_valid(&data->current_stream))
            emit_stopped(dbus_playback_iface, data);

        if(urlfifo_pop_item(&data->current_stream, true) < 0)
            urlfifo_free_item(&data->current_stream);

        data->stream_has_just_started = false;

        break;

      case GST_STATE_PAUSED:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to pause, but not there yet */
            break;
        }

        if(activate_stream(data, state) && dbus_playback_iface != NULL)
            tdbus_splay_playback_emit_paused(dbus_playback_iface,
                                             data->current_stream.id);

        break;

      case GST_STATE_PLAYING:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to play, but not there yet */
            break;
        }

        if(activate_stream(data, state) && dbus_playback_iface != NULL &&
           !data->stream_has_just_started)
            emit_now_playing(dbus_playback_iface, data);

        data->stream_has_just_started = false;

        if(data->progress_watcher == 0)
            data->progress_watcher = g_timeout_add(50, report_progress, data);

        break;

      case GST_STATE_VOID_PENDING:
      case GST_STATE_NULL:
        BUG("Ignoring state transition for bogus pipeline target %s",
            gst_element_state_get_name(GST_STATE_TARGET(data->pipeline)));
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
    memset(&sd->big_image, 0, sizeof(sd->big_image));
    memset(&sd->preview_image, 0, sizeof(sd->preview_image));
}

static void handle_start_of_stream(GstMessage *message,
                                   struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    LOCK_DATA(data);

    switch(data->current_stream.state)
    {
      case URLFIFO_ITEM_STATE_INVALID:
      case URLFIFO_ITEM_STATE_IN_QUEUE:
        BUG("Replace current by next in unexpected state %s",
            urlfifo_state_name(data->current_stream.state));
        log_assert(!urlfifo_is_empty());

        /* fall-through */

      case URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED:
      case URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT:
        if(urlfifo_pop_item(&data->current_stream, true) < 0)
            break;

        /* fall-through */

      case URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE:
        urlfifo_set_item_state(&data->current_stream,
                               URLFIFO_ITEM_STATE_ACTIVE);
        break;

      case URLFIFO_ITEM_STATE_ACTIVE:
        break;
    }

    struct stream_data *sd = item_data_get_nonconst(&data->current_stream);

    if(sd != NULL)
    {
        clear_current_meta_data(sd);
        invalidate_stream_position_information(data);
        query_seconds(gst_element_query_duration, data->pipeline,
                      &data->current_time.duration_s);
        emit_now_playing(dbus_get_playback_iface(), data);
    }

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

static void handle_clock_lost_message(GstMessage *message, struct streamer_data *data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    LOCK_DATA(data);

    static const char context[] = "clock lost";
    if(set_stream_state(data->pipeline, GST_STATE_PAUSED, context))
        set_stream_state(data->pipeline, GST_STATE_PLAYING, context);

    UNLOCK_DATA(data);
}

static void setup_source_element(GstElement *playbin,
                                 GstElement *source, gpointer user_data)
{
    struct streamer_data *data = user_data;

    if(data->is_failing)
        return;

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

      case GST_MESSAGE_CLOCK_LOST:
        handle_clock_lost_message(message, user_data);
        break;

      case GST_MESSAGE_NEW_CLOCK:
      case GST_MESSAGE_STREAM_STATUS:
      case GST_MESSAGE_RESET_TIME:
        /* these messages are not handled, and they are explicitly ignored */
        break;

      case GST_MESSAGE_UNKNOWN:
      case GST_MESSAGE_INFO:
      case GST_MESSAGE_STATE_DIRTY:
      case GST_MESSAGE_STEP_DONE:
      case GST_MESSAGE_CLOCK_PROVIDE:
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

static void try_play_next_stream(struct streamer_data *data,
                                 GstState next_state, const char *context)
{
    bool is_next_current;
    bool is_just_queued;
    struct urlfifo_item *const next_stream =
        try_take_next(data, true, &is_next_current, &is_just_queued, context);

    if(next_stream != NULL && (is_next_current || is_just_queued))
        play_next_stream(data, NULL, next_stream, next_state,
                         false, false, context);
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

static void disconnect_playbin_signals(void)
{
    if(streamer_data.pipeline == NULL)
        return;

    for(size_t i = 0;
        i < sizeof(streamer_data.signal_handler_ids) / sizeof(streamer_data.signal_handler_ids[0]);
        ++i)
    {
        g_signal_handler_disconnect(streamer_data.pipeline,
                                    streamer_data.signal_handler_ids[i]);
        streamer_data.signal_handler_ids[i] = 0;
    }
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

    gst_object_unref(GST_OBJECT(streamer_data.pipeline));
    streamer_data.pipeline = NULL;
}

static int rebuild_playbin(struct streamer_data *data, const char *context)
{
    disconnect_playbin_signals();

    /* allow signal handlers already waiting for the lock to pass */
    UNLOCK_DATA(data);
    g_usleep(500000);
    LOCK_DATA(data);

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

    disconnect_playbin_signals();
    set_stream_state(streamer_data.pipeline, GST_STATE_NULL, "shutdown");
    teardown_playbin();

    gst_object_unref(GST_OBJECT(streamer_data.system_clock));
    streamer_data.system_clock = NULL;

    urlfifo_free_item(&streamer_data.current_stream);
}

void streamer_activate(void)
{
    LOCK_DATA(&streamer_data);

    if(streamer_data.is_player_activated)
        BUG("Already activated");
    else
    {
        msg_info("Activated");
        streamer_data.is_player_activated = true;
    }

    UNLOCK_DATA(&streamer_data);
}

static bool do_stop(const char *context, const GstState pending,
                    bool *failed_hard)
{
    log_assert(streamer_data.pipeline != NULL);

    streamer_data.supposed_play_status = PLAY_STATUS_STOPPED;

    const GstState state = (pending == GST_STATE_VOID_PENDING)
        ? GST_STATE(streamer_data.pipeline)
        : pending;
    bool is_stream_state_unchanged = true;

    *failed_hard = false;

    switch(state)
    {
      case GST_STATE_PLAYING:
      case GST_STATE_PAUSED:
        if(set_stream_state(streamer_data.pipeline, GST_STATE_READY, context))
        {
            is_stream_state_unchanged = false;
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
                  "Stop: pipeline is in unhandled state %s",
                  gst_element_state_get_name(state));
        *failed_hard = true;
        break;
    }

    return is_stream_state_unchanged;
}

void streamer_deactivate(void)
{
    static const char context[] = "deactivate";

    LOCK_DATA(&streamer_data);

    if(!streamer_data.is_player_activated)
        BUG("Already deactivated");
    else
    {
        msg_info("Deactivating as requested");
        streamer_data.is_player_activated = false;

        const GstState pending = GST_STATE_PENDING(streamer_data.pipeline);
        bool dummy;
        do_stop(context, pending, &dummy);

        msg_info("Deactivated");
    }

    UNLOCK_DATA(&streamer_data);
}

static bool do_start(void)
{
    static const char context[] = "start playing";

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
            try_play_next_stream(&streamer_data, GST_STATE_PLAYING, context);
            break;

          case GST_STATE_PAUSED:
            set_stream_state(streamer_data.pipeline, GST_STATE_PLAYING, context);
            break;

          case GST_STATE_VOID_PENDING:
            msg_error(ENOSYS, LOG_ERR,
                      "Start: pipeline is in unhandled state %s",
                      gst_element_state_get_name(state));
            return false;
        }
    }

    return true;
}

bool streamer_start(void)
{
    LOCK_DATA(&streamer_data);

    bool retval;

    if(streamer_data.is_player_activated)
        retval = do_start();
    else
    {
        BUG("Start request while inactive");
        retval = false;
    }

    UNLOCK_DATA(&streamer_data);

    return retval;
}

bool streamer_stop(void)
{
    LOCK_DATA(&streamer_data);

    bool retval;

    if(!streamer_data.is_player_activated)
    {
        BUG("Stop request while inactive");
        retval = false;
    }
    else
    {
        static const char context[] = "stop playing";

        msg_info("Stopping as requested");

        const GstState pending = GST_STATE_PENDING(streamer_data.pipeline);
        const bool may_emit_stopped_with_error =
            do_stop(context, pending, &retval);

        if(may_emit_stopped_with_error &&
           (GST_STATE(streamer_data.pipeline) == GST_STATE_READY ||
            GST_STATE(streamer_data.pipeline) == GST_STATE_NULL) &&
           pending == GST_STATE_VOID_PENDING)
        {
            emit_stopped_with_error(dbus_get_playback_iface(), &streamer_data,
                                    STOPPED_REASON_ALREADY_STOPPED);
        }
    }

    UNLOCK_DATA(&streamer_data);

    return retval;
}

static bool do_pause(void)
{
    static const char context[] = "pause stream";

    msg_info("Pausing as requested");
    log_assert(streamer_data.pipeline != NULL);

    streamer_data.supposed_play_status = PLAY_STATUS_PAUSED;

    const GstState state = GST_STATE(streamer_data.pipeline);

    switch(state)
    {
      case GST_STATE_PAUSED:
        break;

      case GST_STATE_NULL:
        try_play_next_stream(&streamer_data, GST_STATE_PAUSED, context);
        break;

      case GST_STATE_READY:
      case GST_STATE_PLAYING:
        set_stream_state(streamer_data.pipeline, GST_STATE_PAUSED, context);
        break;

      case GST_STATE_VOID_PENDING:
        msg_error(ENOSYS, LOG_ERR,
                  "Pause: pipeline is in unhandled state %s",
                  gst_element_state_get_name(state));
        return false;
    }

    return true;
}

/*!
 * \bug Call it a bug in or a feature of GStreamer playbin, but the following
 *     is anyway inconvenient: pausing an internet stream for a long time
 *     causes skipping to the next stream in the FIFO when trying to resume.
 *     There is probably some buffer overflow and connection timeout involved,
 *     but playbin won't tell us. It is therefore not easy to determine if we
 *     should reconnect or really take the next URL when asked to.
 */
bool streamer_pause(void)
{
    LOCK_DATA(&streamer_data);

    bool retval;

    if(streamer_data.is_player_activated)
        retval = do_pause();
    else
    {
        BUG("Pause request while inactive");
        retval = false;
    }

    UNLOCK_DATA(&streamer_data);

    return retval;
}

/*!
 * Convert percentage to time in nanoseconds.
 *
 * Why not simply use GST_FORMAT_PERCENT? The answer is that it won't work with
 * our version of GStreamer. The elements don't support it, so we have to do it
 * by ourselves.
 */
static int64_t compute_position_from_percentage(const int64_t percentage,
                                                const uint64_t duration_ns)
{
    if(percentage <= GST_FORMAT_PERCENT_MAX)
        return (int64_t)gst_util_uint64_scale_int(duration_ns, percentage,
                                                  GST_FORMAT_PERCENT_MAX);

    msg_error(EINVAL, LOG_ERR, "Seek percentage value too large");
    return -1;
}

bool streamer_seek(int64_t position, const char *units)
{
    if(position < 0)
    {
        msg_error(EINVAL, LOG_ERR, "Negative seeks not supported");
        return false;
    }

    gint64 duration_ns;

    LOCK_DATA(&streamer_data);

    if(!streamer_data.is_player_activated)
    {
        BUG("Seek request while inactive");
        goto error_exit;
    }

    if(streamer_data.pipeline == NULL ||
       !gst_element_query_duration(streamer_data.pipeline,
                                   GST_FORMAT_TIME, &duration_ns) ||
       duration_ns < 0)
        duration_ns = INT64_MIN;

    if(duration_ns < 0)
    {
        msg_error(EINVAL, LOG_ERR, "Cannot seek, duration unknown");
        goto error_exit;
    }

    static const GstSeekFlags seek_flags =
        GST_SEEK_FLAG_FLUSH |
        GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST;

    if(strcmp(units, "%") == 0)
        position = compute_position_from_percentage(position, duration_ns);
    else if(strcmp(units, "s") == 0)
        position *= GST_SECOND;
    else if(strcmp(units, "ms") == 0)
        position *= GST_MSECOND;
    else if(strcmp(units, "us") == 0)
        position *= GST_USECOND;
    else if(strcmp(units, "ns") == 0)
    {
        /* position value is in nanoseconds already, nothing to do */
    }
    else
        position = INT64_MIN;

    if(position < 0)
    {
        if(position == INT64_MAX)
            msg_error(EINVAL, LOG_ERR, "Seek unit %s not supported", units);

        goto error_exit;
    }

    if(position > duration_ns)
    {
        msg_error(EINVAL, LOG_ERR,
                  "Seek position %" PRId64 " ns beyond EOS at %" PRId64 " ns",
                  position, duration_ns);
        goto error_exit;
    }

    msg_info("Seek to time %" PRId64 " ns", position);

    if(!gst_element_seek_simple(streamer_data.pipeline, GST_FORMAT_TIME,
                                seek_flags, position))
        goto error_exit;

    tdbus_splay_playback_emit_speed_changed(dbus_get_playback_iface(),
                                            streamer_data.current_stream.id,
                                            1.0);

    UNLOCK_DATA(&streamer_data);

    return true;

error_exit:
    UNLOCK_DATA(&streamer_data);

    return false;
}

static bool do_set_speed(struct streamer_data *data, double factor)
{
    LOCK_DATA(data);

    if(data->pipeline == NULL)
    {
        UNLOCK_DATA(data);
        msg_error(0, LOG_NOTICE, "Cannot set speed, have no active pipeline");
        return false;
    }

    gint64 position_ns;
    if(!gst_element_query_position(data->pipeline,
                                   GST_FORMAT_TIME, &position_ns) ||
       position_ns < 0)
    {
        UNLOCK_DATA(data);
        msg_error(0, LOG_ERR,
                  "Cannot set speed, failed querying stream position");
        return false;
    }

    static const GstSeekFlags seek_flags =
        GST_SEEK_FLAG_FLUSH |
        GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST |
#if GST_CHECK_VERSION(1, 5, 1)
        GST_SEEK_FLAG_TRICKMODE | GST_SEEK_FLAG_TRICKMODE_KEY_UNITS |
#else
        GST_SEEK_FLAG_SKIP |
#endif /* minimum version 1.5.1 */
        0;

    const bool success =
        gst_element_seek(data->pipeline, factor, GST_FORMAT_TIME, seek_flags,
                         GST_SEEK_TYPE_SET, position_ns,
                         GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    const stream_id_t id = data->current_stream.id;

    UNLOCK_DATA(data);

    if(!success)
        msg_error(0, LOG_ERR, "Failed setting speed");
    else
        tdbus_splay_playback_emit_speed_changed(dbus_get_playback_iface(), id, factor);

    return success;
}

bool streamer_fast_winding(double factor)
{
    msg_info("Setting playback speed to %f", factor);
    return do_set_speed(&streamer_data, factor);
}

bool streamer_fast_winding_stop(void)
{
    msg_info("Playing at regular speed");
    return do_set_speed(&streamer_data, 1.0);
}

static enum PlayStatus do_next(bool skip_only_if_not_stopped,
                               uint32_t *out_skipped_id, uint32_t *out_next_id)
{
    static const char context[] = "skip to next";

    msg_info("Next requested");
    log_assert(streamer_data.pipeline != NULL);

    if(skip_only_if_not_stopped)
        urlfifo_free_item(&streamer_data.current_stream);

    const bool is_dequeuing_permitted =
        (streamer_data.supposed_play_status != PLAY_STATUS_STOPPED || !skip_only_if_not_stopped);
    uint32_t skipped_id = urlfifo_is_item_valid(&streamer_data.current_stream)
        ? streamer_data.current_stream.id
        : UINT32_MAX;
    bool is_next_current = false;
    bool is_just_queued = false;
    struct urlfifo_item *next_stream =
        is_dequeuing_permitted
        ? try_take_next(&streamer_data, true, &is_next_current, &is_just_queued, context)
        : NULL;

    uint32_t next_id = UINT32_MAX;

    if(next_stream != NULL && !is_next_current)
    {
        switch(streamer_data.current_stream.state)
        {
          case URLFIFO_ITEM_STATE_INVALID:
          case URLFIFO_ITEM_STATE_IN_QUEUE:
            BUG("[%s] Wrong state %s of current straem",
                context, urlfifo_state_name(streamer_data.current_stream.state));
            break;

          case URLFIFO_ITEM_STATE_ABOUT_TO_ACTIVATE:
          case URLFIFO_ITEM_STATE_ACTIVE:
            /* mark current stream as to-be-skipped */
            urlfifo_set_item_state(&streamer_data.current_stream,
                                   URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED);
            break;

          case URLFIFO_ITEM_STATE_ABOUT_TO_PHASE_OUT:
          case URLFIFO_ITEM_STATE_ABOUT_TO_BE_SKIPPED:
            /* current stream is already being taken down, cannot do it again;
             * also, we cannot drop directly from URL FIFO because in the
             * meantime it may have been refilled */
            next_stream = NULL;
            skipped_id = UINT32_MAX;
            break;
        }
    }

    if(next_stream == NULL)
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

            if(play_next_stream(&streamer_data,
                                is_next_current ? NULL : &streamer_data.current_stream,
                                next_stream, next_state, true, false,
                                context))
                next_id = next_stream->id;
        }
    }

    if(out_skipped_id != NULL)
        *out_skipped_id = skipped_id;

    if(out_next_id != NULL)
        *out_next_id = next_id;

    return streamer_data.supposed_play_status;
}

enum PlayStatus streamer_next(bool skip_only_if_not_stopped,
                              uint32_t *out_skipped_id, uint32_t *out_next_id)
{
    LOCK_DATA(&streamer_data);

    enum PlayStatus retval;

    if(streamer_data.is_player_activated)
        retval =
            do_next(skip_only_if_not_stopped, out_skipped_id, out_next_id);
    else
    {
        BUG("Next request while inactive");
        retval = PLAY_STATUS_STOPPED;
    }

    UNLOCK_DATA(&streamer_data);

    return retval;
}

bool streamer_is_playing(void)
{
    return GST_STATE(streamer_data.pipeline) == GST_STATE_PLAYING;
}

bool streamer_get_current_stream_id(stream_id_t *id)
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

bool streamer_push_item(stream_id_t stream_id, GVariant *stream_key,
                        const char *stream_url, size_t keep_items)
{
    LOCK_DATA(&streamer_data);
    bool is_active = streamer_data.is_player_activated;
    UNLOCK_DATA(&streamer_data);

    if(!is_active)
    {
        BUG("Push request while inactive");
        return false;
    }

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

    memset(&sd->big_image, 0, sizeof(sd->big_image));
    memset(&sd->preview_image, 0, sizeof(sd->preview_image));

    return urlfifo_push_item(stream_id, stream_url, NULL, NULL,
                             keep_items, NULL, sd,
                             &streamer_urlfifo_item_data_ops) != 0;
}
