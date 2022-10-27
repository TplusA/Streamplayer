/*
 * Copyright (C) 2018, 2020--2023  T+A elektroakustik GmbH & Co. KG
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

#ifndef PLAYITEM_HH
#define PLAYITEM_HH

#include <string>
#include <chrono>
#include <functional>
#include <memory>

#include "streamdata.hh"
#include "stream_id.h"
#include "url_collection.hh"
#include "stopped_reasons.hh"
#include "strbo_url.hh"
#include "rnfcall_resolve_airable_redirect.hh"

namespace PlayQueue
{

enum class ItemState
{
    IN_QUEUE,
    ABOUT_TO_ACTIVATE,
    ACTIVE_HALF_PLAYING,
    ACTIVE_NOW_PLAYING,
    ABOUT_TO_PHASE_OUT,
    ABOUT_TO_BE_SKIPPED,

    LAST_VALUE = ABOUT_TO_BE_SKIPPED,
};

enum class FailState
{
    NOT_FAILED,
    FAILURE_DETECTED,

    LAST_VALUE = FAILURE_DETECTED,
};

enum class URLState
{
    KNOWN_DIRECT_URL,   /*!< Regular, direct URL for the stream */
    KNOWN_AIRABLE_LINK, /*!< Airable link */
    RESOLVING_LINK,     /*!< Currently resolving Airable link */
    KNOWN_RESOLVED_URL, /*!< Direct URL retrieved from Airable */
    BROKEN,             /*!< Unrecoverable error */

    LAST_VALUE = BROKEN,
};

/*!
 * Convert enum to printable string for diagnostic purposes.
 */
const char *item_state_name(ItemState state);

/*!
 * Convert enum to printable string for diagnostic purposes.
 */
const char *fail_state_name(FailState state);

/*!
 * Convert enum to printable string for diagnostic purposes.
 */
const char *url_state_name(URLState state);

/*!
 * URL FIFO item data.
 */
class Item
{
  public:
    using stream_id_t = ::stream_id_t;

  private:
    ItemState state_;
    FailState fail_state_;
    StoppedReasons::Reason prefail_reason_;

  public:
    const stream_id_t stream_id_;

  private:
    URLCollection::StreamURLs stream_urls_;
    std::shared_ptr<DBusRNF::ResolveAirableRedirectCall> resolver_;
    bool pipeline_start_required_after_resolve_;
    bool enable_realtime_processing_;
    const StreamType stream_type_;

  public:
    const std::chrono::time_point<std::chrono::nanoseconds> start_time_;
    const std::chrono::time_point<std::chrono::nanoseconds> end_time_;

  private:
    StreamData stream_data_;
    const Timebase &timebase_;

  public:
    Item(const Item &) = delete;
    Item &operator=(const Item &) = delete;

    /*!
     * \param stream_id
     *     The stream ID associated with this item.
     * \param stream_key
     *     Opaque key identifying th stream, passed on to the cover art caching
     *     daemon whenever cover arts are discovered.
     * \param stream_urls
     *     The stream URLs requested to play.
     * \param enable_realtime_processing
     *     Enable real-time priorities of streaming threads for this stream.
     * \param cover_art_url
     *     URL of cover art (may be empty).
     * \param extra_tags
     *     Non-standard tags.
     * \param preset_tag_list
     *     Preset meta data for the stream as pulled from some external data
     *     source.
     * \param start_time, end_time
     *     The start and stop positions of a stretch to be played. Pass
     *     \c std::chrono::time_point::min() and
     *     \c std::chrono::time_point::max(), respectively, to play the whole
     *     stream from its natural start to its natural end.
     * \param timebase
     *     How to get the current time.
     */
    explicit Item(const stream_id_t &stream_id, GVariantWrapper &&stream_key,
                  URLCollection::StreamURLs &&stream_urls,
                  bool enable_realtime_processing,
                  std::string &&cover_art_url,
                  std::unordered_map<std::string, std::string> &&extra_tags,
                  GstTagList *preset_tag_list,
                  std::chrono::time_point<std::chrono::nanoseconds> &&start_time,
                  std::chrono::time_point<std::chrono::nanoseconds> &&end_time,
                  const Timebase &timebase):
        state_(ItemState::IN_QUEUE),
        fail_state_(FailState::NOT_FAILED),
        prefail_reason_(StoppedReasons::Reason::UNKNOWN),
        stream_id_(stream_id),
        stream_urls_(std::move(stream_urls)),
        pipeline_start_required_after_resolve_(false),
        enable_realtime_processing_(enable_realtime_processing),
        stream_type_(stream_urls_.empty()
                     ? StreamType::EMPTY
                     : StrBoURL::determine_stream_type_from_url(stream_urls_[0])),
        start_time_(std::move(start_time)),
        end_time_(std::move(end_time)),
        stream_data_(preset_tag_list, std::move(cover_art_url),
                     std::move(extra_tags), std::move(stream_key)),
        timebase_(timebase)
    {}

    void set_state(ItemState state) { state_ = state; }
    ItemState get_state() const { return state_; }

    void disable_realtime() { enable_realtime_processing_ = false; }
    bool is_realtime_processing_allowed() const { return enable_realtime_processing_; }

    /*!
     * Failure detected outside of GStreamer error message handler.
     */
    bool prefail(StoppedReasons::Reason reason);
    bool has_prefailed() const;
    StoppedReasons::Reason get_prefail_reason() const;

    /*!
     * Set failure.
     */
    bool fail();

    const StreamData &get_stream_data() const { return stream_data_; }
    StreamData &get_stream_data() { return stream_data_; }

    URLState get_selected_url_state() const;
    bool select_next_url();

    bool empty() const { return stream_urls_.empty(); }

    const std::string &get_url_for_playing() const
    {
        bool dummy;
        return stream_urls_.get_selected().get_stream_uri(dummy);
    }

    const std::string &get_url_for_reporting() const
    {
        return stream_urls_.get_selected().get_original_url_string();
    }

    bool is_network_stream() const { return stream_type_ != StreamType::LOCAL_FILE; }

    bool is_pipeline_start_required()
    {
        const bool result = pipeline_start_required_after_resolve_;
        pipeline_start_required_after_resolve_ = false;
        return result;
    }

    enum class PipelineStartRequired
    {
        NOT_REQUIRED,
        START_NOW,
        WHEN_READY,
    };

    PipelineStartRequired pipeline_start_required_when_ready();

    enum class ResolverResult
    {
        RESOLVED,
        HAVE_MORE_URLS,
        FAILED,
    };

    void resolver_begin(DBusRNF::CookieManagerIface &cm,
                        std::unique_ptr<DBusRNF::ContextData> context_data);
    ResolverResult resolver_finish(std::string &&uri,
                                   std::chrono::seconds &&expected_valid);
    ResolverResult resolver_finish(PlayQueue::URLState url_state);
};

}

#endif /* !PLAYITEM_HH */
