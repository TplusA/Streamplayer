/*
 * Copyright (C) 2015--2020  T+A elektroakustik GmbH & Co. KG
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

#include <sstream>
#include <cinttypes>
#include <cstring>
#include <unordered_set>

#include <gst/gst.h>
#include <gst/tag/tag.h>

#include "streamer.hh"
#include "urlfifo.hh"
#include "playitem.hh"
#include "dbus_iface_deep.hh"
#include "messages.h"

enum class StoppedReason
{
    /*! Reason not known. Should be used very rarely, if ever. */
    UNKNOWN,

    /*! Cannot play because URL FIFO is empty. */
    QUEUE_EMPTY,

    /*! Cannot stop because the player is already stopped. */
    ALREADY_STOPPED,

    /*! I/O error on physical medium (e.g., read error on some USB drive). */
    PHYSICAL_MEDIA_IO,

    /*! I/O error on the network (e.g., broken network connection). */
    NET_IO,

    /*! Have no URL. */
    URL_MISSING,

    /*! Network protocol error. */
    PROTOCOL,

    /*! Authentication with some external system has failed. */
    AUTHENTICATION,

    /*! Resource does not exist. */
    DOES_NOT_EXIST,

    /*! Resource has wrong type. */
    WRONG_TYPE,

    /*! Cannot access resource due to restricted permissions. */
    PERMISSION_DENIED,

    /*! Failed decoding stream because of a missing codec. */
    MISSING_CODEC,

    /*! Stream codec is known, but format wrong. */
    WRONG_STREAM_FORMAT,

    /*! Decoding failed. */
    BROKEN_STREAM,

    /*! Decryption key missing. */
    ENCRYPTED,

    /*! Cannot decrypt because this is not implemented/supported. */
    DECRYPTION_NOT_SUPPORTED,

    /*! Stable name for the highest-valued code. */
    LAST_VALUE = DECRYPTION_NOT_SUPPORTED,
};

struct time_data
{
    int64_t position_s;
    int64_t duration_s;
};

struct FailureData
{
    StoppedReason reason;
    bool clear_fifo_on_error;
    bool report_on_stream_stop;

    explicit FailureData():
        reason(StoppedReason::UNKNOWN),
        clear_fifo_on_error(false),
        report_on_stream_stop(false)
    {}

    explicit FailureData(StoppedReason sreason):
        reason(sreason),
        clear_fifo_on_error(false),
        report_on_stream_stop(false)
    {}

    explicit FailureData(bool report_on_stop):
        reason(StoppedReason::UNKNOWN),
        clear_fifo_on_error(false),
        report_on_stream_stop(report_on_stop)
    {}

    void reset()
    {
        reason = StoppedReason::UNKNOWN;
        clear_fifo_on_error = false;
        report_on_stream_stop = false;
    }
};

class BufferUnderrunFilter
{
  public:
    enum UpdateResult
    {
        EVERYTHING_IS_GOING_ACCORDING_TO_PLAN,
        UNDERRUN_DETECTED,
        FILLING_UP,
        RECOVERED,
    };

  private:
    static constexpr unsigned int RECOVERY_COUNT = 5;
    unsigned int recovered_count_;

  public:
    BufferUnderrunFilter(const BufferUnderrunFilter &) = delete;
    BufferUnderrunFilter(BufferUnderrunFilter &&) = default;
    BufferUnderrunFilter &operator=(const BufferUnderrunFilter &) = delete;
    BufferUnderrunFilter &operator=(BufferUnderrunFilter &&) = default;

    explicit BufferUnderrunFilter():
        recovered_count_(0)
    {}

    void reset() { recovered_count_ = 0; }

    UpdateResult update(uint8_t percent)
    {
        if(percent > 0)
        {
            if(recovered_count_ == 0)
                return EVERYTHING_IS_GOING_ACCORDING_TO_PLAN;

            --recovered_count_;

            return recovered_count_ == 0 ? RECOVERED : FILLING_UP;
        }
        else
        {
            recovered_count_ = RECOVERY_COUNT;
            return UNDERRUN_DETECTED;
        }
    }
};

class StreamerData
{
  private:
    mutable std::recursive_mutex lock_;

  public:
    bool is_player_activated;

    GstElement *pipeline;
    guint bus_watch;
    guint progress_watcher;
    guint soup_http_block_size;
    gulong signal_handler_ids[2];

    std::unique_ptr<PlayQueue::Queue<PlayQueue::Item>> url_fifo_LOCK_ME;

    /*!
     * The item currently played/paused/handled.
     *
     * The item is moved from the URL FIFO into this place using
     * #PlayQueue::Queue::pop() before the item is actually playing. Check
     * #PlayQueue::Item::get_state() to tell what is supposed to be done with
     * the item.
     */
    std::unique_ptr<PlayQueue::Item> current_stream;

    bool is_failing;
    FailureData fail;

    struct time_data previous_time;
    struct time_data current_time;

    GstClock *system_clock;
    bool is_tag_update_scheduled;
    GstClockTime next_allowed_tag_update_time;

    bool stream_has_just_started;
    BufferUnderrunFilter stream_buffer_underrun_filter;

    Streamer::PlayStatus supposed_play_status;

  public:
    StreamerData(const StreamerData &) = delete;
    StreamerData &operator=(const StreamerData &) = delete;

    explicit StreamerData():
        is_player_activated(false),
        pipeline(nullptr),
        bus_watch(0),
        progress_watcher(0),
        soup_http_block_size(0),
        signal_handler_ids{0, 0},
        url_fifo_LOCK_ME(std::make_unique<PlayQueue::Queue<PlayQueue::Item>>()),
        is_failing(false),
        previous_time{},
        current_time{},
        system_clock(nullptr),
        is_tag_update_scheduled(false),
        next_allowed_tag_update_time(0),
        stream_has_just_started(false),
        supposed_play_status(Streamer::PlayStatus::STOPPED)
    {}

    std::unique_lock<std::recursive_mutex> lock() const
    {
        return std::unique_lock<std::recursive_mutex>(lock_);
    }

    template <typename F>
    auto locked(F &&code) -> decltype(code(*this))
    {
        std::lock_guard<std::recursive_mutex> lk(lock_);
        return code(*this);
    }
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

static void invalidate_position_information(struct time_data *data)
{
    data->position_s = INT64_MAX;
    data->duration_s = INT64_MAX;
}

static inline void invalidate_stream_position_information(StreamerData &data)
{
    invalidate_position_information(&data.previous_time);
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
                         StreamerData &data)
{
    data.supposed_play_status = Streamer::PlayStatus::STOPPED;

    if(playback_iface != nullptr)
        tdbus_splay_playback_emit_stopped(dbus_get_playback_iface(),
                                          data.current_stream->stream_id_);
}

static void emit_stopped_with_error(tdbussplayPlayback *playback_iface,
                                    StreamerData &data,
                                    const PlayQueue::Queue<PlayQueue::Item> &url_fifo,
                                    StoppedReason reason)
{
    data.supposed_play_status = Streamer::PlayStatus::STOPPED;

    if(playback_iface == nullptr)
        return;

    /*!
     * String IDs that can be used as a reason as to why the stream was
     * stopped.
     *
     * Must be sorted according to values in #StoppedReason enumeration.
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

    static_assert(G_N_ELEMENTS(reasons) == size_t(StoppedReason::LAST_VALUE) + 1U,
                  "Array size mismatch");

    if(data.current_stream == nullptr)
        tdbus_splay_playback_emit_stopped_with_error(playback_iface, 0, "",
                                                     url_fifo.size() == 0,
                                                     reasons[size_t(reason)]);
    else
    {
        const auto *const failed_stream = data.current_stream.get();

        tdbus_splay_playback_emit_stopped_with_error(playback_iface,
                                                     failed_stream->stream_id_,
                                                     failed_stream->url_.c_str(),
                                                     url_fifo.size() == 0,
                                                     reasons[size_t(reason)]);
    }
}

static void disconnect_playbin_signals(StreamerData &data)
{
    if(data.pipeline == nullptr)
        return;

    for(size_t i = 0;
        i < sizeof(data.signal_handler_ids) / sizeof(data.signal_handler_ids[0]);
        ++i)
    {
        g_signal_handler_disconnect(data.pipeline, data.signal_handler_ids[i]);
        data.signal_handler_ids[i] = 0;
    }
}

static void teardown_playbin(StreamerData &data)
{
    if(data.pipeline == nullptr)
        return;

    g_source_remove(data.bus_watch);
    data.bus_watch = 0;

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(data.pipeline));
    log_assert(bus != nullptr);
    gst_object_unref(bus);

    gst_object_unref(GST_OBJECT(data.pipeline));
    data.pipeline = nullptr;
}

static int create_playbin(StreamerData &data, const char *context);

static int rebuild_playbin(StreamerData &data,
                           std::unique_lock<std::recursive_mutex> &data_lock,
                           const char *context)
{
    disconnect_playbin_signals(data);

    /* allow signal handlers already waiting for the lock to pass */
    data_lock.unlock();
    g_usleep(500000);
    data_lock.lock();

    set_stream_state(data.pipeline, GST_STATE_NULL, "rebuild");
    teardown_playbin(data);

    return create_playbin(data, context);
}


static void do_stop_pipeline_and_recover_from_error(StreamerData &data,
                                                    std::unique_lock<std::recursive_mutex> &data_lock,
                                                    PlayQueue::Queue<PlayQueue::Item> &url_fifo)
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
    rebuild_playbin(data, data_lock, context);

    msg_info("Stop reason is %d", int(data.fail.reason));

    if(data.fail.clear_fifo_on_error)
        url_fifo.clear(0);

    invalidate_stream_position_information(data);
    emit_stopped_with_error(dbus_get_playback_iface(), data, url_fifo,
                            data.fail.reason);

    data.stream_has_just_started = false;
    data.stream_buffer_underrun_filter.reset();
    data.is_failing = false;

    data.current_stream.reset();
    data.fail.reset();
}

static gboolean stop_pipeline_and_recover_from_error(gpointer user_data)
{
    msg_vinfo(MESSAGE_LEVEL_DIAG, "Recover from error");

    auto &data = *static_cast<StreamerData *>(user_data);
    auto data_lock(data.lock());

    data.url_fifo_LOCK_ME->locked_rw(
        [&data, &data_lock]
        (PlayQueue::Queue<PlayQueue::Item> &fifo)
        {
            do_stop_pipeline_and_recover_from_error(data, data_lock, fifo);
        });

    return G_SOURCE_REMOVE;
}

static void schedule_error_recovery(StreamerData &data, StoppedReason reason)
{
    data.is_failing = true;
    data.fail.reason = reason;
    data.fail.clear_fifo_on_error = false;

    g_idle_add(stop_pipeline_and_recover_from_error, &data);
}

static void recover_from_error_now_or_later(StreamerData &data,
                                            const FailureData &fdata)
{
    if(!fdata.report_on_stream_stop)
        schedule_error_recovery(data, fdata.reason);
    else
        data.fail = fdata;
}

static PlayQueue::Item *pick_next_item(StreamerData &data,
                                       PlayQueue::Queue<PlayQueue::Item> &url_fifo,
                                       bool &next_stream_is_in_fifo)
{
    if(data.current_stream != nullptr)
    {
        switch(data.current_stream->get_state())
        {
          case PlayQueue::ItemState::IN_QUEUE:
            next_stream_is_in_fifo = false;
            return data.current_stream.get();

          case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
          case PlayQueue::ItemState::ACTIVE:
          case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
          case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
            break;
        }
    }

    auto *const result = url_fifo.peek();
    next_stream_is_in_fifo = (result != nullptr);

    return result;
}

/*!
 * Return next item from queue, if any.
 *
 * The function takes a look at the URL FIFO. In case it is empty, this
 * function returns \c nullptr to indicate that there is no next item.
 * Depending on context, error recovery might be required to handle this case
 * (see parameter \p is_queued_item_expected).
 *
 * In case the URL FIFO is not empty, it will return a pointer to the head
 * element. The head element will remain in the URL FIFO in case the current
 * stream in \p data is still valid, otherwise the element will be removed from
 * the FIFO and its ownership is transferred to \p data as new current stream.
 * That is, the returned pointer will point either to a URL FIFO element or to
 * the current stream structure. See \p replaced_current_stream for how to
 * distinguish these two cases.
 *
 * \param data
 *     Streamer state data, also used for error recovery (see
 *     #schedule_error_recovery()).
 *
 * \param url_fifo
 *     The FIFO to take the current stream from.
 *
 * \param is_queued_item_expected
 *     If this parameter is \c true, then error recovery is scheduled in case
 *     the queue is empty. If it is \c false and the queue is empty, then
 *     nothing special happens and the function simply returns \c nullptr.
 *
 * \param[out] replaced_current_stream
 *     If this function returns a non-null pointer, then \c true is returned
 *     through this parameter in case the pointer points to the current stream
 *     structure in \p data, and \c false is returned in case the current
 *     stream was not changed and the pointer points directly to an item in the
 *     URL FIFO. If this function returns \c nullptr, then \c true is
 *     returned through this parameter in case the current stream has been
 *     replaced by the next item from the URL FIFO (which also will have been
 *     marked as failed), and \c false is returned in case the current stream
 *     has been marked as failed (if any).
 *
 * \param[out] current_stream_is_just_in_queue
 *     If this function returns a non-null pointer, then \c true is returned in
 *     case the currently active stream's state is equal to
 *     #PlayQueue::ItemState::IN_QUEUE. In all other cases, \c false is
 *     returned.
 *
 * \param context
 *     For better logs.
 *
 * \returns
 *     A pointer to the next stream information, or \c nullptr in case there is
 *     no such stream.
 */
static PlayQueue::Item *try_take_next(StreamerData &data,
                                      PlayQueue::Queue<PlayQueue::Item> &url_fifo,
                                      bool is_queued_item_expected,
                                      bool &replaced_current_stream,
                                      bool &current_stream_is_just_in_queue,
                                      const char *context)
{
    FailureData fdata(data.current_stream != nullptr);

    auto *const queued = url_fifo.peek();
    auto *next = pick_next_item(data, url_fifo, replaced_current_stream);

    current_stream_is_just_in_queue = false;

    if(next == nullptr)
    {
        if(!is_queued_item_expected)
            return nullptr;

        msg_info("[%s] Cannot dequeue, URL FIFO is empty", context);
        fdata.reason = StoppedReason::QUEUE_EMPTY;
    }
    else if(next->url_.empty())
    {
        msg_vinfo(MESSAGE_LEVEL_IMPORTANT,
                  "[%s] Cannot dequeue, URL in item is empty", context);
        fdata.reason = StoppedReason::URL_MISSING;
    }
    else
    {
        if(replaced_current_stream)
        {
            data.current_stream = url_fifo.pop();
            next = data.current_stream.get();
        }

        if(data.current_stream != nullptr)
        {
            switch(data.current_stream->get_state())
            {
              case PlayQueue::ItemState::IN_QUEUE:
                current_stream_is_just_in_queue = true;
                break;

              case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
              case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
              case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
              case PlayQueue::ItemState::ACTIVE:
                break;
            }
        }

        return next;
    }

    /* error, failure handling below */
    replaced_current_stream = data.current_stream != nullptr;

    if(replaced_current_stream || queued != nullptr)
    {
        if(replaced_current_stream)
            url_fifo.pop(data.current_stream);
        else
            url_fifo.pop();

        if(data.current_stream != nullptr && data.current_stream->fail())
            recover_from_error_now_or_later(data, fdata);
    }
    else
    {
        replaced_current_stream = false;
        schedule_error_recovery(data, fdata.reason);
    }

    return nullptr;
}

static bool play_next_stream(StreamerData &data,
                             PlayQueue::Item *replaced_stream,
                             PlayQueue::Item &next_stream,
                             GstState next_state, bool is_skipping,
                             bool is_prefetching_for_gapless, const char *context)
{
    switch(next_stream.get_state())
    {
      case PlayQueue::ItemState::ACTIVE:
      case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
      case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
        BUG("[%s] Unexpected stream state %s",
            context, PlayQueue::item_state_name(next_stream.get_state()));
        return false;

      case PlayQueue::ItemState::IN_QUEUE:
        next_stream.set_state(PlayQueue::ItemState::ABOUT_TO_ACTIVATE);
        break;

      case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
        /* already in the making, don't be so hasty */
        return true;
    }

    if(replaced_stream != nullptr)
        replaced_stream->set_state(is_skipping
                                   ? PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED
                                   : PlayQueue::ItemState::ABOUT_TO_PHASE_OUT);

    msg_info("Setting URL %s for next stream %u",
             next_stream.url_.c_str(), next_stream.stream_id_);

    g_object_set(data.pipeline, "uri", next_stream.url_.c_str(), nullptr);

    if(is_prefetching_for_gapless)
        return true;

    const bool retval = set_stream_state(data.pipeline, next_state, "play queued");

    if(retval)
        invalidate_stream_position_information(data);

    return retval;
}

/*
 * GLib signal callback.
 */
static void queue_stream_from_url_fifo(GstElement *elem, StreamerData &data)
{
    auto data_lock(data.lock());

    if(data.is_failing)
        return;

    auto fifo_lock(data.url_fifo_LOCK_ME->lock());
    static const char context[] = "need next stream";

    bool is_next_current;
    bool is_just_queued;
    auto *const next_stream =
        try_take_next(data, *data.url_fifo_LOCK_ME, false,
                      is_next_current, is_just_queued, context);

    if(data.current_stream == nullptr && next_stream == nullptr)
    {
        BUG("Having nothing to play, have nothing in queue, "
            "but GStreamer is asking for more");
        return;
    }

    if(next_stream == nullptr)
    {
        /* we are done here */
        data.current_stream->set_state(PlayQueue::ItemState::ABOUT_TO_PHASE_OUT);
        return;
    }

    play_next_stream(data, is_next_current ? nullptr : data.current_stream.get(),
                     *next_stream, GST_STATE_NULL, false, true, context);
}

static void handle_end_of_stream(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    msg_info("Finished playing all streams");

    auto data_lock(data.lock());

    if(set_stream_state(data.pipeline, GST_STATE_READY, "EOS"))
    {
        emit_stopped(dbus_get_playback_iface(), data);
        data.current_stream.reset();
    }
}

static void add_tuple_to_tags_variant_builder(const GstTagList *list,
                                              const gchar *tag,
                                              gpointer user_data)
{
    static const std::unordered_set<std::string> filtered_out =
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

    if(filtered_out.count(tag) != 0)
        return;

    auto *builder = static_cast<GVariantBuilder *>(user_data);
    const GValue *value = gst_tag_list_get_value_index(list, tag, 0);

    if(value == nullptr)
        return;

    if(G_VALUE_HOLDS_STRING(value))
        g_variant_builder_add(builder, "(ss)", tag, g_value_get_string(value));
    else if(G_VALUE_HOLDS_BOOLEAN(value))
        g_variant_builder_add(builder, "(ss)", tag,
                              g_value_get_boolean(value) ? "true" : "false");
    else if(G_VALUE_HOLDS_UINT(value) || G_VALUE_HOLDS_UINT64(value))
    {
        std::ostringstream os;

        if(G_VALUE_HOLDS_UINT(value))
            os << g_value_get_uint(value);
        else
            os << g_value_get_uint64(value);

        g_variant_builder_add(builder, "(ss)", tag, os.str().c_str());
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
    if(list != nullptr)
        gst_tag_list_foreach(list, add_tuple_to_tags_variant_builder, &builder);

    return g_variant_builder_end(&builder);
}

enum ImageTagType
{
    IMAGE_TAG_TYPE_NONE,
    IMAGE_TAG_TYPE_RAW_DATA,
    IMAGE_TAG_TYPE_URI,
};

static enum ImageTagType get_image_tag_type(const GstCaps *caps)
{
    if(caps == nullptr)
        return IMAGE_TAG_TYPE_NONE;

    for(size_t i = 0; /* nothing */; ++i)
    {
        const GstStructure *caps_struct = gst_caps_get_structure(caps, i);

        if(caps_struct == nullptr)
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
                                               PlayQueue::Item &item)
{
    GstBuffer *buffer = gst_sample_get_buffer(sample);

    if(buffer == nullptr)
        return;

    const GstStructure *sample_info = gst_sample_get_info(sample);
    GstTagImageType image_type;
    gint image_type_value;

    if(sample_info == nullptr ||
       !gst_structure_get_enum(sample_info, "image-type",
                               GST_TYPE_TAG_IMAGE_TYPE, &image_type_value))
        image_type = GST_TAG_IMAGE_TYPE_UNDEFINED;
    else
        image_type = static_cast<GstTagImageType>(image_type_value);

    static const std::array<const uint8_t, 19> prio_raise_table =
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

    if(image_type < 0 || (size_t)image_type >= prio_raise_table.size())
        return;

    const uint8_t priority = base_priority + prio_raise_table[image_type];

    auto &sd = item.get_stream_data();
    auto &sent_data(sd.get_image_sent_data(is_big_image));

    if(sent_data.priority > priority)
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

    if(sent_data.data == mi.data && sent_data.size == mi.size)
        return;

    sent_data.data = mi.data;
    sent_data.size = mi.size;
    sent_data.priority = priority;

    tdbus_artcache_write_call_add_image_by_data(dbus_artcache_get_write_iface(),
                                                GVariantWrapper::get(sd.stream_key_),
                                                priority,
                                                g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                                          mi.data, mi.size,
                                                                          sizeof(mi.data[0])),
                                                nullptr, nullptr, nullptr);

    gst_memory_unmap(memory, &mi);
}

static void update_picture_for_item(PlayQueue::Item &item,
                                    const GstTagList *tags)
{
    static const std::array<const std::pair<const char *const, const uint8_t>, 2> image_tags =
    {
        std::make_pair(GST_TAG_IMAGE,         150),
        std::make_pair(GST_TAG_PREVIEW_IMAGE, 120),
    };

    for(const auto &tag_and_prio : image_tags)
    {
        GstSample *sample = nullptr;

        if(!gst_tag_list_get_sample(tags, tag_and_prio.first, &sample))
            continue;

        GstCaps *caps = gst_sample_get_caps(sample);

        if(caps == nullptr)
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
            send_image_data_to_cover_art_cache(sample,
                                               &tag_and_prio == image_tags.begin(),
                                               tag_and_prio.second, item);
            break;

          case IMAGE_TAG_TYPE_URI:
            BUG("Embedded image tag is URI: not implemented");
            break;
        }

        gst_sample_unref(sample);
    }
}

static void emit_tags__unlocked(StreamerData &data)
{
    auto &sd = data.current_stream->get_stream_data();
    GVariant *meta_data = tag_list_to_g_variant(sd.get_tag_list());

    tdbus_splay_playback_emit_meta_data_changed(dbus_get_playback_iface(),
                                                data.current_stream->stream_id_,
                                                meta_data);

    data.next_allowed_tag_update_time =
        gst_clock_get_time(data.system_clock) + 500UL * GST_MSECOND;
    data.is_tag_update_scheduled = false;
}

static gboolean emit_tags(gpointer user_data)
{
    auto &data = *static_cast<StreamerData *>(user_data);
    auto data_lock(data.lock());

    if(data.current_stream != nullptr)
        emit_tags__unlocked(data);
    else
        data.is_tag_update_scheduled = false;

    return FALSE;
}

static void handle_tag(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    auto data_lock(data.lock());

    if(data.current_stream == nullptr)
        return;

    GstTagList *tags = nullptr;
    gst_message_parse_tag(message, &tags);

    update_picture_for_item(*data.current_stream, tags);
    data.current_stream->get_stream_data().merge_tag_list(tags);

    gst_tag_list_unref(tags);

    if(data.is_tag_update_scheduled)
        return;

    GstClockTime now = gst_clock_get_time(data.system_clock);
    GstClockTimeDiff cooldown = GST_CLOCK_DIFF(now, data.next_allowed_tag_update_time);

    if(cooldown <= 0L)
        emit_tags__unlocked(data);
    else
    {
        g_timeout_add(GST_TIME_AS_MSECONDS(cooldown), emit_tags, &data);
        data.is_tag_update_scheduled = true;
    }
}

static void emit_now_playing(tdbussplayPlayback *playback_iface,
                             const StreamerData &data,
                             const PlayQueue::Queue<PlayQueue::Item> &url_fifo)
{
    if(playback_iface == nullptr || data.current_stream == nullptr)
        return;

    const auto &sd = data.current_stream->get_stream_data();
    GVariant *meta_data = tag_list_to_g_variant(sd.get_tag_list());

    tdbus_splay_playback_emit_now_playing(playback_iface,
                                          data.current_stream->stream_id_,
                                          GVariantWrapper::get(sd.stream_key_),
                                          data.current_stream->url_.c_str(),
                                          url_fifo.full(), meta_data);
}

static StoppedReason core_error_to_stopped_reason(GstCoreError code,
                                                  bool is_local_error)
{
    switch(code)
    {
      case GST_CORE_ERROR_MISSING_PLUGIN:
      case GST_CORE_ERROR_DISABLED:
        return StoppedReason::MISSING_CODEC;

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

    return StoppedReason::UNKNOWN;
}

static StoppedReason library_error_to_stopped_reason(GstLibraryError code,
                                                     bool is_local_error)
{
    BUG("Failed to convert GstLibraryError code %d to reason code", code);
    return StoppedReason::UNKNOWN;
}

static StoppedReason resource_error_to_stopped_reason(GstResourceError code,
                                                      bool is_local_error)
{
    switch(code)
    {
      case GST_RESOURCE_ERROR_NOT_FOUND:
        return StoppedReason::DOES_NOT_EXIST;

      case GST_RESOURCE_ERROR_OPEN_READ:
        return is_local_error
            ? StoppedReason::PHYSICAL_MEDIA_IO
            : StoppedReason::NET_IO;

      case GST_RESOURCE_ERROR_READ:
      case GST_RESOURCE_ERROR_SEEK:
        return StoppedReason::PROTOCOL;

      case GST_RESOURCE_ERROR_NOT_AUTHORIZED:
        return StoppedReason::PERMISSION_DENIED;

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

    return StoppedReason::UNKNOWN;
}

static StoppedReason stream_error_to_stopped_reason(GstStreamError code,
                                                    bool is_local_error)
{
    switch(code)
    {
      case GST_STREAM_ERROR_FAILED:
      case GST_STREAM_ERROR_TYPE_NOT_FOUND:
      case GST_STREAM_ERROR_WRONG_TYPE:
        return StoppedReason::WRONG_TYPE;

      case GST_STREAM_ERROR_CODEC_NOT_FOUND:
        return StoppedReason::MISSING_CODEC;

      case GST_STREAM_ERROR_DECODE:
      case GST_STREAM_ERROR_DEMUX:
        return StoppedReason::BROKEN_STREAM;

      case GST_STREAM_ERROR_FORMAT:
        return StoppedReason::WRONG_STREAM_FORMAT;

      case GST_STREAM_ERROR_DECRYPT:
        return StoppedReason::DECRYPTION_NOT_SUPPORTED;

      case GST_STREAM_ERROR_DECRYPT_NOKEY:
        return StoppedReason::ENCRYPTED;

      case GST_STREAM_ERROR_TOO_LAZY:
      case GST_STREAM_ERROR_NOT_IMPLEMENTED:
      case GST_STREAM_ERROR_ENCODE:
      case GST_STREAM_ERROR_MUX:
      case GST_STREAM_ERROR_NUM_ERRORS:
        break;
    }

    BUG("Failed to convert GstStreamError code %d to reason code", code);

    return StoppedReason::UNKNOWN;
}

static StoppedReason gerror_to_stopped_reason(GError *error, bool is_local_error)
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

    return StoppedReason::UNKNOWN;
}

static bool determine_is_local_error_by_url(const PlayQueue::Item &item)
{
    if(item.url_.empty())
        return true;

#if GST_CHECK_VERSION(1, 5, 1)
    GstUri *uri = gst_uri_from_string(item.url_.c_str());

    if(uri == nullptr)
        return true;

    static const std::string file_scheme("file");

    const char *scheme = gst_uri_get_scheme(uri);
    const bool retval = (scheme == nullptr || scheme == file_scheme);

    gst_uri_unref(uri);

    return retval;
#else /* pre 1.5.1 */
    static const char protocol_prefix[] = "file://";

    return item.url_.compare(0, sizeof(protocol_prefix) - 1,
                             protocol_prefix) == 0;
#endif /* use GstUri if not older than v1.5.1 */
}

static PlayQueue::Item *get_failed_item(StreamerData &data,
                                        PlayQueue::Queue<PlayQueue::Item> &url_fifo)
{
    if(data.current_stream == nullptr)
        return nullptr;

    switch(data.current_stream->get_state())
    {
      case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
      case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
        if(!url_fifo.pop(data.current_stream))
            return nullptr;

        break;

      case PlayQueue::ItemState::IN_QUEUE:
      case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
      case PlayQueue::ItemState::ACTIVE:
        break;
    }

    return data.current_stream.get();
}

static void handle_error_message(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    GError *error = nullptr;
    gchar *debug = nullptr;

    gst_message_parse_error(message, &error, &debug);

    auto data_lock(data.lock());

    PlayQueue::Item *const failed_stream =
        data.url_fifo_LOCK_ME->locked_rw(
            [&data] (PlayQueue::Queue<PlayQueue::Item> &fifo)
            {
                return get_failed_item(data, fifo);
            });

    if(failed_stream == nullptr)
        BUG("Supposed to handle error, but have no item");
    else
    {
        const bool is_local_error = determine_is_local_error_by_url(*failed_stream);

        GstElement *source_elem;
        g_object_get(data.pipeline, "source", &source_elem, nullptr);

        const FailureData fdata(gerror_to_stopped_reason(error, is_local_error));

        msg_error(0, LOG_ERR, "ERROR code %d, domain %s from \"%s\"",
                  error->code, g_quark_to_string(error->domain),
                  GST_MESSAGE_SRC_NAME(message));
        msg_error(0, LOG_ERR, "ERROR message: %s", error->message);
        msg_error(0, LOG_ERR, "ERROR debug: %s", debug);
        msg_error(0, LOG_ERR, "ERROR mapped to stop reason %d, reporting %s",
                  int(fdata.reason),
                  fdata.report_on_stream_stop ? "on stop" : "now");

        if(failed_stream->fail())
            recover_from_error_now_or_later(data, fdata);
    }

    g_free(debug);
    g_error_free(error);
}

static void handle_warning_message(GstMessage *message)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    GError *error = nullptr;
    gchar *debug = nullptr;

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
                          GstElement *element, int64_t &seconds)
{
    seconds = -1;

    gint64 t_ns;

    if(!query(element, GST_FORMAT_TIME, &t_ns))
        return;

    if(t_ns < 0)
        return;

    /*
     * Rounding: simple cut to whole seconds, no arithmetic rounding.
     */
    seconds = t_ns / (1000LL * 1000LL * 1000LL);
}

/*!
 * GLib callback.
 *
 * \bug There is a bug in GStreamer that leads to the wrong position being
 *     displayed in pause mode for internet streams. How to trigger: play some
 *     URL, then pause; skip to next URL; the position queried from the playbin
 *     pipeline is still the paused time, but should be 0.
 */
static gboolean report_progress(gpointer user_data)
{
    auto &data = *static_cast<StreamerData *>(user_data);
    auto data_lock(data.lock());

    if(data.current_stream == nullptr)
    {
        data.progress_watcher = 0;
        return G_SOURCE_REMOVE;
    }

    const GstState state = GST_STATE(data.pipeline);

    switch(state)
    {
      case GST_STATE_PLAYING:
      case GST_STATE_PAUSED:
        query_seconds(gst_element_query_position, data.pipeline,
                      data.current_time.position_s);
        break;

      case GST_STATE_READY:
      case GST_STATE_NULL:
      case GST_STATE_VOID_PENDING:
        data.current_time.position_s = INT64_MAX;
        break;
    }

    if(data.current_time.position_s != data.previous_time.position_s ||
       data.current_time.duration_s != data.previous_time.duration_s)
    {
        data.previous_time = data.current_time;

        tdbussplayPlayback *playback_iface = dbus_get_playback_iface();

        if(playback_iface != nullptr)
            tdbus_splay_playback_emit_position_changed(playback_iface,
                                                       data.current_stream->stream_id_,
                                                       data.current_time.position_s, "s",
                                                       data.current_time.duration_s, "s");
    }

    return G_SOURCE_CONTINUE;
}

static bool activate_stream(const StreamerData &data, GstState pipeline_state)
{
    if(data.current_stream == nullptr)
    {
        BUG("Current item is invalid, switched to %s",
            gst_element_state_get_name(pipeline_state));
        return false;
    }

    switch(data.current_stream->get_state())
    {
      case PlayQueue::ItemState::ACTIVE:
        return true;

      case PlayQueue::ItemState::IN_QUEUE:
        BUG("Unexpected state %s for stream switched to %s",
            PlayQueue::item_state_name(data.current_stream->get_state()),
            gst_element_state_get_name(pipeline_state));

        data.current_stream->set_state(PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED);

        /* fall-through */

      case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
      case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
        break;

      case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
        data.current_stream->set_state(PlayQueue::ItemState::ACTIVE);
        return true;
    }

    return false;
}

static void handle_stream_state_change(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    auto data_lock(data.lock());

    const bool is_ours =
        (GST_MESSAGE_SRC(message) == GST_OBJECT(data.pipeline));

    if(!is_ours && !msg_is_verbose(MESSAGE_LEVEL_TRACE))
        return;

    GstState oldstate, state, pending;
    gst_message_parse_state_changed(message, &oldstate, &state, &pending);

    msg_vinfo(MESSAGE_LEVEL_TRACE,
              "State change on %s \"%s\": state %s -> %s, pending %s, target %s (%sours)",
              G_OBJECT_TYPE_NAME(GST_MESSAGE_SRC(message)),
              GST_MESSAGE_SRC_NAME(message),
              gst_element_state_get_name(oldstate),
              gst_element_state_get_name(state),
              gst_element_state_get_name(pending),
              gst_element_state_get_name(GST_STATE_TARGET(data.pipeline)),
              is_ours ? "" : "not ");

    /* leave now if we came here only for the trace */
    if(!is_ours)
        return;

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
            return;
    }

    tdbussplayPlayback *dbus_playback_iface = dbus_get_playback_iface();

    switch(state)
    {
      case GST_STATE_NULL:
      case GST_STATE_READY:
        if(data.progress_watcher != 0)
        {
            g_source_remove(data.progress_watcher);
            data.progress_watcher = 0;
        }

        break;

      case GST_STATE_PAUSED:
        if((oldstate == GST_STATE_READY || oldstate == GST_STATE_NULL) &&
           pending == GST_STATE_PLAYING)
        {
            data.stream_has_just_started = true;
            data.stream_buffer_underrun_filter.reset();
        }

        break;

      case GST_STATE_PLAYING:
      case GST_STATE_VOID_PENDING:
        break;
    }


    switch(GST_STATE_TARGET(data.pipeline))
    {
      case GST_STATE_READY:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to stop, but not there yet */
            break;
        }

        if(data.current_stream != nullptr)
            emit_stopped(dbus_playback_iface, data);

        data.url_fifo_LOCK_ME->locked_rw(
            [&data] (PlayQueue::Queue<PlayQueue::Item> &fifo)
            {
                if(!fifo.pop(data.current_stream))
                    data.current_stream.reset();
            });

        data.stream_has_just_started = false;

        break;

      case GST_STATE_PAUSED:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to pause, but not there yet */
            break;
        }

        if(activate_stream(data, state) && dbus_playback_iface != nullptr)
            tdbus_splay_playback_emit_paused(dbus_playback_iface,
                                             data.current_stream->stream_id_);

        break;

      case GST_STATE_PLAYING:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to play, but not there yet */
            break;
        }

        if(activate_stream(data, state) && dbus_playback_iface != nullptr &&
           !data.stream_has_just_started)
        {
            data.url_fifo_LOCK_ME->locked_ro(
                [&dbus_playback_iface, &data]
                (const PlayQueue::Queue<PlayQueue::Item> &fifo)
                {
                    emit_now_playing(dbus_playback_iface, data, fifo);
                });
        }

        data.stream_has_just_started = false;

        if(data.progress_watcher == 0)
            data.progress_watcher = g_timeout_add(50, report_progress, &data);

        break;

      case GST_STATE_VOID_PENDING:
      case GST_STATE_NULL:
        BUG("Ignoring state transition for bogus pipeline target %s",
            gst_element_state_get_name(GST_STATE_TARGET(data.pipeline)));
        break;
    }
}

static void handle_start_of_stream(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    auto data_lock(data.lock());
    auto fifo_lock(data.url_fifo_LOCK_ME->lock());

    bool failed = false;
    bool with_bug = false;
    bool need_activation = true;

    if(data.current_stream == nullptr)
        failed = with_bug = true;
    else
    {
        switch(data.current_stream->get_state())
        {
          case PlayQueue::ItemState::IN_QUEUE:
            with_bug = true;

            /* fall-through */

          case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
          case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
            failed = true;

            /* fall-through */

          case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
            break;

          case PlayQueue::ItemState::ACTIVE:
            need_activation = false;
            break;
        }
    }

    if(with_bug)
    {
        if(data.current_stream == nullptr)
            BUG("Replace nullptr current by next");
        else
            BUG("Replace current by next in unexpected state %s",
                PlayQueue::item_state_name(data.current_stream->get_state()));

        log_assert(!data.url_fifo_LOCK_ME->empty());
    }

    if(failed && !data.url_fifo_LOCK_ME->pop(data.current_stream))
        need_activation = false;

    if(need_activation)
        data.current_stream->set_state(PlayQueue::ItemState::ACTIVE);

    if(data.current_stream != nullptr)
    {
        auto &sd = data.current_stream->get_stream_data();

        sd.clear_meta_data();
        invalidate_stream_position_information(data);
        query_seconds(gst_element_query_duration, data.pipeline,
                      data.current_time.duration_s);
        emit_now_playing(dbus_get_playback_iface(), data,
                         *data.url_fifo_LOCK_ME);
    }
}

static void handle_buffering(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    gint percent = -1;
    gst_message_parse_buffering(message, &percent);

    if(percent < 0 || percent > 100)
    {
        msg_error(ERANGE, LOG_NOTICE, "Buffering percentage is %d", percent);
        return;
    }

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Buffer level: %d%%", percent);

    switch(data.stream_buffer_underrun_filter.update(percent))
    {
      case BufferUnderrunFilter::EVERYTHING_IS_GOING_ACCORDING_TO_PLAN:
        break;

      case BufferUnderrunFilter::UNDERRUN_DETECTED:
        msg_error(0, LOG_WARNING, "Buffer underrun detected");
        break;

      case BufferUnderrunFilter::FILLING_UP:
        msg_info("Buffer filling up (%d%%)", percent);
        break;

      case BufferUnderrunFilter::RECOVERED:
        msg_info("Buffer recovered (%d%%)", percent);
        break;
    }
}

static void handle_stream_duration(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    data.locked([] (StreamerData &d)
    {
        query_seconds(gst_element_query_duration, d.pipeline,
                      d.current_time.duration_s);
    });
}

static void handle_stream_duration_async(GstMessage *message, StreamerData &data)
{
    GstClockTime running_time;
    gst_message_parse_async_done(message, &running_time);

    auto data_lock(data.lock());

    if(running_time != GST_CLOCK_TIME_NONE)
        data.current_time.duration_s = running_time / (1000LL * 1000LL * 1000LL);
    else
        query_seconds(gst_element_query_duration, data.pipeline,
                      data.current_time.duration_s);
}

static void handle_clock_lost_message(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    auto data_lock(data.lock());

    static const char context[] = "clock lost";

    if(set_stream_state(data.pipeline, GST_STATE_PAUSED, context))
        set_stream_state(data.pipeline, GST_STATE_PLAYING, context);
}

/*
 * GLib signal callback.
 */
static void setup_source_element(GstElement *playbin,
                                 GstElement *source, gpointer user_data)
{
    const auto &data = *static_cast<const StreamerData *>(user_data);

    if(data.is_failing)
        return;

    static const std::string soup_name("GstSoupHTTPSrc");

    if(G_OBJECT_TYPE_NAME(source) == soup_name)
        g_object_set(source, "blocksize", &data.soup_http_block_size, nullptr);
}

/*
 * GStreamer callback.
 */
static gboolean bus_message_handler(GstBus *bus, GstMessage *message,
                                    gpointer user_data)
{
    auto &data = *static_cast<StreamerData *>(user_data);

    switch(GST_MESSAGE_TYPE(message))
    {
      case GST_MESSAGE_EOS:
        handle_end_of_stream(message, data);
        break;

      case GST_MESSAGE_TAG:
        handle_tag(message, data);
        break;

      case GST_MESSAGE_STATE_CHANGED:
        handle_stream_state_change(message, data);
        break;

      case GST_MESSAGE_STREAM_START:
        handle_start_of_stream(message, data);
        break;

      case GST_MESSAGE_BUFFERING:
        handle_buffering(message, data);
        break;

      case GST_MESSAGE_DURATION_CHANGED:
        handle_stream_duration(message, data);
        break;

      case GST_MESSAGE_ASYNC_DONE:
        handle_stream_duration_async(message, data);
        break;

      case GST_MESSAGE_ERROR:
        handle_error_message(message, data);
        break;

      case GST_MESSAGE_WARNING:
        handle_warning_message(message);
        break;

      case GST_MESSAGE_CLOCK_LOST:
        handle_clock_lost_message(message, data);
        break;

      case GST_MESSAGE_NEW_CLOCK:
      case GST_MESSAGE_STREAM_STATUS:
      case GST_MESSAGE_RESET_TIME:
      case GST_MESSAGE_ELEMENT:
      case GST_MESSAGE_LATENCY:
      case GST_MESSAGE_NEED_CONTEXT:
      case GST_MESSAGE_HAVE_CONTEXT:
        /* these messages are not handled, and they are explicitly ignored */
        break;

      case GST_MESSAGE_UNKNOWN:
      case GST_MESSAGE_INFO:
      case GST_MESSAGE_STATE_DIRTY:
      case GST_MESSAGE_STEP_DONE:
      case GST_MESSAGE_CLOCK_PROVIDE:
      case GST_MESSAGE_STRUCTURE_CHANGE:
      case GST_MESSAGE_APPLICATION:
      case GST_MESSAGE_SEGMENT_START:
      case GST_MESSAGE_SEGMENT_DONE:
      case GST_MESSAGE_ASYNC_START:
      case GST_MESSAGE_REQUEST_STATE:
      case GST_MESSAGE_STEP_START:
      case GST_MESSAGE_QOS:
      case GST_MESSAGE_PROGRESS:
      case GST_MESSAGE_TOC:
      case GST_MESSAGE_ANY:
#if GST_CHECK_VERSION(1, 5, 1)
      case GST_MESSAGE_EXTENDED:
      case GST_MESSAGE_DEVICE_ADDED:
      case GST_MESSAGE_DEVICE_REMOVED:
#endif /* v1.5.1 */
#if GST_CHECK_VERSION(1, 10, 0)
      case GST_MESSAGE_PROPERTY_NOTIFY:
      case GST_MESSAGE_STREAM_COLLECTION:
      case GST_MESSAGE_STREAMS_SELECTED:
      case GST_MESSAGE_REDIRECT:
#endif /* v1.10 */
#if GST_CHECK_VERSION(1, 16, 0)
      case GST_MESSAGE_DEVICE_CHANGED:
#endif /* v1.16 */
#if GST_CHECK_VERSION(1, 18, 0)
      case GST_MESSAGE_INSTANT_RATE_REQUEST:
#endif /* v1.16 */
        BUG("UNHANDLED MESSAGE TYPE %s from %s",
            GST_MESSAGE_TYPE_NAME(message), GST_MESSAGE_SRC_NAME(message));
        break;
    }

    return G_SOURCE_CONTINUE;
}

static int create_playbin(StreamerData &data, const char *context)
{
    data.pipeline = gst_element_factory_make("playbin", "play");
    data.bus_watch = 0;

    if(data.pipeline == nullptr)
        return -1;

    gst_object_ref(GST_OBJECT(data.pipeline));

    data.bus_watch = gst_bus_add_watch(GST_ELEMENT_BUS(data.pipeline),
                                       bus_message_handler, &data);

    g_object_set(data.pipeline, "flags", GST_PLAY_FLAG_AUDIO, nullptr);

    data.signal_handler_ids[0] =
        g_signal_connect(data.pipeline, "about-to-finish",
                         G_CALLBACK(queue_stream_from_url_fifo), &data);

    data.signal_handler_ids[1] =
        g_signal_connect(data.pipeline, "source-setup",
                         G_CALLBACK(setup_source_element), &data);

    set_stream_state(data.pipeline, GST_STATE_READY, context);

    return 0;
}

static void try_play_next_stream(StreamerData &data,
                                 PlayQueue::Queue<PlayQueue::Item> &url_fifo,
                                 GstState next_state, const char *context)
{
    bool is_next_current;
    bool is_just_queued;
    PlayQueue::Item *const next_stream =
        try_take_next(data, url_fifo, true, is_next_current, is_just_queued, context);

    if(next_stream != nullptr && (is_next_current || is_just_queued))
        play_next_stream(data, nullptr, *next_stream, next_state,
                         false, false, context);
}

static bool do_stop(StreamerData &data, const char *context,
                    const GstState pending, bool &failed_hard)
{
    log_assert(data.pipeline != nullptr);

    data.supposed_play_status = Streamer::PlayStatus::STOPPED;

    const GstState state = (pending == GST_STATE_VOID_PENDING)
        ? GST_STATE(data.pipeline)
        : pending;
    bool is_stream_state_unchanged = true;

    failed_hard = false;

    switch(state)
    {
      case GST_STATE_PLAYING:
      case GST_STATE_PAUSED:
        if(set_stream_state(data.pipeline, GST_STATE_READY, context))
        {
            is_stream_state_unchanged = false;

            data.url_fifo_LOCK_ME->locked_rw(
                [] (PlayQueue::Queue<PlayQueue::Item> &fifo) { fifo.clear(0); });
        }
        else
            data.fail.clear_fifo_on_error = true;

        break;

      case GST_STATE_READY:
      case GST_STATE_NULL:
        data.url_fifo_LOCK_ME->locked_rw(
            [] (PlayQueue::Queue<PlayQueue::Item> &fifo) { fifo.clear(0); });
        break;

      case GST_STATE_VOID_PENDING:
        msg_error(ENOSYS, LOG_ERR,
                  "Stop: pipeline is in unhandled state %s",
                  gst_element_state_get_name(state));
        failed_hard = true;
        break;
    }

    return is_stream_state_unchanged;
}

static bool do_set_speed(StreamerData &data, double factor)
{
    if(data.pipeline == nullptr)
    {
        msg_error(0, LOG_NOTICE, "Cannot set speed, have no active pipeline");
        return false;
    }

    gint64 position_ns;
    if(!gst_element_query_position(data.pipeline,
                                   GST_FORMAT_TIME, &position_ns) ||
       position_ns < 0)
    {
        msg_error(0, LOG_ERR,
                  "Cannot set speed, failed querying stream position");
        return false;
    }

    static const auto seek_flags =
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                  GST_SEEK_FLAG_KEY_UNIT |
                                  GST_SEEK_FLAG_SNAP_NEAREST |
#if GST_CHECK_VERSION(1, 5, 1)
                                  GST_SEEK_FLAG_TRICKMODE |
                                  GST_SEEK_FLAG_TRICKMODE_KEY_UNITS |
#else
                                  GST_SEEK_FLAG_SKIP |
#endif /* minimum version 1.5.1 */
                                  0);

    const bool success =
        gst_element_seek(data.pipeline, factor, GST_FORMAT_TIME, seek_flags,
                         GST_SEEK_TYPE_SET, position_ns,
                         GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

    if(!success)
        msg_error(0, LOG_ERR, "Failed setting speed");
    else if(data.current_stream != nullptr)
        tdbus_splay_playback_emit_speed_changed(dbus_get_playback_iface(),
                                                data.current_stream->stream_id_,
                                                factor);

    return success;
}

static StreamerData streamer_data;

int Streamer::setup(GMainLoop *loop, guint soup_http_block_size,
                    PlayQueue::Queue<PlayQueue::Item> &url_fifo)
{
    streamer_data.soup_http_block_size = soup_http_block_size;

    if(create_playbin(streamer_data, "setup") < 0)
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

void Streamer::shutdown(GMainLoop *loop)
{
    if(loop == nullptr)
        return;

    g_main_loop_unref(loop);

    disconnect_playbin_signals(streamer_data);
    set_stream_state(streamer_data.pipeline, GST_STATE_NULL, "shutdown");
    teardown_playbin(streamer_data);

    gst_object_unref(GST_OBJECT(streamer_data.system_clock));
    streamer_data.system_clock = nullptr;

    streamer_data.current_stream.reset();
}

void Streamer::activate()
{
    auto data_lock(streamer_data.lock());

    if(streamer_data.is_player_activated)
        BUG("Already activated");
    else
    {
        msg_info("Activated");
        streamer_data.is_player_activated = true;
    }
}

void Streamer::deactivate()
{
    static const char context[] = "deactivate";

    auto data_lock(streamer_data.lock());

    if(!streamer_data.is_player_activated)
        BUG("Already deactivated");
    else
    {
        msg_info("Deactivating as requested");
        streamer_data.is_player_activated = false;

        const GstState pending = GST_STATE_PENDING(streamer_data.pipeline);
        bool dummy;
        do_stop(streamer_data, context, pending, dummy);

        msg_info("Deactivated");
    }
}

bool Streamer::start()
{
    auto data_lock(streamer_data.lock());

    if(!streamer_data.is_player_activated)
    {
        BUG("Start request while inactive");
        return false;
    }

    static const char context[] = "start playing";

    log_assert(streamer_data.pipeline != nullptr);

    streamer_data.supposed_play_status = Streamer::PlayStatus::PLAYING;

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
            streamer_data.url_fifo_LOCK_ME->locked_rw(
                [] (PlayQueue::Queue<PlayQueue::Item> &fifo)
                {
                    try_play_next_stream(streamer_data, fifo,
                                         GST_STATE_PLAYING, context);
                });
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

bool Streamer::stop(const char *reason)
{
    auto data_lock(streamer_data.lock());

    if(!streamer_data.is_player_activated)
    {
        BUG("Stop request while inactive (%s)", reason);
        return false;
    }

    static const char context[] = "stop playing";

    msg_info("Stopping as requested (%s)", reason);

    const GstState pending = GST_STATE_PENDING(streamer_data.pipeline);
    bool retval;
    const bool may_emit_stopped_with_error = do_stop(streamer_data, context,
                                                     pending, retval);

    if(may_emit_stopped_with_error &&
       (GST_STATE(streamer_data.pipeline) == GST_STATE_READY ||
        GST_STATE(streamer_data.pipeline) == GST_STATE_NULL) &&
       pending == GST_STATE_VOID_PENDING)
    {
        streamer_data.url_fifo_LOCK_ME->locked_ro(
            []
            (const PlayQueue::Queue<PlayQueue::Item> &fifo)
            {
                emit_stopped_with_error(dbus_get_playback_iface(), streamer_data,
                                        fifo, StoppedReason::ALREADY_STOPPED);
            });
    }

    return retval;
}

/*!
 * \bug Call it a bug in or a feature of GStreamer playbin, but the following
 *     is anyway inconvenient: pausing an internet stream for a long time
 *     causes skipping to the next stream in the FIFO when trying to resume.
 *     There is probably some buffer overflow and connection timeout involved,
 *     but playbin won't tell us. It is therefore not easy to determine if we
 *     should reconnect or really take the next URL when asked to.
 */
bool Streamer::pause()
{
    auto data_lock(streamer_data.lock());

    if(!streamer_data.is_player_activated)
    {
        BUG("Pause request while inactive");
        return false;
    }

    static const char context[] = "pause stream";

    msg_info("Pausing as requested");
    log_assert(streamer_data.pipeline != nullptr);

    streamer_data.supposed_play_status = Streamer::PlayStatus::PAUSED;

    const GstState state = GST_STATE(streamer_data.pipeline);

    switch(state)
    {
      case GST_STATE_PAUSED:
        break;

      case GST_STATE_NULL:
        streamer_data.url_fifo_LOCK_ME->locked_rw(
            [] (PlayQueue::Queue<PlayQueue::Item> &fifo)
            {
                try_play_next_stream(streamer_data, fifo,
                                     GST_STATE_PAUSED, context);
            });

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

bool Streamer::seek(int64_t position, const char *units)
{
    if(position < 0)
    {
        msg_error(EINVAL, LOG_ERR, "Negative seeks not supported");
        return false;
    }

    gint64 duration_ns;

    auto data_lock(streamer_data.lock());

    if(!streamer_data.is_player_activated)
    {
        BUG("Seek request while inactive");
        return false;
    }

    if(streamer_data.pipeline == nullptr ||
       !gst_element_query_duration(streamer_data.pipeline,
                                   GST_FORMAT_TIME, &duration_ns) ||
       duration_ns < 0)
        duration_ns = INT64_MIN;

    if(duration_ns < 0)
    {
        msg_error(EINVAL, LOG_ERR, "Cannot seek, duration unknown");
        return false;
    }

    static const auto seek_flags =
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                  GST_SEEK_FLAG_KEY_UNIT |
                                  GST_SEEK_FLAG_SNAP_NEAREST);

    if(units == std::string("%"))
        position = compute_position_from_percentage(position, duration_ns);
    else if(units == std::string("s"))
        position *= GST_SECOND;
    else if(units == std::string("ms"))
        position *= GST_MSECOND;
    else if(units == std::string("us"))
        position *= GST_USECOND;
    else if(units == std::string("ns"))
    {
        /* position value is in nanoseconds already, nothing to do */
    }
    else
        position = INT64_MIN;

    if(position < 0)
    {
        if(position == INT64_MIN)
            msg_error(EINVAL, LOG_ERR, "Seek unit %s not supported", units);

        return false;
    }

    if(position > duration_ns)
    {
        msg_error(EINVAL, LOG_ERR,
                  "Seek position %" PRId64 " ns beyond EOS at %" PRId64 " ns",
                  position, duration_ns);
        return false;
    }

    msg_info("Seek to time %" PRId64 " ns", position);

    if(!gst_element_seek_simple(streamer_data.pipeline, GST_FORMAT_TIME,
                                seek_flags, position))
        return false;

    if(streamer_data.current_stream != nullptr)
        tdbus_splay_playback_emit_speed_changed(dbus_get_playback_iface(),
                                                streamer_data.current_stream->stream_id_,
                                                1.0);

    return true;
}

bool Streamer::fast_winding(double factor)
{
    msg_info("Setting playback speed to %f", factor);
    return streamer_data.locked(
                [&factor] (StreamerData &d) { return do_set_speed(d, factor); });
}

bool Streamer::fast_winding_stop()
{
    msg_info("Playing at regular speed");
    return streamer_data.locked(
                [] (StreamerData &d) { return do_set_speed(d, 1.0); });
}

Streamer::PlayStatus Streamer::next(bool skip_only_if_not_stopped,
                                    uint32_t &out_skipped_id, uint32_t &out_next_id)
{
    auto data_lock(streamer_data.lock());

    if(!streamer_data.is_player_activated)
    {
        BUG("Next request while inactive");
        return Streamer::PlayStatus::STOPPED;
    }

    static const char context[] = "skip to next";

    msg_info("Next requested");
    log_assert(streamer_data.pipeline != nullptr);

    if(skip_only_if_not_stopped)
        streamer_data.current_stream.reset();

    const bool is_dequeuing_permitted =
        (streamer_data.supposed_play_status != Streamer::PlayStatus::STOPPED ||
         !skip_only_if_not_stopped);
    uint32_t skipped_id = streamer_data.current_stream != nullptr
        ? streamer_data.current_stream->stream_id_
        : UINT32_MAX;

    bool is_next_current = false;
    PlayQueue::Item *next_stream = nullptr;

    if(is_dequeuing_permitted)
        next_stream =
            streamer_data.url_fifo_LOCK_ME->locked_rw(
                [&is_next_current]
                (PlayQueue::Queue<PlayQueue::Item> &fifo)
                {
                    bool dummy;
                    return try_take_next(streamer_data, fifo,
                                         true, is_next_current, dummy,
                                         context);
                });

    uint32_t next_id = UINT32_MAX;

    if(next_stream != nullptr && !is_next_current)
    {
        if(streamer_data.current_stream == nullptr)
            BUG("[%s] Have no current stream", context);
        else
        {
            switch(streamer_data.current_stream->get_state())
            {
              case PlayQueue::ItemState::IN_QUEUE:
                BUG("[%s] Wrong state %s of current stream",
                    context,
                    PlayQueue::item_state_name(streamer_data.current_stream->get_state()));
                break;

              case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
              case PlayQueue::ItemState::ACTIVE:
                /* mark current stream as to-be-skipped */
                streamer_data.current_stream->set_state(PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED);
                break;

              case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
              case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
                /* current stream is already being taken down, cannot do it again;
                 * also, we cannot drop directly from URL FIFO because in the
                 * meantime it may have been refilled */
                next_stream = nullptr;
                skipped_id = UINT32_MAX;
                break;
            }
        }
    }

    if(next_stream == nullptr)
        streamer_data.supposed_play_status = Streamer::PlayStatus::STOPPED;
    else
    {
        GstState next_state = GST_STATE_READY;

        if(set_stream_state(streamer_data.pipeline, next_state, context))
        {
            switch(streamer_data.supposed_play_status)
            {
              case Streamer::PlayStatus::STOPPED:
                break;

              case Streamer::PlayStatus::PLAYING:
                next_state = GST_STATE_PLAYING;
                break;

              case Streamer::PlayStatus::PAUSED:
                next_state = GST_STATE_PAUSED;
                break;
            }

            if(play_next_stream(streamer_data,
                                is_next_current ? nullptr : streamer_data.current_stream.get(),
                                *next_stream, next_state, true, false,
                                context))
                next_id = next_stream->stream_id_;
        }
    }

    out_skipped_id = skipped_id;
    out_next_id = next_id;

    return streamer_data.supposed_play_status;
}

bool Streamer::is_playing()
{
    return streamer_data.locked(
                [] (StreamerData &d) { return GST_STATE(d.pipeline) == GST_STATE_PLAYING; });
}

bool Streamer::get_current_stream_id(stream_id_t &id)
{
    auto data_lock(streamer_data.lock());

    if(streamer_data.current_stream != nullptr &&
       !streamer_data.current_stream->url_.empty())
    {
        id = streamer_data.current_stream->stream_id_;
        return true;
    }

    return false;
}

bool Streamer::push_item(stream_id_t stream_id, GVariantWrapper &&stream_key,
                         const char *stream_url, size_t keep_items)
{
    auto data_lock(streamer_data.lock());
    bool is_active = streamer_data.is_player_activated;

    if(!is_active)
    {
        BUG("Push request while inactive");
        return false;
    }

    auto item(std::make_unique<PlayQueue::Item>(
            stream_id, std::move(stream_key), stream_url,
            std::chrono::time_point<std::chrono::nanoseconds>::min(),
            std::chrono::time_point<std::chrono::nanoseconds>::max()));

    if(item == nullptr)
    {
        msg_out_of_memory("stream item");
        return false;
    }

    return streamer_data.url_fifo_LOCK_ME->locked_rw(
                [&item, &keep_items]
                (PlayQueue::Queue<PlayQueue::Item> &fifo)
                {
                    return fifo.push(std::move(item), keep_items) != 0;
                });
}

bool Streamer::remove_items_for_root_path(const char *root_path)
{
    const std::string PREFIX = "file://";

    auto filename_from_uri = [] (const std::string &url) -> std::string
    {
        GError *gerror = nullptr;
        char *filename = g_filename_from_uri(url.c_str(), nullptr, &gerror);

        if(!filename)
        {
            msg_error(0, LOG_EMERG, "Error while extracting file name from uri: '%s'" ,
                      (gerror && gerror->message) ? gerror->message : "N/A");
            g_clear_error(&gerror);
            return std::string();
        }

        std::string s(filename);
        g_free(filename);
        return s;
    };

    auto realpath_cxx = [] (const std::string &file_path) -> std::string
    {
        std::string buf;
        buf.resize(PATH_MAX);

        if(realpath(file_path.c_str(), &buf[0]) == nullptr)
            msg_error(0, LOG_EMERG, "Error while realpath(%s) : '%s'" ,
                      file_path.c_str(), strerror(errno));

        return buf;
    };

    auto starts_with = [] (const std::string &s, const std::string &prefix) -> bool
    {
        return s.size() > prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    };

    std::unique_lock<std::recursive_mutex> data_lock(streamer_data.lock());

    if(streamer_data.is_player_activated && streamer_data.current_stream != nullptr)
    {
        const auto &url = streamer_data.current_stream->url_;
        if(starts_with(url, "file://"))
        {
            const auto &filename = filename_from_uri(url);
            if(filename.empty())
                return false;

            const auto &file_path_real = realpath_cxx(filename);
            if(starts_with(file_path_real, root_path))
            {
                msg_info("Will stop streamer because current stream '%s' is on '%s' being removed",
                         url.c_str(), root_path);
                stop("Device removal");
            }
        }
    }

    return true;
}
