/*
 * Copyright (C) 2022  T+A elektroakustik GmbH & Co. KG
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

#ifndef QUEUE_FILTER_HH
#define QUEUE_FILTER_HH

#include "urlfifo.hh"
#include "playitem.hh"
#include "stopped_reasons.hh"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

struct _GstDiscoverer;

class QueueFilter
{
  public:
    using UnplayableStreamRemovedNotification =
        std::function<void(stream_id_t, StoppedReasons::Reason)>;

  private:
    enum class State
    {
        ACTIVE,
        PAUSED,
        SHUTTING_DOWN,
    };

    std::mutex lock_;
    std::condition_variable resume_event_;

    std::thread thread_;
    State thread_state_;
    bool queue_has_new_items_;

    PlayQueue::Queue<PlayQueue::Item> &queue_;
    const UnplayableStreamRemovedNotification notify_unplayable_stream_removed_;

    struct _GstDiscoverer *disco_;

  public:
    QueueFilter(const QueueFilter &) = delete;
    QueueFilter(QueueFilter &&) = default;
    QueueFilter &operator=(const QueueFilter &) = delete;
    QueueFilter &operator=(QueueFilter &&) = default;

    explicit QueueFilter(PlayQueue::Queue<PlayQueue::Item> &queue,
                         UnplayableStreamRemovedNotification &&notify_unplayable_stream_removed_fn):
        thread_state_(State::ACTIVE),
        queue_has_new_items_(false),
        queue_(queue),
        notify_unplayable_stream_removed_(std::move(notify_unplayable_stream_removed_fn)),
        disco_(nullptr)
    {}

    void init();
    void shutdown();

    /* stopping and resuming to reduce CPU consumption during known peaks */
    void pause_thread();
    void resume_thread();

    void queue_item_added();

  private:
    void thread_main();
};

#endif /* !QUEUE_FILTER_HH */
