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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include "queue_filter.hh"
#include "stream_id.hh"
#include "gerrorwrapper.hh"
#include "guard.hh"

#include <algorithm>

#include <gst/pbutils/pbutils.h>

static bool find_unprobed_stream(PlayQueue::Queue<PlayQueue::Item> &queue,
                                 ID::Stream &sid, std::string &url)
{
    sid = ID::Stream::make_invalid();

    queue.locked_ro(
        [&sid, &url] (const PlayQueue::Queue<PlayQueue::Item> &fifo)
        {
            msg_info("Searching for unprobed items in queue of size %zu",
                     fifo.size());

            const auto &it =
                std::find_if(fifo.begin(), fifo.end(),
                    [] (const auto &item) { return !item->was_probed(); });
            if(it != fifo.end())
            {
                sid = ID::Stream::make_from_raw_id((*it)->stream_id_);
                url = (*it)->get_url_for_playing();
            }
        }
    );

    return sid.is_valid();
}

static void mark_stream_good(PlayQueue::Queue<PlayQueue::Item> &queue,
                             ID::Stream sid, const GstTagList *tags)
{
    queue.locked_rw(
        [sid, tags] (PlayQueue::Queue<PlayQueue::Item> &fifo)
        {
            const auto &it =
                std::find_if(fifo.begin(), fifo.end(),
                    [sid] (const auto &item) { return item->stream_id_ == sid.get_raw_id(); });
            if(it != fifo.end())
            {
                msg_info("Stream %u seems to be playable", sid.get_raw_id());
                (*it)->filter_passed();
                (*it)->get_stream_data().merge_tag_list(const_cast<GstTagList *>(tags), false);
            }
            else
                msg_info("Stream %u probed (playable), but not in queue anymore",
                         sid.get_raw_id());
        }
    );
}

static bool remove_stream(PlayQueue::Queue<PlayQueue::Item> &queue,
                          ID::Stream sid)
{
    return queue.locked_rw(
        [sid] (PlayQueue::Queue<PlayQueue::Item> &fifo)
        {
            const auto &it =
                std::find_if(fifo.begin(), fifo.end(),
                    [sid] (const auto &item) { return item->stream_id_ == sid.get_raw_id(); });
            if(it != fifo.end())
            {
                const bool in_queue =
                    (*it)->get_state() == PlayQueue::ItemState::IN_QUEUE;

                msg_info("Stream %u is not playable%s", sid.get_raw_id(),
                         in_queue ? "" : ", but already in gapless pipeline (expect audible interruption)");

                if(in_queue)
                {
                    fifo.erase_directly(it);
                    return true;
                }
            }
            else
                msg_info("Stream %u probed (not playable), but not in queue anymore",
                         sid.get_raw_id());

            return false;
        }
    );
}

static const GstTagList *
probe_stream(GstDiscoverer *disco, const std::string &url,
             StoppedReasons::Reason &stopped_reason)
{
    GErrorWrapper err;
    GstDiscovererInfo *info =
        gst_discoverer_discover_uri(disco, url.c_str(), err.await());
    err.log_failure("QueueFilter::init()");

    if(info == nullptr)
    {
        stopped_reason = StoppedReasons::Reason::URL_MISSING;
        return nullptr;
    }

    Guard free_data([info] { gst_discoverer_info_unref(info); });

    switch(gst_discoverer_info_get_result(info))
    {
      case GST_DISCOVERER_OK:
        stopped_reason = StoppedReasons::Reason::WRONG_TYPE;
        break;

      case GST_DISCOVERER_URI_INVALID:
        stopped_reason = StoppedReasons::Reason::DOES_NOT_EXIST;
        return nullptr;

      case GST_DISCOVERER_ERROR:
        stopped_reason = StoppedReasons::from_gerror(
                err, StoppedReasons::determine_is_local_error_by_url(url));
        return nullptr;

      case GST_DISCOVERER_TIMEOUT:
        stopped_reason = StoppedReasons::Reason::NET_IO;
        return nullptr;

      case GST_DISCOVERER_BUSY:
        BUG("Unexpected discoverer failure: busy");
        stopped_reason = StoppedReasons::Reason::PERMISSION_DENIED;
        return nullptr;

      case GST_DISCOVERER_MISSING_PLUGINS:
        stopped_reason = StoppedReasons::Reason::MISSING_CODEC;
        return nullptr;
    }

    const GstTagList *tags = gst_discoverer_info_get_tags(info);
    if(tags == nullptr)
        return nullptr;

    if(gst_tag_list_get_tag_size(tags, "audio-codec") == 0)
        return nullptr;

    return tags;
}

void QueueFilter::thread_main()
{
    std::unique_lock<std::mutex> lk(lock_);

    for(;;)
    {
        msg_info("Waiting for streams to probe");
        resume_event_.wait(lk,
            [this]
            {
                return (thread_state_ == State::ACTIVE && queue_has_new_items_) ||
                       thread_state_ == State::SHUTTING_DOWN;
            });

        queue_has_new_items_ = false;

        if(thread_state_ == State::SHUTTING_DOWN)
            return;

        ID::Stream sid(ID::Stream::make_invalid());
        std::string url;

        while(thread_state_ == State::ACTIVE)
        {
            lk.unlock();

            if(!find_unprobed_stream(queue_, sid, url))
            {
                msg_info("No streams found that need probing");
                lk.lock();
                break;
            }

            msg_info("Probing stream %u at %s", sid.get_raw_id(), url.c_str());

            StoppedReasons::Reason stopped_reason;
            const GstTagList *tags = probe_stream(disco_, url, stopped_reason);

            if(tags != nullptr)
                mark_stream_good(queue_, sid, tags);
            else if(remove_stream(queue_, sid))
                notify_unplayable_stream_removed_(sid.get_raw_id(),
                                                  stopped_reason);

            lk.lock();
        }
    }
}

void QueueFilter::init()
{
    GErrorWrapper err;
    disco_ = gst_discoverer_new(8 * GST_SECOND, err.await());
    if(err.log_failure("QueueFilter::init()"))
        return;

    g_object_set(disco_, "use-cache", FALSE, nullptr);

    thread_ = std::thread(&QueueFilter::thread_main, this);

    const struct sched_param prio { sched_get_priority_min(SCHED_IDLE) };
    pthread_setschedparam(thread_.native_handle(), SCHED_IDLE, &prio);
    pthread_setname_np(thread_.native_handle(), "queue_filter");
}

void QueueFilter::shutdown()
{
    if(disco_ == nullptr)
        return;

    {
        std::lock_guard<std::mutex> lk(lock_);
        thread_state_ = State::SHUTTING_DOWN;
    }

    resume_event_.notify_one();
    thread_.join();
    g_object_unref(disco_);
    disco_ = nullptr;
}

void QueueFilter::pause_thread()
{
    std::lock_guard<std::mutex> lk(lock_);

    switch(thread_state_)
    {
      case State::ACTIVE:
        thread_state_ = State::PAUSED;
        break;

      case State::PAUSED:
      case State::SHUTTING_DOWN:
        break;
    }
}

void QueueFilter::resume_thread()
{
    {
        std::lock_guard<std::mutex> lk(lock_);

        switch(thread_state_)
        {
          case State::ACTIVE:
            return;

          case State::PAUSED:
            thread_state_ = State::ACTIVE;
            break;

          case State::SHUTTING_DOWN:
            break;
        }
    }

    resume_event_.notify_one();
}

void QueueFilter::queue_item_added()
{
    {
        std::lock_guard<std::mutex> lk(lock_);

        queue_has_new_items_ = true;

        switch(thread_state_)
        {
          case State::ACTIVE:
          case State::SHUTTING_DOWN:
            break;

          case State::PAUSED:
            return;
        }
    }

    resume_event_.notify_one();
}
