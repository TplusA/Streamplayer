/*
 * Copyright (C) 2018  T+A elektroakustik GmbH & Co. KG
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

#ifndef PLAYITEM_HH
#define PLAYITEM_HH

#include <string>
#include <chrono>
#include <functional>
#include <memory>

#include "streamdata.hh"
#include "stream_id.h"

namespace PlayQueue
{

enum class ItemState
{
    IN_QUEUE,
    ABOUT_TO_ACTIVATE,
    ACTIVE,
    ABOUT_TO_PHASE_OUT,
    ABOUT_TO_BE_SKIPPED,

    LAST_ITEM_STATE = ABOUT_TO_BE_SKIPPED,
};

enum class FailState
{
    NOT_FAILED,
    FAILURE_DETECTED,

    LAST_FAIL_STATE = FAILURE_DETECTED,
};

/*!
 * URL FIFO item data.
 */
class Item
{
  private:
    ItemState state_;
    FailState fail_state_;

  public:
    const stream_id_t stream_id_;
    const std::string url_;
    const std::chrono::time_point<std::chrono::nanoseconds> start_time_;
    const std::chrono::time_point<std::chrono::nanoseconds> end_time_;

  private:
    StreamData stream_data_;

  public:
    Item(const Item &) = delete;
    Item &operator=(const Item &) = delete;

    /*!
     * \param stream_id
     *     The stream ID associated with this item.
     * \param stream_key
     *     Opaque key identifying th stream, passed on to the cover art caching
     *     daemon whenever cover arts are discovered.
     * \param stream_url
     *     The stream URL requested to play.
     * \param start_time, end_time
     *     The start and stop positions of a stretch to be played. Pass
     *     \c std::chrono::time_point::min() and
     *     \c std::chrono::time_point::max(), respectively, to play the whole
     *     stream from its natural start to its natural end.
     */
    explicit Item(stream_id_t stream_id, GVariantWrapper &&stream_key,
                  std::string &&stream_url,
                  std::chrono::time_point<std::chrono::nanoseconds> &&start_time,
                  std::chrono::time_point<std::chrono::nanoseconds> &&end_time):
        state_(ItemState::IN_QUEUE),
        fail_state_(FailState::NOT_FAILED),
        stream_id_(stream_id),
        url_(std::move(stream_url)),
        start_time_(std::move(start_time)),
        end_time_(std::move(end_time)),
        stream_data_(std::move(stream_key))
    {}

    void set_state(ItemState state) { state_ = state; }
    ItemState get_state() const { return state_; }

    /*!
     * Set failure.
     */
    bool fail();

    const StreamData &get_stream_data() const { return stream_data_; }
    StreamData &get_stream_data() { return stream_data_; }
};

/*!
 * Convert enum to printable string for diagnostic purposes.
 */
const char *item_state_name(ItemState state);

/*!
 * Convert enum to printable string for diagnostic purposes.
 */
const char *fail_state_name(FailState state);

}

#endif /* !PLAYITEM_HH */