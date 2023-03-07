/*
 * Copyright (C) 2015--2023  T+A elektroakustik GmbH & Co. KG
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
#include <array>
#include <utility>
#include <unordered_set>
#include <vector>
#include <algorithm>

#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <gst/video/gstvideodecoder.h>

#include "streamer.hh"
#include "urlfifo.hh"
#include "playitem.hh"
#include "stream_logging.hh"
#include "buffering.hh"
#include "boosted_threads.hh"
#include "stopped_reasons.hh"
#include "cookie_manager_impl.hh"
#include "gstringwrapper.hh"
#include "gerrorwrapper.hh"
#include "dbus.hh"
#include "dbus/de_tahifi_streamplayer.hh"
#include "dbus/de_tahifi_artcache.hh"
#include "maybe.hh"
#include "guard.hh"

#if LOGGED_LOCKS_ENABLED
bool LoggedLock::log_messages_enabled = true;
LoggedLock::Mutex LoggedLock::MutexTraits<LoggedLock::Mutex>::dummy_for_default_ctor_;
LoggedLock::RecMutex LoggedLock::MutexTraits<LoggedLock::RecMutex>::dummy_for_default_ctor_;
#if LOGGED_LOCKS_THREAD_CONTEXTS
thread_local LoggedLock::Context LoggedLock::context;
#endif
#endif

#if BOOSTED_THREADS_DEBUG
static BoostedThreads::ThreadObserver thread_observer;
#endif /* BOOSTED_THREADS_DEBUG */

enum class ActivateStreamResult
{
    INVALID_ITEM,
    INVALID_STATE,
    ALREADY_ACTIVE,
    ACTIVATED,
};

enum class WhichStreamFailed
{
    UNKNOWN,
    CURRENT,
    CURRENT_WITH_PREFAIL_REASON,
    GAPLESS_NEXT,
    GAPLESS_NEXT_WITH_PREFAIL_REASON,
    ALREADY_GONE,
};

struct time_data
{
    int64_t position_s;
    int64_t duration_s;
};

struct FailureData
{
    StoppedReasons::Reason reason;
    bool clear_fifo_on_error;
    bool report_on_stream_stop;

    explicit FailureData():
        reason(StoppedReasons::Reason::UNKNOWN),
        clear_fifo_on_error(false),
        report_on_stream_stop(false)
    {}

    explicit FailureData(StoppedReasons::Reason sreason):
        reason(sreason),
        clear_fifo_on_error(false),
        report_on_stream_stop(false)
    {}

    explicit FailureData(bool report_on_stop):
        reason(StoppedReasons::Reason::UNKNOWN),
        clear_fifo_on_error(false),
        report_on_stream_stop(report_on_stop)
    {}

    void reset()
    {
        reason = StoppedReasons::Reason::UNKNOWN;
        clear_fifo_on_error = false;
        report_on_stream_stop = false;
    }
};

/*!
 * GStreamer next URI requested.
 */
enum class NextStreamRequestState
{
    NOT_REQUESTED,
    REQUESTED,
    REQUEST_DEFERRED,
};

class StreamerData
{
  private:
    mutable LoggedLock::RecMutex lock_;

  public:
    bool is_player_activated;

    GstElement *pipeline;
    guint bus_watch;
    guint progress_watcher;
    guint soup_http_block_size;
    bool boost_streaming_thread;
    const std::string *force_alsa_device;
    BoostedThreads::Threads boosted_threads_;
    std::vector<gulong> signal_handler_ids;

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
    bool current_stream_protected_once;
    NextStreamRequestState next_stream_request;

    bool is_failing;
    FailureData fail;
    Maybe<int64_t> initial_seek_position_ns;

    struct time_data previous_time;
    struct time_data current_time;

    GstClock *system_clock;
    bool is_tag_update_scheduled;
    GstClockTime next_allowed_tag_update_time;

    bool stream_has_just_started;
    Buffering::Data stream_buffering_data;

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
        boost_streaming_thread(true),
        force_alsa_device(nullptr),
        url_fifo_LOCK_ME(std::make_unique<PlayQueue::Queue<PlayQueue::Item>>()),
        current_stream_protected_once(false),
        next_stream_request(NextStreamRequestState::NOT_REQUESTED),
        is_failing(false),
        previous_time{},
        current_time{},
        system_clock(nullptr),
        is_tag_update_scheduled(false),
        next_allowed_tag_update_time(0),
        stream_has_just_started(false),
        supposed_play_status(Streamer::PlayStatus::STOPPED)
    {
        LoggedLock::configure(lock_, "StreamerData", MESSAGE_LEVEL_DEBUG);
    }

    std::pair<bool, LoggedLock::UniqueLock<LoggedLock::RecMutex>>
    lock(const GstElement *expected_pipeline, const char *context) const
    {
        auto result =
            std::make_pair(false, LoggedLock::UniqueLock<LoggedLock::RecMutex>(lock_));
        if(expected_pipeline == nullptr || expected_pipeline == pipeline ||
           !GST_IS_PIPELINE(expected_pipeline))
            result.first = true;
        else
            msg_vinfo(MESSAGE_LEVEL_IMPORTANT,
                      "LOCK: Pipeline %s has been replaced [%s]",
                      GST_ELEMENT_NAME(expected_pipeline), context);
        return result;
    }

    template <typename F>
    auto locked(F &&code) -> decltype(code(*this))
    {
        std::lock_guard<LoggedLock::RecMutex> lk(lock_);
        return code(*this);
    }
};

static CookieManager global_rnf_call_cookie_manager;

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
    GST_PLAY_FLAG_FORCE_FILTERS     = (1 << 11),
    GST_PLAY_FLAG_FORCE_SW_DECODERS = (1 << 12),
}
GstPlayFlags;

static void invalidate_position_information(struct time_data &data)
{
    data.position_s = INT64_MAX;
    data.duration_s = INT64_MAX;
}

static bool set_stream_state(GstElement *pipeline, GstState next_state,
                             const char *context)
{
    msg_vinfo(MESSAGE_LEVEL_DEBUG, "[%s] Set pipeline state %s",
              context, gst_element_state_get_name(next_state));
    GstStateChangeReturn ret = gst_element_set_state(pipeline, next_state);

    switch(ret)
    {
      case GST_STATE_CHANGE_SUCCESS:
      case GST_STATE_CHANGE_ASYNC:
        return true;

      case GST_STATE_CHANGE_NO_PREROLL:
        msg_info("[%s] State change OK, no preroll (gst_element_set_state())",
                 context);
        return true;

      case GST_STATE_CHANGE_FAILURE:
        msg_error(0, LOG_ERR,
                  "[%s] Failed changing state (gst_element_set_state())",
                  context);
        break;
    }

    msg_error(0, LOG_ERR,
              "[%s] gst_element_set_state() failed (%d)", context, ret);

    return false;
}

template <typename T>
static inline GVariantWrapper
mk_id_array(const T &input, std::unordered_set<stream_id_t> &&dropped)
{
    if(input.empty() && dropped.empty())
        return GVariantWrapper(
                    g_variant_new_fixed_array(G_VARIANT_TYPE_UINT16,
                                              nullptr, 0, sizeof(stream_id_t)));

    std::vector<stream_id_t> ids;
    std::transform(
        input.begin(), input.end(), std::back_inserter(ids),
        [&dropped] (const auto &item) -> stream_id_t
        {
            dropped.erase(item->stream_id_);
            return item->stream_id_;
        });
    std::copy(dropped.begin(), dropped.end(), std::back_inserter(ids));

    static_assert(sizeof(stream_id_t) == 2, "Unexpected stream ID size");

    return GVariantWrapper(
                g_variant_new_fixed_array(G_VARIANT_TYPE_UINT16, ids.data(),
                                          ids.size(), sizeof(ids[0])));
}

static GVariantWrapper
mk_id_array_from_queued_items(const PlayQueue::Queue<PlayQueue::Item> &url_fifo)
{
    return mk_id_array(url_fifo, {});
}

static GVariantWrapper
mk_id_array_from_dropped_items(PlayQueue::Queue<PlayQueue::Item> &url_fifo)
{
    return mk_id_array(url_fifo.get_removed(), url_fifo.get_dropped());
}

static void wipe_out_uri(StreamerData &data, const char *context)
{
    msg_vinfo(MESSAGE_LEVEL_DEBUG,
              "Wiping out pipeline's uri property [%s]", context);
    g_object_set(data.pipeline, "uri", "", nullptr);
}

static inline void about_to_wipe_current_stream(PlayQueue::Item *current_stream,
                                                bool will_be_null, const char *context)
{
    msg_vinfo(MESSAGE_LEVEL_DEBUG,
              "About to %s current %snull stream pointer (%s)",
              will_be_null ? "wipe out" : "replace",
              current_stream == nullptr ? "" : "non-", context);
}

static void emit_stopped(TDBus::Iface<tdbussplayPlayback> &playback_iface,
                         StreamerData &data)
{
    data.supposed_play_status = Streamer::PlayStatus::STOPPED;
    data.stream_buffering_data.reset();
    data.boosted_threads_.throttle("stopped");
    wipe_out_uri(data, __func__);

    auto dropped_ids(mk_id_array_from_dropped_items(*data.url_fifo_LOCK_ME));

    if(data.current_stream == nullptr &&
       (dropped_ids == nullptr ||
        g_variant_n_children(GVariantWrapper::get(dropped_ids)) <= 0))
        return;

    playback_iface.emit(tdbus_splay_playback_emit_stopped,
                        data.current_stream != nullptr
                        ? data.current_stream->stream_id_
                        : 0,
                        GVariantWrapper::move(dropped_ids));
}

static void emit_stopped_with_error(TDBus::Iface<tdbussplayPlayback> &playback_iface,
                                    StreamerData &data,
                                    PlayQueue::Queue<PlayQueue::Item> &url_fifo,
                                    StoppedReasons::Reason reason,
                                    std::unique_ptr<PlayQueue::Item> failed_stream)
{
    data.supposed_play_status = Streamer::PlayStatus::STOPPED;
    data.stream_buffering_data.reset();
    data.boosted_threads_.throttle("stopped with error");
    wipe_out_uri(data, __func__);

    auto dropped_ids(mk_id_array_from_dropped_items(url_fifo));

    if(failed_stream == nullptr)
        playback_iface.emit(
            tdbus_splay_playback_emit_stopped_with_error,
            0, "", url_fifo.size() == 0,
            GVariantWrapper::move(dropped_ids),
            StoppedReasons::as_string(reason));
    else
    {
        playback_iface.emit(
            tdbus_splay_playback_emit_stopped_with_error,
            failed_stream->stream_id_,
            failed_stream->get_url_for_reporting().c_str(),
            url_fifo.size() == 0,
            GVariantWrapper::move(dropped_ids),
            StoppedReasons::as_string(reason));
    }
}

static void disconnect_playbin_signals(StreamerData &data)
{
    if(data.pipeline == nullptr)
        return;

    for(const auto id : data.signal_handler_ids)
        g_signal_handler_disconnect(data.pipeline, id);

    data.signal_handler_ids.clear();
}

static void teardown_playbin(StreamerData &data)
{
    if(data.pipeline == nullptr)
        return;

    g_source_remove(data.bus_watch);
    data.bus_watch = 0;

    gst_object_unref(GST_OBJECT(data.pipeline));
    data.pipeline = nullptr;
}

static void reset_playbin(StreamerData &data, const char *context)
{
    data.boosted_threads_.throttle(context);
    disconnect_playbin_signals(data);
    set_stream_state(data.pipeline, GST_STATE_NULL, "reset/rebuild");
}

static int create_playbin(StreamerData &data, const char *context);

static int rebuild_playbin(StreamerData &data, const char *context)
{
    reset_playbin(data, context);
    teardown_playbin(data);
    return create_playbin(data, context);
}

static void do_stop_pipeline_and_recover_from_error(
        StreamerData &data, PlayQueue::Queue<PlayQueue::Item> &url_fifo)
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

    msg_info("Stop reason is %s", as_string(data.fail.reason));

    if(data.fail.clear_fifo_on_error)
        url_fifo.clear(0);

    invalidate_position_information(data.previous_time);
    about_to_wipe_current_stream(data.current_stream.get(), true, "stop and recover");
    emit_stopped_with_error(TDBus::get_exported_iface<tdbussplayPlayback>(),
                            data, url_fifo,
                            data.fail.reason, std::move(data.current_stream));

    data.stream_has_just_started = false;
    data.next_stream_request = NextStreamRequestState::NOT_REQUESTED;
    data.is_failing = false;
    data.fail.reset();
}

class PipelineCallbackData
{
  public:
    StreamerData &data_;
    GstElement *pipeline_;
    GstMessage *message_;

    PipelineCallbackData(const PipelineCallbackData &) = delete;
    PipelineCallbackData(PipelineCallbackData &&) = delete;
    PipelineCallbackData &operator=(const PipelineCallbackData &) = delete;
    PipelineCallbackData &operator=(PipelineCallbackData &&) = delete;

    explicit PipelineCallbackData(StreamerData &data,
                                  GstMessage *msg = nullptr):
        data_(data),
        pipeline_(GST_ELEMENT(gst_object_ref(data_.pipeline))),
        message_(gst_message_ref(msg))
    {}

    ~PipelineCallbackData()
    {
        gst_object_unref(pipeline_);
        gst_message_unref(message_);
    }
};

static gboolean stop_pipeline_and_recover_from_error(gpointer user_data)
{
    msg_vinfo(MESSAGE_LEVEL_DIAG, "Recover from error");

    auto *pcbdata = static_cast<PipelineCallbackData *>(user_data);
    Guard guard([pcbdata] { delete pcbdata; });

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(pcbdata->data_.lock(pcbdata->pipeline_,
                                       "stop, recover from error"));

    if(!data_lock.first)
        return G_SOURCE_REMOVE;

    LOGGED_LOCK_CONTEXT_HINT;
    pcbdata->data_.url_fifo_LOCK_ME->locked_rw(
        [pcbdata]
        (PlayQueue::Queue<PlayQueue::Item> &fifo)
        {
            do_stop_pipeline_and_recover_from_error(pcbdata->data_, fifo);
        });

    return G_SOURCE_REMOVE;
}

static void schedule_error_recovery(StreamerData &data,
                                    StoppedReasons::Reason reason)
{
    data.next_stream_request = NextStreamRequestState::NOT_REQUESTED;
    data.is_failing = true;
    data.fail.reason = reason;
    data.fail.clear_fifo_on_error = false;

    g_idle_add(stop_pipeline_and_recover_from_error,
               new PipelineCallbackData(data));
}

static void recover_from_error_now_or_later(StreamerData &data,
                                            const FailureData &fdata)
{
    if(!fdata.report_on_stream_stop)
        schedule_error_recovery(data, fdata.reason);
    else
        data.fail = fdata;
}

static void rebuild_playbin_for_workarounds(StreamerData &data,
                                            const char *context)
{
    rebuild_playbin(data, context);
    data.current_stream_protected_once = true;
    data.stream_has_just_started = false;
    data.next_stream_request = NextStreamRequestState::NOT_REQUESTED;
    data.is_failing = false;
    data.fail.reset();
    data.stream_buffering_data.reset();
    invalidate_position_information(data.current_time);
    invalidate_position_information(data.previous_time);
}

/*!
 * Find out which item is going to be played next.
 *
 * In most cases, this function will simply return the pointer stored at the
 * head of the queue. In case there is a current stream whose state is
 * #PlayQueue::ItemState::IN_QUEUE (i.e., it is the first stream to play and
 * has been removed from the queue already, but is not playing yet), the
 * current stream is returned.
 *
 * \param current_stream
 *     Pointer to the current stream, or \c nullptr if there is no currently
 *     playing stream.
 *
 * \param url_fifo
 *     The item queue.
 *
 * \param[out] next_stream_is_in_fifo
 *     Tells the caller whether or not the returned stream pointer is stored in
 *     \p url_fifo.
 *
 * \returns
 *     Pointer to the next stream, or \c nullptr is there is no next stream.
 */
static PlayQueue::Item *pick_next_item(PlayQueue::Item *current_stream,
                                       PlayQueue::Queue<PlayQueue::Item> &url_fifo,
                                       bool &next_stream_is_in_fifo)
{
    if(current_stream != nullptr)
    {
        switch(current_stream->get_state())
        {
          case PlayQueue::ItemState::IN_QUEUE:
          case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
          case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
            next_stream_is_in_fifo = false;
            return current_stream;

          case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
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
    auto *next = pick_next_item(data.current_stream.get(),
                                url_fifo, replaced_current_stream);

    current_stream_is_just_in_queue = false;

    if(next == nullptr)
    {
        if(!is_queued_item_expected)
            return nullptr;

        msg_info("[%s] Cannot dequeue, URL FIFO is empty", context);
        fdata.reason = StoppedReasons::Reason::QUEUE_EMPTY;
    }
    else if(next->empty())
    {
        msg_vinfo(MESSAGE_LEVEL_IMPORTANT,
                  "[%s] Cannot dequeue, URL in item is empty", context);
        fdata.reason = StoppedReasons::Reason::URL_MISSING;
    }
    else
    {
        if(replaced_current_stream)
        {
            about_to_wipe_current_stream(data.current_stream.get(), false,
                                         "pop() in try_take_next() on success");
            url_fifo.pop(data.current_stream,
                         "try_take_next(), replaced current stream");
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
              case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
              case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
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
        {
            about_to_wipe_current_stream(data.current_stream.get(), false,
                                         "pop() in try_take_next() on error");
            url_fifo.pop(data.current_stream,
                         "try_take_next(), error after replacing current stream");
        }
        else
            url_fifo.pop_drop();

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

static void handle_buffer_underrun(StreamerData &data);

static void do_encourage_buffering(StreamerData &locked_data)
{
    if(locked_data.current_stream->is_network_stream())
    {
        locked_data.stream_buffering_data.set_buffer_level(0);
        handle_buffer_underrun(locked_data);
    }
}

static bool on_link_resolved(StreamerData &locked_data,
                             bool pipeline_has_changed,
                             PlayQueue::Item &stream,
                             GstState next_state,
                             bool is_prefetching_for_gapless)
{
    if(pipeline_has_changed)
        return false;

    g_object_set(locked_data.pipeline, "uri",
                 stream.get_url_for_playing().c_str(), nullptr);
    locked_data.next_stream_request = NextStreamRequestState::NOT_REQUESTED;

    if(is_prefetching_for_gapless)
    {
        if(!stream.is_pipeline_start_required())
            return true;

        if(next_state != GST_STATE_PLAYING && next_state != GST_STATE_PAUSED)
            next_state = GST_STATE_PLAYING;
    }

    const bool retval = set_stream_state(locked_data.pipeline, next_state,
                                         "play queued");

    if(retval)
    {
        invalidate_position_information(locked_data.previous_time);
        do_encourage_buffering(locked_data);
    }

    return retval;
}

static bool resolve_selected_url_or_play_uri(StreamerData &data,
                                             bool pipeline_has_changed,
                                             PlayQueue::Item &stream,
                                             GstState next_state,
                                             bool is_prefetching_for_gapless,
                                             const char *context);

class ResolveAirableRedirectContext: public DBusRNF::ContextData
{
  private:
    StreamerData &streamer_data_;
    const stream_id_t stream_id_;
    const GstState next_state_;
    const bool is_prefetching_for_gapless_;

  public:
    ResolveAirableRedirectContext(const ResolveAirableRedirectContext &) = delete;
    ResolveAirableRedirectContext &operator=(const ResolveAirableRedirectContext &) = delete;

    explicit ResolveAirableRedirectContext(StreamerData &streamer_data,
                                           stream_id_t stream_id,
                                           GstState next_state,
                                           bool is_prefetching_for_gapless):
        DBusRNF::ContextData(nullptr),
        streamer_data_(streamer_data),
        stream_id_(stream_id),
        next_state_(next_state),
        is_prefetching_for_gapless_(is_prefetching_for_gapless)
    {}

    void notify(DBusRNF::CallBase &call, DBusRNF::CallState state) final override
    {
        PlayQueue::URLState url_state = PlayQueue::URLState::KNOWN_DIRECT_URL;

        switch(state)
        {
          case DBusRNF::CallState::INITIALIZED:
          case DBusRNF::CallState::WAIT_FOR_NOTIFICATION:
          case DBusRNF::CallState::READY_TO_FETCH:
          case DBusRNF::CallState::ABOUT_TO_DESTROY:
            return;

          case DBusRNF::CallState::ABORTING:
          case DBusRNF::CallState::ABORTED_BY_PEER:
            url_state = PlayQueue::URLState::KNOWN_AIRABLE_LINK;
            break;

          case DBusRNF::CallState::RESULT_FETCHED:
            url_state = PlayQueue::URLState::KNOWN_RESOLVED_URL;
            break;

          case DBusRNF::CallState::FAILED:
            url_state = PlayQueue::URLState::BROKEN;
            break;
        }

        auto &redir_call(static_cast<DBusRNF::ResolveAirableRedirectCall &>(call));
        LOGGED_LOCK_CONTEXT_HINT;
        auto data_lock(streamer_data_.lock(nullptr,
                                           "ResolveAirableRedirectContext::notify()"));
        LOGGED_LOCK_CONTEXT_HINT;
        auto fifo_lock(streamer_data_.url_fifo_LOCK_ME->lock());

        PlayQueue::Item *next_item;
        if(streamer_data_.current_stream != nullptr &&
           streamer_data_.current_stream->stream_id_ == stream_id_)
            next_item = streamer_data_.current_stream.get();
        else
        {
            next_item = streamer_data_.url_fifo_LOCK_ME->peek();
            if(next_item != nullptr && next_item->stream_id_ != stream_id_)
                next_item = nullptr;
        }

        if(next_item == nullptr)
        {
            msg_info("Resolved link for stream %u, but queue item already gone",
                     stream_id_);
            return;
        }

        auto resolver_result = PlayQueue::Item::ResolverResult::FAILED;
        switch(url_state)
        {
          case PlayQueue::URLState::KNOWN_RESOLVED_URL:
            try
            {
                DBusRNF::ResolveAirableRedirectResult result(redir_call.get_result_unlocked());
                resolver_result = next_item->resolver_finish(result.url_.get(),
                                                             std::move(result.expected_valid_));
            }
            catch(const std::exception &e)
            {
                msg_info("EXCEPTION on Airable redirect: %s", e.what());
                resolver_result = next_item->resolver_finish(PlayQueue::URLState::BROKEN);
            }
            break;

          case PlayQueue::URLState::KNOWN_AIRABLE_LINK:
            if(redir_call.get_list_error().failed())
                url_state = PlayQueue::URLState::BROKEN;

            /* fall-through */

          case PlayQueue::URLState::BROKEN:
            resolver_result = next_item->resolver_finish(url_state);
            break;

          case PlayQueue::URLState::KNOWN_DIRECT_URL:
          case PlayQueue::URLState::RESOLVING_LINK:
            MSG_UNREACHABLE();
            resolver_result = next_item->resolver_finish(PlayQueue::URLState::BROKEN);
            break;
        }

        switch(resolver_result)
        {
          case PlayQueue::Item::ResolverResult::RESOLVED:
            /* we got a direct URL, so let's pass it on to GStreamer */
            on_link_resolved(streamer_data_, !data_lock.first,
                             *next_item, next_state_,
                             is_prefetching_for_gapless_);
            break;

          case PlayQueue::Item::ResolverResult::HAVE_MORE_URLS:
            /* the item knows about more URLs, so we may advance to the next
             * and try that one */
            if(!next_item->select_next_url())
                break;

            resolve_selected_url_or_play_uri(streamer_data_, !data_lock.first,
                                             *next_item, next_state_,
                                             is_prefetching_for_gapless_,
                                             "try resolving next URI for stream");
            break;

          case PlayQueue::Item::ResolverResult::FAILED:
            /* we have tried all URLs, so we must propagate an error to the
             * queue management process, which will most probably skip this
             * item */
            MSG_NOT_IMPLEMENTED();
            break;
        }
    }
};

static bool resolve_selected_url_or_play_uri(StreamerData &data,
                                             bool pipeline_has_changed,
                                             PlayQueue::Item &stream,
                                             GstState next_state,
                                             bool is_prefetching_for_gapless,
                                             const char *context)
{
    PlayQueue::log_next_stream(stream, context);

    const auto url_state(stream.get_selected_url_state());

    switch(url_state)
    {
      case PlayQueue::URLState::KNOWN_DIRECT_URL:
      case PlayQueue::URLState::KNOWN_RESOLVED_URL:
        break;

      case PlayQueue::URLState::KNOWN_AIRABLE_LINK:
        msg_info("URL for next stream %u needs to be resolved now [%s, %s]",
                 stream.stream_id_,
                 stream.get_url_for_reporting().c_str(), context);
        stream.resolver_begin(
            global_rnf_call_cookie_manager,
            std::make_unique<ResolveAirableRedirectContext>(
                data, stream.stream_id_, next_state, is_prefetching_for_gapless));
        return true;

      case PlayQueue::URLState::RESOLVING_LINK:
        msg_info("URL for next stream %u not resolved yet [%s, %s]",
                 stream.stream_id_,
                 stream.get_url_for_reporting().c_str(), context);
        return true;

      case PlayQueue::URLState::BROKEN:
        msg_info("URL for next stream %u is broken [%s, %s]",
                 stream.stream_id_,
                 stream.get_url_for_reporting().c_str(), context);
        MSG_NOT_IMPLEMENTED();
        return false;
    }

    return on_link_resolved(data, pipeline_has_changed, stream, next_state,
                            is_prefetching_for_gapless);

}

static bool play_next_stream(StreamerData &data,
                             bool pipeline_has_changed,
                             PlayQueue::Item *replaced_stream,
                             PlayQueue::Item &next_stream,
                             GstState next_state, bool is_skipping,
                             bool is_prefetching_for_gapless, const char *context)
{
    msg_log_assert(!pipeline_has_changed);

    switch(next_stream.get_state())
    {
      case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
      case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
      case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
      case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
        MSG_BUG("[%s] Unexpected stream state %s",
                context, PlayQueue::item_state_name(next_stream.get_state()));
        return false;

      case PlayQueue::ItemState::IN_QUEUE:
        next_stream.set_state(PlayQueue::ItemState::ABOUT_TO_ACTIVATE);
        break;

      case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
        /* next stream is already in activation phase, so don't attempt to
         * start playing it */
        if(replaced_stream == nullptr &&
            data.next_stream_request == NextStreamRequestState::REQUESTED)
        {
            /* The next stream is in FIFO and GStreamer has requested the next
             * URI, but we cannot advance because the stream sitting at the
             * head of our FIFO is already in activation phase. Therefore, we
             * will wait for the activating stream to start, and push the
             * stream following it (if any) to GStreamer when we see a
             * \c GST_MESSAGE_STREAM_START message. */
            data.next_stream_request = NextStreamRequestState::REQUEST_DEFERRED;
        }
        else
        {
            /* next stream is not in FIFO, or GStreamer hasn't requested one */
        }

        return true;
    }

    if(replaced_stream != nullptr)
        replaced_stream->set_state(is_skipping
                                   ? PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED
                                   : PlayQueue::ItemState::ABOUT_TO_PHASE_OUT);

    return resolve_selected_url_or_play_uri(data, pipeline_has_changed,
                                            next_stream, next_state,
                                            is_prefetching_for_gapless, context);
}

static void queue_stream_from_url_fifo__unlocked(StreamerData &data,
                                                 bool pipeline_has_changed,
                                                 const char *context)
{
    MSG_BUG_IF(data.next_stream_request == NextStreamRequestState::NOT_REQUESTED,
               "GStreamer has not requested the next stream yet [%s]", context);
    msg_log_assert(!pipeline_has_changed);

    bool is_next_in_fifo;
    auto *const next_stream = pick_next_item(data.current_stream.get(),
                                             *data.url_fifo_LOCK_ME,
                                             is_next_in_fifo);

    if(data.current_stream == nullptr && next_stream == nullptr)
    {
        MSG_BUG("Having nothing in queue, GStreamer is asking for more, "
                "but currently playing nothing [%s]", context);
        return;
    }

    if(next_stream == nullptr)
    {
        /* we are done here */
        msg_log_assert(data.current_stream != nullptr);
        data.current_stream->set_state(PlayQueue::ItemState::ABOUT_TO_PHASE_OUT);
    }
    else
        play_next_stream(data, pipeline_has_changed,
                         is_next_in_fifo ? nullptr : data.current_stream.get(),
                         *next_stream, GST_STATE_NULL, false, true, context);
}

/*
 * GLib signal callback: playbin3 "about-to-finish".
 */
static void queue_stream_from_url_fifo(GstElement *elem, gpointer user_data)
{
    static const char context[] = "about-to-finish";

    auto &data = *static_cast<StreamerData *>(user_data);
    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(elem, context));

    if(!data_lock.first)
        return;

    if(data.is_failing)
        return;

    data.next_stream_request = NextStreamRequestState::REQUESTED;

    LOGGED_LOCK_CONTEXT_HINT;
    auto fifo_lock(data.url_fifo_LOCK_ME->lock());
    queue_stream_from_url_fifo__unlocked(data, !data_lock.first, context);
}

static gboolean do_restart_playing(gpointer user_data)
{
    Streamer::start("automatic restart");
    return G_SOURCE_REMOVE;
}

static void handle_end_of_stream(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));
    msg_info("End of stream notification received");

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), "EOS"));
    if(!data_lock.first)
        return;

    bool restart_required_now = false;

    set_stream_state(data.pipeline, GST_STATE_READY, "EOS");

    LOGGED_LOCK_CONTEXT_HINT;
    data.url_fifo_LOCK_ME->locked_rw(
        [&data, &restart_required_now] (auto &q)
        {
            if(q.empty())
            {
                msg_info("Finished playing all streams");
                emit_stopped(TDBus::get_exported_iface<tdbussplayPlayback>(),
                             data);
            }
            else
            {
                msg_info("Still have %zu stream(s) in queue at EOS", q.size());

                switch(q.peek()->pipeline_start_required_when_ready())
                {
                  case PlayQueue::Item::PipelineStartRequired::NOT_REQUIRED:
                    MSG_BUG("Stopping because we don't have any other option now");
                    emit_stopped(TDBus::get_exported_iface<tdbussplayPlayback>(),
                                 data);
                    break;

                  case PlayQueue::Item::PipelineStartRequired::START_NOW:
                    restart_required_now = true;
                    break;

                  case PlayQueue::Item::PipelineStartRequired::WHEN_READY:
                    break;
                }
            }
        }
    );

    about_to_wipe_current_stream(data.current_stream.get(), true, "end of stream");
    data.current_stream.reset();
    wipe_out_uri(data, "EOS");

    if(restart_required_now)
        g_idle_add(do_restart_playing, nullptr);
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
        GST_TAG_BEATS_PER_MINUTE,
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

static GVariant *tag_list_to_g_variant(const GstTagList *list,
                                       const std::unordered_map<std::string, std::string> &extra_tags)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ss)"));
    if(list != nullptr)
        gst_tag_list_foreach(list, add_tuple_to_tags_variant_builder, &builder);

    for(const auto &it : extra_tags)
        g_variant_builder_add(&builder, "(ss)", it.first.c_str(), it.second.c_str());

    return g_variant_builder_end(&builder);
}

static GstTagList *g_variant_to_tag_list(GVariantWrapper &&md,
                                         std::string &cover_art_uri,
                                         std::unordered_map<std::string, std::string> &extra_tags)
{
    if(g_variant_n_children(GVariantWrapper::get(md)) == 0)
        return nullptr;

    GstTagList *list = gst_tag_list_new_empty();
    GVariantIter iter;
    g_variant_iter_init(&iter, GVariantWrapper::get(md));
    gchar *tag;
    gchar *value;

    while(g_variant_iter_loop(&iter, "(ss)", &tag, &value))
    {
        if(strcmp(tag, "cover_art") == 0)
            cover_art_uri = value;
        else if(strcmp(tag, "x-drcpd-title") == 0)
            extra_tags.emplace(tag, value);
        else if(strcmp(tag, "parent_id") != 0)
            gst_tag_list_add(list, GST_TAG_MERGE_KEEP, tag, value, nullptr);
    }

    return list;
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
        MSG_BUG("Image data spans multiple memory regions (not implemented)");
        return;
    }

    /*
     * TODO: We can optimize this path for reduced expected CPU consumption by
     * referencing the GstBuffer objects (gst_buffer_ref()) and moving them to
     * a slot per image type ("big" or "preview"). The slots can be processed
     * by an idle task with a rate limit of one shot per 1 or 2 seconds. This
     * moves the CPU consumption taken by image processing away from this time
     * point in a controlled manner, and also compresses multiple image updates
     * into a single update.
     */

    GstMemory *memory = gst_buffer_peek_memory(buffer, 0);
    GstMapInfo mi;

    if(!gst_memory_map(memory, &mi, GST_MAP_READ))
    {
        msg_error(0, LOG_ERR, "Failed mapping image data");
        return;
    }

    /*
     * Pointer comparison should be fine (for filtering) because either we are
     * still working with the same file---and thus, the same sample buffer---as
     * before, or we don't. In the former case, the memory at index 0 should
     * still be at the same location as is was before and we don't need to
     * process the image data again. In the latter case, the address is likely
     * to be different, so we need to read out the image data and send them
     * around. The size comparison provides extra confidence.
     */
    if(sent_data.data == mi.data && sent_data.size == mi.size)
    {
        gst_memory_unmap(memory, &mi);
        return;
    }

    sent_data.data = mi.data;
    sent_data.size = mi.size;
    sent_data.priority = priority;

    TDBus::get_singleton<tdbusartcacheWrite>()
        .call_and_forget<TDBus::ArtCacheWriteAddImageByData>(
            GVariantWrapper::get(sd.stream_key_), priority,
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                      mi.data, mi.size, sizeof(mi.data[0])));

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

        const GstCaps *caps = gst_sample_get_caps(sample);

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
            MSG_BUG("Embedded image tag is URI: not implemented");
            break;
        }

        gst_sample_unref(sample);
    }
}

static void emit_tags__unlocked(StreamerData &data)
{
    auto &sd = data.current_stream->get_stream_data();
    GVariant *meta_data = tag_list_to_g_variant(sd.get_tag_list(), sd.get_extra_tags());

    TDBus::get_exported_iface<tdbussplayPlayback>().emit(
        tdbus_splay_playback_emit_meta_data_changed,
        data.current_stream->stream_id_, meta_data);

    data.next_allowed_tag_update_time =
        gst_clock_get_time(data.system_clock) + 500UL * GST_MSECOND;
    data.is_tag_update_scheduled = false;
}

static gboolean emit_tags(gpointer user_data)
{
    auto &data = *static_cast<StreamerData *>(user_data);
    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(nullptr, "emit tags"));

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

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), "handle tag"));

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

static void emit_now_playing(TDBus::Iface<tdbussplayPlayback> &playback_iface,
                             const StreamerData &data,
                             PlayQueue::Queue<PlayQueue::Item> &url_fifo)
{
    if(data.current_stream == nullptr)
        return;

    const auto &sd = data.current_stream->get_stream_data();
    GVariant *meta_data = tag_list_to_g_variant(sd.get_tag_list(), sd.get_extra_tags());

    auto dropped_ids(mk_id_array_from_dropped_items(url_fifo));

    playback_iface.emit(tdbus_splay_playback_emit_now_playing,
                        data.current_stream->stream_id_,
                        GVariantWrapper::get(sd.stream_key_),
                        data.current_stream->get_url_for_reporting().c_str(),
                        url_fifo.full(),
                        GVariantWrapper::move(dropped_ids),
                        meta_data);
}

static WhichStreamFailed
determine_failed_stream(const StreamerData &data, const GLibString &current_uri,
                        const PlayQueue::Queue<PlayQueue::Item> &fifo)
{
    if(data.current_stream != nullptr && data.current_stream->has_prefailed())
        return current_uri.empty()
            ? WhichStreamFailed::GAPLESS_NEXT_WITH_PREFAIL_REASON
            : WhichStreamFailed::CURRENT_WITH_PREFAIL_REASON;

    if(current_uri.empty())
    {
        if(data.current_stream == nullptr)
            return WhichStreamFailed::ALREADY_GONE;

        MSG_BUG("Have current stream with URI %s, stored URI is empty",
                data.current_stream->get_url_for_playing().c_str());
        return WhichStreamFailed::UNKNOWN;
    }

    if(data.current_stream != nullptr &&
       data.current_stream->get_url_for_playing() == current_uri.get())
        return WhichStreamFailed::CURRENT;

    const auto *const next = fifo.peek();

    if(next != nullptr && next->get_url_for_playing() == current_uri.get())
        return WhichStreamFailed::GAPLESS_NEXT;

    if(data.current_stream == nullptr)
        MSG_BUG("Have no current stream, stored URI is %s", current_uri.get());
    else
        MSG_BUG("Have current stream with URI %s, stored URI is %s",
                data.current_stream->get_url_for_playing().c_str(),
                current_uri.get());

    return WhichStreamFailed::UNKNOWN;
}

static bool try_refresh_uri_or_resolve_alternative_uri(
        StreamerData &data, bool pipeline_has_changed,
        const GErrorWrapper &error, bool is_prefetching_for_gapless)
{
    if(data.current_stream == nullptr)
        return false;

    if(pipeline_has_changed)
        return false;

    if(!data.current_stream->select_next_url())
    {
        /* no more alternative URIs available, failing on this stream */
        return false;
    }

    static const char context[] = "try next URI after failure";

    data.is_failing = false;
    data.fail.reset();
    data.current_stream->set_state(PlayQueue::ItemState::ABOUT_TO_ACTIVATE);
    data.current_stream_protected_once = true;
    if(data.current_time.position_s > 0 &&
       data.current_time.position_s < INT64_MAX / GST_SECOND)
        data.initial_seek_position_ns = data.current_time.position_s * GST_SECOND;
    reset_playbin(data, context);

    return resolve_selected_url_or_play_uri(
            data, pipeline_has_changed, *data.current_stream,
            data.supposed_play_status == Streamer::PlayStatus::PLAYING
            ? GST_STATE_PLAYING
            : GST_STATE_PAUSED,
            is_prefetching_for_gapless, context);
}

static void handle_error_message(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    GErrorWrapper error(Streamer::log_error_message(message));

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), "handle error"));
    if(!data_lock.first)
        return;

    LOGGED_LOCK_CONTEXT_HINT;
    const auto fifo_lock(data.url_fifo_LOCK_ME->lock());

    data.initial_seek_position_ns.set_unknown();

    const GLibString current_uri(
        [p = data.pipeline] ()
        {
            gchar *temp = nullptr;
            g_object_get(p, "current-uri", &temp, nullptr);
            return temp;
        });

    auto which_stream_failed =
        determine_failed_stream(data, current_uri, *data.url_fifo_LOCK_ME);
    auto failure_reason = StoppedReasons::Reason::UNKNOWN;
    std::unique_ptr<PlayQueue::Item> failed_item;

    switch(which_stream_failed)
    {
      case WhichStreamFailed::ALREADY_GONE:
        return;

      case WhichStreamFailed::UNKNOWN:
        MSG_BUG("Supposed to handle error, but have no item");
        return;

      case WhichStreamFailed::GAPLESS_NEXT:
        data.url_fifo_LOCK_ME->pop(failed_item, "prefetched stream failed");

        /* fall-through */

      case WhichStreamFailed::CURRENT:
        failure_reason =
            StoppedReasons::from_gerror(error,
                                        !data.current_stream->is_network_stream());
        break;

      case WhichStreamFailed::CURRENT_WITH_PREFAIL_REASON:
        failure_reason = data.current_stream->get_prefail_reason();
        break;

      case WhichStreamFailed::GAPLESS_NEXT_WITH_PREFAIL_REASON:
        data.url_fifo_LOCK_ME->pop(failed_item, "prefetched stream failed with reason");
        failure_reason = failed_item->get_prefail_reason();
        break;
    }

    switch(failure_reason)
    {
      case StoppedReasons::Reason::UNKNOWN:
      case StoppedReasons::Reason::QUEUE_EMPTY:
      case StoppedReasons::Reason::ALREADY_STOPPED:
      case StoppedReasons::Reason::PHYSICAL_MEDIA_IO:
      case StoppedReasons::Reason::AUTHENTICATION:
        /* trying another URI would most probably not resolve this failure */
        break;

      case StoppedReasons::Reason::NET_IO:
      case StoppedReasons::Reason::URL_MISSING:
      case StoppedReasons::Reason::PROTOCOL:
      case StoppedReasons::Reason::DOES_NOT_EXIST:
      case StoppedReasons::Reason::WRONG_TYPE:
      case StoppedReasons::Reason::PERMISSION_DENIED:
      case StoppedReasons::Reason::MISSING_CODEC:
      case StoppedReasons::Reason::WRONG_STREAM_FORMAT:
      case StoppedReasons::Reason::BROKEN_STREAM:
      case StoppedReasons::Reason::ENCRYPTED:
      case StoppedReasons::Reason::DECRYPTION_NOT_SUPPORTED:
        /* worth a retry */
        if(try_refresh_uri_or_resolve_alternative_uri(
                data, !data_lock.first, error,
                which_stream_failed == WhichStreamFailed::GAPLESS_NEXT ||
                which_stream_failed == WhichStreamFailed::GAPLESS_NEXT_WITH_PREFAIL_REASON))
            return;

        break;
    }

    const FailureData fdata(failure_reason);

    switch(which_stream_failed)
    {
      case WhichStreamFailed::ALREADY_GONE:
      case WhichStreamFailed::UNKNOWN:
        MSG_UNREACHABLE();
        break;

      case WhichStreamFailed::CURRENT:
      case WhichStreamFailed::CURRENT_WITH_PREFAIL_REASON:
        msg_error(0, LOG_ERR, "ERROR mapped to stop reason %s, reporting %s",
                  as_string(fdata.reason),
                  fdata.report_on_stream_stop ? "on stop" : "now");
        if(data.current_stream->fail())
            recover_from_error_now_or_later(data, fdata);

        break;

      case WhichStreamFailed::GAPLESS_NEXT:
      case WhichStreamFailed::GAPLESS_NEXT_WITH_PREFAIL_REASON:
        msg_error(0, LOG_ERR, "ERROR prefetching for gapless failed for reason %s",
                  as_string(fdata.reason));

        if(data.current_stream != nullptr)
            data.url_fifo_LOCK_ME->mark_as_dropped(data.current_stream->stream_id_);

        if(failed_item->fail())
        {
            set_stream_state(data.pipeline, GST_STATE_NULL, "stop on bad stream");
            invalidate_position_information(data.previous_time);
            emit_stopped_with_error(TDBus::get_exported_iface<tdbussplayPlayback>(),
                                    data, *data.url_fifo_LOCK_ME,
                                    fdata.reason, std::move(failed_item));
            data.stream_has_just_started = false;
            data.next_stream_request = NextStreamRequestState::NOT_REQUESTED;
            data.is_failing = false;
            about_to_wipe_current_stream(data.current_stream.get(), true,
                                         "error handler");
            data.current_stream.reset();
            data.fail.reset();
        }

        break;
    }
}

static void handle_warning_message(GstMessage *message)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));
    Streamer::log_warning_message(message);
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

static gboolean report_progress__unlocked(StreamerData &data);

/*!
 * GLib callback: timer function, GSourceFunc.
 *
 * \bug There is a bug in GStreamer that leads to the wrong position being
 *     displayed in pause mode for internet streams. How to trigger: play some
 *     URL, then pause; skip to next URL; the position queried from the playbin
 *     pipeline is still the paused time, but should be 0.
 */
static gboolean report_progress(gpointer user_data)
{
    auto &data = *static_cast<StreamerData *>(user_data);
    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(nullptr, "report progress"));
    return report_progress__unlocked(data);
}

static gboolean report_progress__unlocked(StreamerData &data)
{
    if(data.current_stream == nullptr)
    {
        data.progress_watcher = 0;
        return G_SOURCE_REMOVE;
    }

    if(data.stream_buffering_data.is_buffering())
        return G_SOURCE_CONTINUE;

    const GstState state = GST_STATE(data.pipeline);

    switch(state)
    {
      case GST_STATE_PLAYING:
      case GST_STATE_PAUSED:
        query_seconds(gst_element_query_position, data.pipeline,
                      data.current_time.position_s);
        query_seconds(gst_element_query_duration, data.pipeline,
                      data.current_time.duration_s);
        break;

      case GST_STATE_READY:
      case GST_STATE_NULL:
      case GST_STATE_VOID_PENDING:
        invalidate_position_information(data.current_time);
        break;
    }

    if(data.current_time.position_s != data.previous_time.position_s ||
       data.current_time.duration_s != data.previous_time.duration_s)
    {
        data.previous_time = data.current_time;

        TDBus::get_exported_iface<tdbussplayPlayback>().emit(
            tdbus_splay_playback_emit_position_changed,
            data.current_stream->stream_id_,
            data.current_time.position_s, "s",
            data.current_time.duration_s, "s");
    }

    return G_SOURCE_CONTINUE;
}

static ActivateStreamResult
activate_stream(const StreamerData &data, GstState pipeline_state, int phase)
{
    if(data.current_stream == nullptr)
    {
        MSG_BUG("Current item is invalid, switched to %s",
                gst_element_state_get_name(pipeline_state));
        return ActivateStreamResult::INVALID_ITEM;
    }

    switch(data.current_stream->get_state())
    {
      case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
        switch(phase)
        {
          case 0:
          case 1:
            return ActivateStreamResult::ALREADY_ACTIVE;

          case 2:
            LOGGED_LOCK_CONTEXT_HINT;
            if(data.url_fifo_LOCK_ME->locked_rw([] (auto &fifo) { return fifo.empty(); }))
                data.current_stream->set_state(PlayQueue::ItemState::ABOUT_TO_PHASE_OUT);
            else
                data.current_stream->set_state(PlayQueue::ItemState::ACTIVE_NOW_PLAYING);

            return ActivateStreamResult::ACTIVATED;

          default:
            break;
        }

        break;

      case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
      case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
        switch(phase)
        {
          case 0:
          case 2:
            return ActivateStreamResult::ALREADY_ACTIVE;

          default:
            break;
        }

        break;

      case PlayQueue::ItemState::IN_QUEUE:
        MSG_BUG("Unexpected state %s for stream switched to %s",
                PlayQueue::item_state_name(data.current_stream->get_state()),
                gst_element_state_get_name(pipeline_state));

        data.current_stream->set_state(PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED);

        /* fall-through */

      case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
        break;

      case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
        switch(phase)
        {
          case 0:
          case 1:
            data.current_stream->set_state(PlayQueue::ItemState::ACTIVE_HALF_PLAYING);
            return ActivateStreamResult::ACTIVATED;

          case 2:
            data.current_stream->set_state(PlayQueue::ItemState::ACTIVE_NOW_PLAYING);
            return ActivateStreamResult::ACTIVATED;

          default:
            break;
        }

        break;
    }

    return ActivateStreamResult::INVALID_STATE;
}

static void try_leave_buffering_state(StreamerData &data)
{
    switch(data.stream_buffering_data.try_leave_buffering_state())
    {
      case Buffering::LeaveBufferingResult::BUFFER_FILLED:
        BOOSTED_THREADS_DEBUG_CODE(thread_observer.dump("buffer filled (before boost)"));
        if(data.current_stream->is_realtime_processing_allowed())
            data.boosted_threads_.boost("buffer filled");
        BOOSTED_THREADS_DEBUG_CODE(thread_observer.dump("buffer filled (after boost)"));

        switch(data.supposed_play_status)
        {
          case Streamer::PlayStatus::PLAYING:
            set_stream_state(data.pipeline, GST_STATE_PLAYING, "buffer filled");
            break;

          case Streamer::PlayStatus::STOPPED:
          case Streamer::PlayStatus::PAUSED:
            if(data.progress_watcher == 0)
                report_progress__unlocked(data);

            break;
        }

        break;

      case Buffering::LeaveBufferingResult::STILL_BUFFERING:
      case Buffering::LeaveBufferingResult::NOT_BUFFERING:
        break;
    }
}

static void emit_pause_state_if_not_buffering(const StreamerData &data,
                                              gboolean is_paused)
{
    if(!data.stream_buffering_data.is_buffering())
        TDBus::get_exported_iface<tdbussplayPlayback>().emit(
            tdbus_splay_playback_emit_pause_state,
            data.current_stream->stream_id_, is_paused);
}

static void activate_stream_and_emit_pause_state(const StreamerData &data,
                                                 GstState pipeline_state,
                                                 gboolean is_paused)
{
    BOOSTED_THREADS_DEBUG_CODE(thread_observer.dump("activate stream"));

    switch(activate_stream(data, pipeline_state, 0))
    {
      case ActivateStreamResult::INVALID_ITEM:
      case ActivateStreamResult::INVALID_STATE:
        break;

      case ActivateStreamResult::ALREADY_ACTIVE:
      case ActivateStreamResult::ACTIVATED:
        emit_pause_state_if_not_buffering(data, is_paused);
        break;
    }
}

static void handle_stream_state_change(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), "state change"));
    if(!data_lock.first)
        return;

    const bool is_ours =
        (GST_MESSAGE_SRC(message) == GST_OBJECT(data.pipeline));
    const bool work_around_video_decoder = GST_IS_VIDEO_DECODER(GST_MESSAGE_SRC(message));

    if(!work_around_video_decoder &&
       !is_ours && !msg_is_verbose(MESSAGE_LEVEL_TRACE))
        return;

    const GstState target_state = GST_STATE_TARGET(data.pipeline);
    GstState oldstate, state, pending;
    gst_message_parse_state_changed(message, &oldstate, &state, &pending);

    msg_vinfo(MESSAGE_LEVEL_TRACE,
              "State change on %s \"%s\": state %s -> %s, pending %s, target %s (%sours)",
              G_OBJECT_TYPE_NAME(GST_MESSAGE_SRC(message)),
              GST_MESSAGE_SRC_NAME(message),
              gst_element_state_get_name(oldstate),
              gst_element_state_get_name(state),
              gst_element_state_get_name(pending),
              gst_element_state_get_name(target_state),
              is_ours ? "" : "not ");

    if(work_around_video_decoder)
    {
        LOGGED_LOCK_CONTEXT_HINT;
        auto fifo_lock(data.url_fifo_LOCK_ME->lock());

        switch(state)
        {
          case GST_STATE_PAUSED:
          case GST_STATE_PLAYING:
          case GST_STATE_READY:
            if(data.current_stream == nullptr)
                break;

            if(!data.current_stream->prefail(StoppedReasons::Reason::WRONG_TYPE))
                break;

            switch(data.current_stream->get_state())
            {
              case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
              case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
              case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
              case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
                GST_ELEMENT_ERROR(data.pipeline, STREAM, WRONG_TYPE,
                                  ("blocked video content"), ("blocked video content"));
                break;

              case PlayQueue::ItemState::IN_QUEUE:
              case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
                break;
            }

            break;

          case GST_STATE_NULL:
          case GST_STATE_VOID_PENDING:
            break;
        }
    }

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

            if(target_state != GST_STATE_PAUSED)
                data.stream_buffering_data.reset();
            else if(data.stream_buffering_data.entered_pause())
                try_leave_buffering_state(data);
        }

        break;

      case GST_STATE_PLAYING:
      case GST_STATE_VOID_PENDING:
        break;
    }

    switch(target_state)
    {
      case GST_STATE_READY:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to stop, but not there yet */
            break;
        }

        {
            LOGGED_LOCK_CONTEXT_HINT;
            auto fifo_lock(data.url_fifo_LOCK_ME->lock());

            static const char context[] = "previous stream stopped";

            /* clear "uri", otherwise the currently set "uri" is going to be
             * reused on pause request before the new "uri" is known */
            wipe_out_uri(data, context);

            if(data.current_stream != nullptr)
            {
                switch(data.current_stream->get_selected_url_state())
                {
                  case PlayQueue::URLState::KNOWN_DIRECT_URL:
                  case PlayQueue::URLState::KNOWN_RESOLVED_URL:
                    emit_stopped(TDBus::get_exported_iface<tdbussplayPlayback>(), data);
                    break;

                  case PlayQueue::URLState::KNOWN_AIRABLE_LINK:
                  case PlayQueue::URLState::RESOLVING_LINK:
                    return;

                  case PlayQueue::URLState::BROKEN:
                    break;
                }
            }

            if(data.current_stream_protected_once)
                data.current_stream_protected_once = false;
            else
            {
                about_to_wipe_current_stream(data.current_stream.get(), false,
                                             "pop() on stream state change");
                if(!data.url_fifo_LOCK_ME->pop(data.current_stream, context))
                {
                    about_to_wipe_current_stream(data.current_stream.get(), true,
                                                 "empty queue on stream state change");
                    data.current_stream.reset();
                }
            }

            data.stream_has_just_started = false;
        }

        break;

      case GST_STATE_PAUSED:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to pause, but not there yet */
            break;
        }

        activate_stream_and_emit_pause_state(data, state, TRUE);

        if(data.stream_buffering_data.entered_pause())
            try_leave_buffering_state(data);

        if(data.progress_watcher == 0)
            report_progress__unlocked(data);

        break;

      case GST_STATE_PLAYING:
        if(pending != GST_STATE_VOID_PENDING)
        {
            /* want to play, but not there yet */
            break;
        }

        if(!data.stream_has_just_started)
            activate_stream_and_emit_pause_state(data, state, FALSE);
        else
            emit_pause_state_if_not_buffering(data, FALSE);

        data.stream_has_just_started = false;

        if(data.progress_watcher == 0)
            data.progress_watcher = g_timeout_add(50, report_progress, &data);

        break;

      case GST_STATE_VOID_PENDING:
      case GST_STATE_NULL:
        MSG_BUG("Ignoring state transition for bogus pipeline target %s",
                gst_element_state_get_name(target_state));
        break;
    }
}

static void handle_start_of_stream(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    static const char context[] = "stream started";

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), context));
    LOGGED_LOCK_CONTEXT_HINT;
    auto fifo_lock(data.url_fifo_LOCK_ME->lock());

    if(!data.stream_buffering_data.is_buffering() &&
       data.current_stream->is_realtime_processing_allowed())
        data.boosted_threads_.boost(context);

    bool failed = false;
    bool with_bug = false;
    bool need_activation = true;
    bool need_push_next_stream = false;

    bool next_stream_is_in_fifo;
    const PlayQueue::Item *picked_stream =
        pick_next_item(data.current_stream.get(), *data.url_fifo_LOCK_ME,
                       next_stream_is_in_fifo);

    if(picked_stream == nullptr)
        picked_stream = data.current_stream.get();

    if(picked_stream == nullptr)
    {
        MSG_BUG("No stream picked at start of stream");
        failed = with_bug = true;
    }
    else
    {
        switch(picked_stream->get_state())
        {
          case PlayQueue::ItemState::IN_QUEUE:
            MSG_BUG("Picked queued stream at start of stream");
            with_bug = true;

            /* fall-through */

          case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
          case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
            failed = true;

            /* fall-through */

          case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
            switch(data.next_stream_request)
            {
              case NextStreamRequestState::REQUEST_DEFERRED:
                /* GStreamer was very fast at requesting the next URI, so we'll
                 * try to set the next stream URI after having treated the
                 * current stream which has just started to play */
                need_push_next_stream = true;
                break;

              case NextStreamRequestState::NOT_REQUESTED:
              case NextStreamRequestState::REQUESTED:
                break;
            }

            break;

          case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
            {
                const auto *next_stream = data.url_fifo_LOCK_ME->peek();

                if(next_stream == nullptr)
                    need_activation = false;
                else
                {
                    switch(next_stream->get_state())
                    {
                      case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
                        break;

                      case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
                      case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
                      case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
                      case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
                        MSG_BUG("Next stream %u in unexpected state %d",
                                next_stream->stream_id_, int(next_stream->get_state()));

                        /* fall-through */

                      case PlayQueue::ItemState::IN_QUEUE:
                        need_activation = false;
                        break;
                    }
                }
            }

            break;

          case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
            need_activation = false;
            break;
        }
    }

    if(with_bug)
    {
        if(picked_stream == nullptr)
            MSG_BUG("Replace nullptr current by next");
        else
            MSG_BUG("Replace current by next %u in unexpected state %s",
                    picked_stream->stream_id_,
                    PlayQueue::item_state_name(picked_stream->get_state()));

        msg_log_assert(!data.url_fifo_LOCK_ME->empty());
    }

    if(next_stream_is_in_fifo)
        about_to_wipe_current_stream(data.current_stream.get(), false,
                                     "start of stream");

    if(next_stream_is_in_fifo &&
       !data.url_fifo_LOCK_ME->pop(data.current_stream,
                                   failed
                                   ? "replace current due to failure at start of stream"
                                   : "take next stream from queue"))
        need_activation = false;

    if(need_activation)
        data.current_stream->set_state(PlayQueue::ItemState::ACTIVE_HALF_PLAYING);

    switch(activate_stream(data, GST_STATE_PLAYING, 2))
    {
      case ActivateStreamResult::INVALID_ITEM:
      case ActivateStreamResult::INVALID_STATE:
        MSG_BUG("Failed activating stream %u in GStreamer handler",
                data.current_stream->stream_id_);
        break;

      case ActivateStreamResult::ALREADY_ACTIVE:
        break;

      case ActivateStreamResult::ACTIVATED:
        {
            auto &sd = data.current_stream->get_stream_data();
            sd.clear_meta_data();

            const auto &cover_art_url(sd.get_cover_art_url());
            if(!cover_art_url.empty())
                TDBus::get_singleton<tdbusartcacheWrite>()
                    .call_and_forget<TDBus::ArtCacheWriteAddImageByURI>(
                        GVariantWrapper::get(sd.stream_key_),
                        140, cover_art_url.c_str());

            invalidate_position_information(data.previous_time);
            query_seconds(gst_element_query_duration, data.pipeline,
                          data.current_time.duration_s);

            emit_now_playing(TDBus::get_exported_iface<tdbussplayPlayback>(),
                             data, *data.url_fifo_LOCK_ME);
        }

        break;
    }

    if(need_push_next_stream)
    {
        data.next_stream_request = NextStreamRequestState::REQUESTED;
        queue_stream_from_url_fifo__unlocked(data, !data_lock.first,
                                             "deferred set uri");
    }
}

static void handle_buffer_underrun(StreamerData &data)
{
    if(data.stream_buffering_data.is_buffering())
    {
        msg_vinfo(MESSAGE_LEVEL_BAD_NEWS, "Buffer underrun while buffering");
        return;
    }

    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Buffer underrun detected");
    GstState current_state;
    GstState pending_state;

    switch(gst_element_get_state(data.pipeline, &current_state, &pending_state, 0))
    {
      case GST_STATE_CHANGE_SUCCESS:
      case GST_STATE_CHANGE_NO_PREROLL:
      case GST_STATE_CHANGE_ASYNC:
        break;

      case GST_STATE_CHANGE_FAILURE:
        MSG_NOT_IMPLEMENTED();
        break;
    }

    const GstState next_state = GST_STATE_TARGET(data.pipeline);

    switch(next_state)
    {
      case GST_STATE_PLAYING:
        MSG_BUG_IF(data.supposed_play_status != Streamer::PlayStatus::PLAYING,
                   "Pipeline playing, but supposed status is %d",
                   int(data.supposed_play_status));
        if(current_state != GST_STATE_PAUSED)
            set_stream_state(data.pipeline, GST_STATE_PAUSED, "fill buffer");

        data.boosted_threads_.throttle("buffering playing");
        data.stream_buffering_data.start_buffering(current_state == GST_STATE_PAUSED
                                                   ? Buffering::State::PAUSED_FOR_BUFFERING
                                                   : Buffering::State::PAUSED_PENDING);
        break;

      case GST_STATE_PAUSED:
        MSG_BUG_IF(data.supposed_play_status != Streamer::PlayStatus::PAUSED,
                   "Pipeline paused, but supposed status is %d",
                   int(data.supposed_play_status));
        data.boosted_threads_.throttle("buffering paused");
        data.stream_buffering_data.start_buffering(current_state == GST_STATE_PAUSED
                                                   ? Buffering::State::PAUSED_PIGGYBACK
                                                   : Buffering::State::PAUSED_PENDING);
        break;

      case GST_STATE_VOID_PENDING:
      case GST_STATE_NULL:
      case GST_STATE_READY:
        MSG_UNREACHABLE();
        break;
    }
}

static void handle_buffering(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), "buffering"));
    if(!data_lock.first)
        return;

    switch(data.supposed_play_status)
    {
      case Streamer::PlayStatus::STOPPED:
        msg_info("Ignoring stray buffering message from GStreamer");
        return;

      case Streamer::PlayStatus::PLAYING:
      case Streamer::PlayStatus::PAUSED:
        break;
    }

    gint percent = -1;
    gst_message_parse_buffering(message, &percent);

    if(percent < 0 || percent > 100)
    {
        msg_error(ERANGE, LOG_NOTICE, "Buffering percentage is %d%%", percent);
        return;
    }

    msg_info("Buffer level: %d%%", percent);

    switch(data.stream_buffering_data.set_buffer_level(percent))
    {
      case Buffering::LevelChange::FULL_DETECTED:
        msg_info("Buffer filled");
        try_leave_buffering_state(data);
        break;

      case Buffering::LevelChange::UNDERRUN_DETECTED:
        handle_buffer_underrun(data);
        break;

      case Buffering::LevelChange::NONE:
        break;
    }

    TDBus::get_exported_iface<tdbussplayPlayback>().emit(
        tdbus_splay_playback_emit_buffer, percent,
        data.stream_buffering_data.is_buffering() ? TRUE : FALSE);
}

static bool do_seek(GstElement *pipeline, int64_t position)
{
    msg_info("Seek to time %" PRId64 " ns", position);

    auto *seek =
        gst_event_new_seek(1.0, GST_FORMAT_TIME,
                           static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                                     GST_SEEK_FLAG_KEY_UNIT),
                           GST_SEEK_TYPE_SET, position,
                           GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
    return !!gst_element_send_event(pipeline, seek);
}

static void handle_stream_duration_async(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));
    msg_info("Prerolled");

    GstClockTime running_time;
    gst_message_parse_async_done(message, &running_time);

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), "prerolled"));
    if(!data_lock.first)
        return;

    if(running_time != GST_CLOCK_TIME_NONE)
        data.current_time.duration_s = running_time / (1000LL * 1000LL * 1000LL);
    else
        query_seconds(gst_element_query_duration, data.pipeline,
                      data.current_time.duration_s);

    if(data.current_time.duration_s < 0)
    {
        data.current_stream->disable_realtime();
        data.boosted_threads_.throttle("RT disabled for Internet radio");
    }

    if(data.initial_seek_position_ns.is_known())
    {
        do_seek(data.pipeline, data.initial_seek_position_ns.get());
        data.initial_seek_position_ns.set_unknown();
    }
}

static void handle_clock_lost_message(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    static const char context[] = "clock lost";

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), context));
    if(!data_lock.first)
        return;

    set_stream_state(data.pipeline, GST_STATE_PAUSED, context);
    set_stream_state(data.pipeline, GST_STATE_PLAYING, context);
}

static void handle_latency_message(GstMessage *message, StreamerData &data)
{
    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), "latency"));
    if(data_lock.first)
        gst_bin_recalculate_latency(GST_BIN(data.pipeline));
}

static void handle_request_state_message(GstMessage *message, StreamerData &data)
{
    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s(): %s",
              __func__, GST_MESSAGE_SRC_NAME(message));

    GstState state;
    gst_message_parse_request_state(message, &state);

    const GLibString name(gst_object_get_path_string(GST_MESSAGE_SRC(message)));
    msg_info("Setting state to %s as requested by %s",
             gst_element_state_get_name(state), name.get());

    auto data_lock(data.lock(GST_ELEMENT(GST_MESSAGE_SRC(message)), "state request"));
    if(data_lock.first)
        set_stream_state(data.pipeline, state, "requested by pipeline element");
}

/*
 * GLib signal callback: playbin3 "source-setup".
 */
static void setup_source_element(GstElement *playbin,
                                 GstElement *source, gpointer user_data)
{
    const auto &data = *static_cast<const StreamerData *>(user_data);

    if(data.is_failing)
        return;

    static const std::string soup_name("GstSoupHTTPSrc");

    if(G_OBJECT_TYPE_NAME(source) == soup_name)
    {
        if(data.soup_http_block_size > 0)
            g_object_set(source, "blocksize", data.soup_http_block_size, nullptr);
    }
}

/*
 * GStreamer callback: bus watch, GstBusFunc.
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

      case GST_MESSAGE_LATENCY:
        handle_latency_message(message, data);
        break;

      case GST_MESSAGE_REQUEST_STATE:
        handle_request_state_message(message, data);
        break;

      case GST_MESSAGE_NEW_CLOCK:
      case GST_MESSAGE_STREAM_STATUS:
      case GST_MESSAGE_DURATION_CHANGED:
      case GST_MESSAGE_RESET_TIME:
      case GST_MESSAGE_ELEMENT:
      case GST_MESSAGE_NEED_CONTEXT:
      case GST_MESSAGE_HAVE_CONTEXT:
        /* these messages are not handled, and they are explicitly ignored */
        break;

#if GST_CHECK_VERSION(1, 10, 0)
      case GST_MESSAGE_STREAM_COLLECTION:
      case GST_MESSAGE_STREAMS_SELECTED:
        /* these messages are sent by playbin3; we should try to make use of
         * them because according to the documentation, "This provides more
         * information and flexibility compared to the legacy property and
         * signal-based mechanism." */
        break;
#endif /* v1.10 */

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
      case GST_MESSAGE_REDIRECT:
#endif /* v1.10 */
#if GST_CHECK_VERSION(1, 16, 0)
      case GST_MESSAGE_DEVICE_CHANGED:
#endif /* v1.16 */
#if GST_CHECK_VERSION(1, 18, 0)
      case GST_MESSAGE_INSTANT_RATE_REQUEST:
#endif /* v1.16 */
        MSG_BUG("UNHANDLED MESSAGE TYPE %s (%u) from %s",
                GST_MESSAGE_TYPE_NAME(message),
                static_cast<unsigned int>(GST_MESSAGE_TYPE(message)),
                GST_MESSAGE_SRC_NAME(message));
        break;
    }

    return G_SOURCE_CONTINUE;
}

static inline BoostedThreads::Priority task_name_to_priority(const char *name)
{
    if(strncmp(name, "dmultiqueue", 11) == 0)
        return BoostedThreads::Priority::HIGH;

    if(strcmp(name, "aqueue:src") == 0)
        return BoostedThreads::Priority::HIGHEST;

    if(strcmp(name, "audiosink-actual-sink-alsa") == 0)
        return BoostedThreads::Priority::HIGHEST;

    return BoostedThreads::Priority::NONE;
}

static inline const char *
determine_thread_name(const GValue *val, GstElement *owner, bool is_task)
{
    if(is_task)
    {
        const auto *task = static_cast<GstTask *>(g_value_get_object(val));
        return GST_OBJECT_NAME(task);
    }
    else
        return GST_ELEMENT_NAME(owner);
}

static GstBusSyncReply
bus_sync_message_handler(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    if(GST_MESSAGE_TYPE(msg) != GST_MESSAGE_STREAM_STATUS)
        return GST_BUS_PASS;

    const GValue *val = gst_message_get_stream_status_object(msg);
    bool is_task;
    if(G_VALUE_TYPE(val) == GST_TYPE_TASK)
        is_task = true;
    else if(G_VALUE_TYPE(val) == GST_TYPE_G_THREAD)
        is_task = false;
    else
        return GST_BUS_PASS;

    GstStreamStatusType status_type;
    GstElement *owner;
    gst_message_parse_stream_status(msg, &status_type, &owner);

    switch(status_type)
    {
      case GST_STREAM_STATUS_TYPE_ENTER:
        {
            BOOSTED_THREADS_DEBUG_CODE({
                const char *const thread_name = determine_thread_name(val, owner, is_task);
                thread_observer.add(thread_name,
                                    is_task
                                    ? static_cast<const void *>(g_value_get_object(val))
                                    : static_cast<const void *>(owner));
            });
        }
        break;

      case GST_STREAM_STATUS_TYPE_LEAVE:
        BOOSTED_THREADS_DEBUG_CODE(thread_observer.leave());
        break;

      case GST_STREAM_STATUS_TYPE_DESTROY:
        BOOSTED_THREADS_DEBUG_CODE(thread_observer.destroy());
        return GST_BUS_PASS;

      case GST_STREAM_STATUS_TYPE_CREATE:
      case GST_STREAM_STATUS_TYPE_START:
      case GST_STREAM_STATUS_TYPE_PAUSE:
      case GST_STREAM_STATUS_TYPE_STOP:
        return GST_BUS_PASS;
    }

    const char *const thread_name = determine_thread_name(val, owner, is_task);

    if(thread_name == nullptr)
        return GST_BUS_PASS;

    const auto prio = task_name_to_priority(thread_name);

    if(prio != BoostedThreads::Priority::NONE)
    {
        /*
         * Threads are coming from a thread pool, so we need to avoid that
         * reused threads inherit realtime priorities by leaking them here.
         */
        auto &data = *static_cast<StreamerData *>(user_data);
        if(status_type == GST_STREAM_STATUS_TYPE_ENTER)
            data.boosted_threads_.add_self(thread_name, prio);
        else
            data.boosted_threads_.remove_self();
    }

    return GST_BUS_PASS;
}

static int create_playbin(StreamerData &data, const char *context)
{
    data.pipeline = gst_element_factory_make("playbin3", "play");
    data.bus_watch = 0;

    if(data.pipeline == nullptr)
    {
        msg_out_of_memory("playbin3");
        return -1;
    }

    data.bus_watch = gst_bus_add_watch(GST_ELEMENT_BUS(data.pipeline),
                                       bus_message_handler, &data);

    if(data.boost_streaming_thread)
        gst_bus_set_sync_handler(GST_ELEMENT_BUS(data.pipeline),
                                 bus_sync_message_handler, &data, nullptr);

    g_object_set(data.pipeline, "flags",
                 GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_BUFFERING,
                 nullptr);

    if(data.force_alsa_device != nullptr)
    {
        GstElement *sink;
#if GST_CHECK_VERSION(1, 20, 0)
        sink = gst_element_factory_make_full("alsasink",
                                             "name", "audiosink-actual-sink-alsa",
                                             "device", data.force_alsa_device->c_str(),
                                             nullptr);
#else
        sink = gst_element_factory_make("alsasink", "audiosink-actual-sink-alsa");
        g_object_set(sink, "device", data.force_alsa_device->c_str(), nullptr);
#endif /* v1.20 */

        g_object_set(data.pipeline, "audio-sink", sink, nullptr);
    }

    msg_log_assert(data.signal_handler_ids.empty());
    data.signal_handler_ids.push_back(
        g_signal_connect(data.pipeline, "about-to-finish",
                         G_CALLBACK(queue_stream_from_url_fifo), &data));

    if(data.soup_http_block_size > 0)
        data.signal_handler_ids.push_back(
            g_signal_connect(data.pipeline, "source-setup",
                            G_CALLBACK(setup_source_element), &data));

    set_stream_state(data.pipeline, GST_STATE_READY, context);

    return 0;
}

static void try_play_next_stream(StreamerData &data, bool pipeline_has_changed,
                                 PlayQueue::Queue<PlayQueue::Item> &url_fifo,
                                 GstState next_state, const char *context)
{
    bool is_next_current;
    bool is_just_queued;
    PlayQueue::Item *const next_stream =
        try_take_next(data, url_fifo, true, is_next_current, is_just_queued, context);

    if(next_stream != nullptr && (is_next_current || is_just_queued))
        play_next_stream(data, pipeline_has_changed, nullptr, *next_stream, next_state,
                         false, false, context);
}

static bool do_stop(StreamerData &data, const char *context,
                    const GstState pending, bool &failed_hard)
{
    msg_log_assert(data.pipeline != nullptr);

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

            LOGGED_LOCK_CONTEXT_HINT;
            data.url_fifo_LOCK_ME->locked_rw(
                [] (PlayQueue::Queue<PlayQueue::Item> &fifo) { fifo.clear(0); });
        }
        else
            data.fail.clear_fifo_on_error = true;

        break;

      case GST_STATE_READY:
      case GST_STATE_NULL:
        LOGGED_LOCK_CONTEXT_HINT;
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

static StreamerData streamer_data;

int Streamer::setup(GMainLoop *loop, guint soup_http_block_size,
                    bool boost_streaming_thread,
                    const std::string &force_alsa_device)
{
    static const char context[] = "setup";

    BOOSTED_THREADS_DEBUG_CODE({
        thread_observer.add("Main", nullptr);
        thread_observer.dump(context);
    });

    streamer_data.soup_http_block_size = soup_http_block_size;
    streamer_data.boost_streaming_thread = boost_streaming_thread;
    streamer_data.force_alsa_device = force_alsa_device.empty() ? nullptr : &force_alsa_device;

    if(create_playbin(streamer_data, context) < 0)
        return -1;

    streamer_data.system_clock = gst_system_clock_obtain();
    streamer_data.next_allowed_tag_update_time =
        gst_clock_get_time(streamer_data.system_clock);

    static bool initialized;

    if(!initialized)
        initialized = true;
    else
        msg_log_assert(false);

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

    about_to_wipe_current_stream(streamer_data.current_stream.get(), true, "shutdown");
    streamer_data.current_stream.reset();
}

void Streamer::activate()
{
    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, "activate"));

    if(streamer_data.is_player_activated)
        MSG_BUG("Already activated");
    else
    {
        msg_info("Activated");
        streamer_data.is_player_activated = true;
    }
}

void Streamer::deactivate()
{
    static const char context[] = "deactivate";

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, context));

    if(!streamer_data.is_player_activated)
        MSG_BUG("Already deactivated");
    else
    {
        msg_info("Deactivating as requested");
        streamer_data.is_player_activated = false;

        const GstState pending = GST_STATE_PENDING(streamer_data.pipeline);
        bool dummy_failed_hard;
        do_stop(streamer_data, context, pending, dummy_failed_hard);

        msg_info("Deactivated");
    }
}

bool Streamer::start(const char *reason)
{
    static const char context[] = "start playing";

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, context));

    if(!streamer_data.is_player_activated)
    {
        MSG_BUG("Start request while inactive (%s)", reason);
        return false;
    }

    msg_info("Starting as requested (%s)", reason);

    msg_log_assert(streamer_data.pipeline != nullptr);

    streamer_data.supposed_play_status = Streamer::PlayStatus::PLAYING;

    if(streamer_data.stream_buffering_data.is_buffering())
    {
        msg_info("Play request deferred, we are buffering");
        return true;
    }

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
            rebuild_playbin_for_workarounds(streamer_data, context);

            LOGGED_LOCK_CONTEXT_HINT;
            streamer_data.url_fifo_LOCK_ME->locked_rw(
                [&data_lock] (PlayQueue::Queue<PlayQueue::Item> &fifo)
                {
                    try_play_next_stream(streamer_data, !data_lock.first, fifo,
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

        break;
    }

    return true;
}

static bool is_pipeline_uri_empty(GstElement *pipeline)
{
    gchar *temp = nullptr;
    g_object_get(pipeline, "uri", &temp, nullptr);
    const bool result = (temp == nullptr || temp[0] == '\0');
    g_free(temp);
    return result;
}

bool Streamer::stop(const char *reason)
{
    static const char context[] = "stop playing";

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, context));

    if(!streamer_data.is_player_activated)
    {
        MSG_BUG("Stop request while inactive (%s)", reason);
        return false;
    }

    if(is_pipeline_uri_empty(streamer_data.pipeline))
    {
        msg_info("Ignored stop request with uri property (%s)", reason);
        return false;
    }

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
        LOGGED_LOCK_CONTEXT_HINT;
        streamer_data.url_fifo_LOCK_ME->locked_rw(
            []
            (PlayQueue::Queue<PlayQueue::Item> &fifo)
            {
                about_to_wipe_current_stream(streamer_data.current_stream.get(), true,
                                             "stop request");
                emit_stopped_with_error(TDBus::get_exported_iface<tdbussplayPlayback>(),
                                        streamer_data, fifo,
                                        StoppedReasons::Reason::ALREADY_STOPPED,
                                        std::move(streamer_data.current_stream));
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
bool Streamer::pause(const char *reason)
{
    static const char context[] = "pause stream";

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, context));

    if(!streamer_data.is_player_activated)
    {
        MSG_BUG("Pause request while inactive (%s)", reason);
        return false;
    }

    if(is_pipeline_uri_empty(streamer_data.pipeline))
    {
        msg_info("Ignored pause request with uri property (%s)", reason);
        return false;
    }

    msg_info("Pausing as requested (%s)", reason);
    msg_log_assert(streamer_data.pipeline != nullptr);

    streamer_data.supposed_play_status = Streamer::PlayStatus::PAUSED;

    if(streamer_data.stream_buffering_data.is_buffering())
    {
        msg_info("Pause request deferred, we are buffering");
        return true;
    }

    const GstState state = GST_STATE(streamer_data.pipeline);

    switch(state)
    {
      case GST_STATE_PAUSED:
        break;

      case GST_STATE_NULL:
        LOGGED_LOCK_CONTEXT_HINT;
        streamer_data.url_fifo_LOCK_ME->locked_rw(
            [&data_lock] (PlayQueue::Queue<PlayQueue::Item> &fifo)
            {
                try_play_next_stream(streamer_data, !data_lock.first, fifo,
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

static inline gint64 query_seek_duration(GstElement *pipeline)
{
    if(pipeline == nullptr)
        return INT64_MIN;

    GstQuery *q = gst_query_new_seeking(GST_FORMAT_TIME);
    if(!gst_element_query(pipeline, q))
    {
        gst_query_unref(q);
        return INT64_MIN;
    }

    gboolean is_seekable;
    gint64 duration_ns;
    gst_query_parse_seeking(q, nullptr, &is_seekable, nullptr, &duration_ns);
    gst_query_unref(q);

    return is_seekable && duration_ns > 0 ? duration_ns : INT64_MIN;
}

bool Streamer::seek(int64_t position, const char *units)
{
    if(position < 0)
    {
        msg_error(EINVAL, LOG_ERR, "Negative seeks not supported");
        return false;
    }

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, "seek"));

    if(!streamer_data.is_player_activated)
    {
        MSG_BUG("Seek request while inactive");
        return false;
    }

    const gint64 duration_ns = query_seek_duration(streamer_data.pipeline);
    if(duration_ns < 0)
    {
        msg_error(EINVAL, LOG_ERR, "Cannot seek, duration unknown");
        return false;
    }

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

    if(!do_seek(streamer_data.pipeline, position))
        return false;

    do_encourage_buffering(streamer_data);
    return true;
}

Streamer::PlayStatus Streamer::next(bool skip_only_if_not_stopped,
                                    uint32_t &out_skipped_id, uint32_t &out_next_id)
{
    static const char context[] = "skip to next";

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, context));

    if(!streamer_data.is_player_activated)
    {
        MSG_BUG("Next request while inactive");
        return Streamer::PlayStatus::STOPPED;
    }

    msg_info("Next requested");
    msg_log_assert(streamer_data.pipeline != nullptr);

    if(skip_only_if_not_stopped && streamer_data.current_stream != nullptr)
    {
        switch(streamer_data.current_stream->get_state())
        {
          case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
          case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
            streamer_data.current_stream->set_state(PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED);
            LOGGED_LOCK_CONTEXT_HINT;
            streamer_data.url_fifo_LOCK_ME->locked_rw(
                [id = streamer_data.current_stream->stream_id_]
                (PlayQueue::Queue<PlayQueue::Item> &fifo) { fifo.mark_as_dropped(id); });
            break;

          case PlayQueue::ItemState::IN_QUEUE:
          case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
          case PlayQueue::ItemState::ABOUT_TO_PHASE_OUT:
          case PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED:
            about_to_wipe_current_stream(streamer_data.current_stream.get(), true,
                                         "next request");
            streamer_data.current_stream.reset();
            break;
        }
    }

    const bool is_dequeuing_permitted =
        (streamer_data.supposed_play_status != Streamer::PlayStatus::STOPPED ||
         !skip_only_if_not_stopped);
    uint32_t skipped_id = streamer_data.current_stream != nullptr
        ? streamer_data.current_stream->stream_id_
        : UINT32_MAX;

    bool is_next_current = false;
    PlayQueue::Item *next_stream = nullptr;
    LOGGED_LOCK_CONTEXT_HINT;
    LoggedLock::UniqueLock<LoggedLock::RecMutex> queue_lock;

    if(is_dequeuing_permitted)
    {
        LOGGED_LOCK_CONTEXT_HINT;
        queue_lock = streamer_data.url_fifo_LOCK_ME->lock();
        bool dummy;
        next_stream = try_take_next(streamer_data, *streamer_data.url_fifo_LOCK_ME,
                                    true, is_next_current, dummy, context);
    }

    uint32_t next_id = UINT32_MAX;

    if(next_stream != nullptr && !is_next_current)
    {
        if(streamer_data.current_stream == nullptr)
            MSG_BUG("[%s] Have no current stream", context);
        else
        {
            switch(streamer_data.current_stream->get_state())
            {
              case PlayQueue::ItemState::IN_QUEUE:
                MSG_BUG("[%s] Wrong state %s of current stream",
                        context,
                        PlayQueue::item_state_name(streamer_data.current_stream->get_state()));
                break;

              case PlayQueue::ItemState::ABOUT_TO_ACTIVATE:
              case PlayQueue::ItemState::ACTIVE_HALF_PLAYING:
              case PlayQueue::ItemState::ACTIVE_NOW_PLAYING:
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

            if(play_next_stream(streamer_data, !data_lock.first,
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

void Streamer::clear_queue(int keep_first_n_entries,
                           GVariantWrapper &queued, GVariantWrapper &dropped)
{
    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, "clear queue"));

    LOGGED_LOCK_CONTEXT_HINT;
    streamer_data.url_fifo_LOCK_ME->locked_rw(
        [keep_first_n_entries, &queued, &dropped]
        (PlayQueue::Queue<PlayQueue::Item> &fifo)
        {
            if(keep_first_n_entries >= 0)
                fifo.clear(keep_first_n_entries);

            queued = mk_id_array_from_queued_items(fifo);
            dropped = mk_id_array_from_dropped_items(fifo);
        });
}

bool Streamer::is_playing()
{
    return streamer_data.locked(
                [] (StreamerData &d) { return GST_STATE(d.pipeline) == GST_STATE_PLAYING; });
}

bool Streamer::get_current_stream_id(stream_id_t &id)
{
    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, "get current ID"));

    if(streamer_data.current_stream != nullptr &&
       !streamer_data.current_stream->empty())
    {
        id = streamer_data.current_stream->stream_id_;
        return true;
    }

    return false;
}

bool Streamer::push_item(stream_id_t stream_id, GVariantWrapper &&stream_key,
                         URLCollection::StreamURLs &&stream_urls,
                         GVariantWrapper &&meta_data, size_t keep_items,
                         GVariantWrapper &out_dropped_ids_before,
                         GVariantWrapper &out_dropped_ids_now)
{
    static const Timebase timebase;

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, "push item"));
    bool is_active = streamer_data.is_player_activated;

    if(!is_active)
    {
        MSG_BUG("Push request while inactive");
        return false;
    }

    streamer_data.url_fifo_LOCK_ME->locked_rw(
        [&out_dropped_ids_before] (auto &fifo)
        { out_dropped_ids_before = std::move(mk_id_array_from_dropped_items(fifo)); }
    );

    std::string cover_art_url;
    std::unordered_map<std::string, std::string> extra_tags;
    auto *list = g_variant_to_tag_list(std::move(meta_data), cover_art_url, extra_tags);
    auto item(std::make_unique<PlayQueue::Item>(
            stream_id, std::move(stream_key), std::move(stream_urls), true,
            std::move(cover_art_url), std::move(extra_tags), list,
            std::chrono::time_point<std::chrono::nanoseconds>::min(),
            std::chrono::time_point<std::chrono::nanoseconds>::max(),
            timebase));

    if(item == nullptr)
    {
        msg_out_of_memory("stream item");
        return false;
    }

    LOGGED_LOCK_CONTEXT_HINT;
    return streamer_data.url_fifo_LOCK_ME->locked_rw(
                [&data_lock, &item, &keep_items, &out_dropped_ids_now]
                (PlayQueue::Queue<PlayQueue::Item> &fifo)
                {
                    if(fifo.push(std::move(item), keep_items) == 0)
                        return false;

                    switch(streamer_data.next_stream_request)
                    {
                      case NextStreamRequestState::REQUESTED:
                        if(streamer_data.current_stream != nullptr &&
                           streamer_data.current_stream->get_state() == PlayQueue::ItemState::ABOUT_TO_PHASE_OUT)
                            queue_stream_from_url_fifo__unlocked(streamer_data,
                                                                 !data_lock.first,
                                                                 "immediately queued on push");
                        break;

                      case NextStreamRequestState::REQUEST_DEFERRED:
                      case NextStreamRequestState::NOT_REQUESTED:
                        break;
                    }

                    out_dropped_ids_now = std::move(mk_id_array_from_dropped_items(fifo));
                    return true;
                });
}

bool Streamer::remove_items_for_root_path(const char *root_path)
{
    const auto filename_from_uri = [] (const std::string &url) -> GLibString
    {
        GErrorWrapper gerror;
        GLibString filename(g_filename_from_uri(url.c_str(), nullptr, gerror.await()));

        if(filename == nullptr)
        {
            msg_error(0, LOG_EMERG, "Error while extracting file name from uri: '%s'" ,
                      (gerror.failed() && gerror->message) ? gerror->message : "N/A");
            gerror.noticed();
        }

        return filename;
    };

    auto realpath_cxx = [] (GLibString &&file_path) -> std::string
    {
        std::string buf;

        if(file_path.size() > PATH_MAX)
        {
            msg_error(0, LOG_EMERG, "Path too long for realpath(): '%s'",
                      file_path.get());
            return buf;
        }

        buf.resize(PATH_MAX);

        if(realpath(file_path.get(), &buf[0]) == nullptr)
            msg_error(0, LOG_EMERG, "Error while realpath(%s): '%s'" ,
                      file_path.get(), strerror(errno));

        return buf;
    };

    auto starts_with = [] (const std::string &s, const std::string &prefix) -> bool
    {
        return s.size() > prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    };

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, "fs path removed"));

    if(streamer_data.is_player_activated && streamer_data.current_stream != nullptr)
    {
        const auto &url = streamer_data.current_stream->get_url_for_playing();
        if(starts_with(url, "file://"))
        {
            auto filename(filename_from_uri(url));
            if(filename.empty())
                return false;

            const auto &file_path_real = realpath_cxx(std::move(filename));
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

static gboolean post_injected_failure(gpointer user_data)
{
    auto *pcbdata = static_cast<PipelineCallbackData *>(user_data);
    Guard guard([pcbdata] { delete pcbdata; });

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(pcbdata->data_.lock(pcbdata->pipeline_,
                                       "do inject failure"));

    if(!data_lock.first)
    {
        msg_error(0, LOG_NOTICE,
                  "Failed injecting error: pipeline has changed");
        return G_SOURCE_REMOVE;
    }

    data_lock.second.unlock();

    /* don't want to hold any locks, just to be completely safe... */
    gst_element_post_message(pcbdata->pipeline_, pcbdata->message_);

    return G_SOURCE_REMOVE;
}

void Streamer::inject_stream_failure(const char *domain, unsigned int code)
{
    GQuark equark;

    if(strcmp(domain, "core") == 0)
        equark = GST_CORE_ERROR;
    else if(strcmp(domain, "library") == 0)
        equark = GST_LIBRARY_ERROR;
    else if(strcmp(domain, "resource") == 0)
        equark = GST_RESOURCE_ERROR;
    else if(strcmp(domain, "stream") == 0)
        equark = GST_STREAM_ERROR;
    else
    {
        msg_error(EINVAL, LOG_NOTICE, "Unknown domain name %s", domain);
        return;
    }

    LOGGED_LOCK_CONTEXT_HINT;
    auto data_lock(streamer_data.lock(nullptr, "inject failure"));

    if(streamer_data.pipeline == nullptr)
    {
        msg_error(0, LOG_NOTICE,
                  "Failed injecting stream failure: pipeline is NULL");
        return;
    }

    GError *error = g_error_new(equark, code, "Injected stream failure");
    GstElement *failing_element = nullptr;
    g_object_get(GST_OBJECT(streamer_data.pipeline),
                 "audio-sink", &failing_element, nullptr);
    GstMessage *msg = gst_message_new_error(GST_OBJECT(failing_element),
                                            error, "Artificially generated error");
    g_object_unref(failing_element);
    g_error_free(error);

    /* let's be extra careful and not post from here */
    g_idle_add(post_injected_failure,
               new PipelineCallbackData(streamer_data, msg));
}
