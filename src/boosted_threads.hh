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

#ifndef BOOSTED_THREADS_HH
#define BOOSTED_THREADS_HH

#define BOOSTED_THREADS_DEBUG 0

#if BOOSTED_THREADS_DEBUG
#define BOOSTED_THREADS_DEBUG_CODE(CODE) do { CODE; } while(0)
#else /* !BOOSTED_THREADS_DEBUG */
#define BOOSTED_THREADS_DEBUG_CODE(CODE) do {} while(0)
#endif /* BOOSTED_THREADS_DEBUG */

#include <pthread.h>

#include <string>
#include <unordered_map>
#include <mutex>

namespace BoostedThreads
{

class Threads
{
  private:
    std::mutex lock_;

    int default_sched_policy_;
    int default_sched_priority_;
    int boosted_sched_policy_;
    int boosted_sched_priority_;

    std::unordered_map<pthread_t, std::string> threads_;

    bool is_boost_enabled_;

  public:
    Threads(const Threads &) = delete;
    Threads &operator=(const Threads &) = delete;

    explicit Threads();

    void boost(const char *context);
    void throttle(const char *context);

    void add_self(std::string &&name);
    void remove_self();

  private:
    void configure_thread(pthread_t tid, const std::string &name,
                          bool is_boosted, const char *context) const;
};

}

#if BOOSTED_THREADS_DEBUG

#include <tuple>
#include <vector>

#include "messages.h"

namespace BoostedThreads
{

class ThreadObserver
{
  private:
    std::mutex lock_;
    std::unordered_map<pthread_t, std::tuple<std::string, const void *, bool>> threads_;

  public:
    ThreadObserver(const ThreadObserver &) = delete;
    ThreadObserver &operator=(const ThreadObserver &) = delete;
    explicit ThreadObserver() = default;

    void add(std::string &&name, const void *ptr)
    {
        std::lock_guard<std::mutex> lock(lock_);

        const auto tid = pthread_self();
        auto it(threads_.find(tid));

        if(it == threads_.end())
            threads_[tid] = std::make_tuple(std::move(name), ptr, true);
        else
        {
            const auto old_name = std::get<0>(it->second);

            if(old_name != name)
            {
                msg_info("Thread %08lx \"%s\": renamed to \"%s\"",
                         tid, old_name.c_str(), name.c_str());
                std::get<0>(it->second) = std::move(name);
            }

            if(std::get<1>(it->second) != ptr)
            {
                msg_info("Thread %08lx \"%s\": pointer %p -> %p",
                         tid, old_name.c_str(), std::get<1>(it->second), ptr);
                std::get<1>(it->second) = ptr;
            }

            if(!std::get<2>(it->second))
            {
                msg_info("Thread %08lx \"%s\": was inactive, now activated",
                         tid, old_name.c_str());
                std::get<2>(it->second) = true;
            }
        }
    }

    void leave()
    {
        std::lock_guard<std::mutex> lock(lock_);
        std::get<2>(threads_.at(pthread_self())) = false;
    }

    void destroy()
    {
        std::lock_guard<std::mutex> lock(lock_);
        threads_.erase(pthread_self());
    }

    void dump(const char *context)
    {
        std::lock_guard<std::mutex> lock(lock_);
        msg_info("Dumping known threads [%s]:", context);

        std::vector<pthread_t> destroyed;

        for(const auto &t : threads_)
        {
            int sched_policy;
            struct sched_param sp;

            /*
             * NOTE
             *
             * This call *may* segfault for threads which have been destroyed
             * already. It often just works and fails as expected, but not
             * always. Unfortunately, we don't always get a notification when a
             * thread is destroyed, so we can never be sure if a thread that
             * has been left is still valid.
             *
             * The calls of \c pthread_getschedparam() and
             * \c pthread_setschedparam() in #BoostedThreads::Threads are safe,
             * however, so the crashes will not happen in production code.
             *
             * NOTE
             */
            const int res = pthread_getschedparam(t.first, &sched_policy, &sp);

            if(res != 0)
            {
                msg_info("%08lx: %s - %p - destroyed",
                         t.first, std::get<0>(t.second).c_str(), std::get<1>(t.second));
                destroyed.push_back(t.first);
            }
            else
                msg_info("%08lx: %s - %p - %s - prio %d policy %s",
                         t.first,
                         std::get<0>(t.second).c_str(), std::get<1>(t.second),
                         std::get<2>(t.second) ? "active" : "left",
                         sp.sched_priority,
                         sched_policy == SCHED_OTHER
                         ? "SCHED_OTHER"
                         : (sched_policy == SCHED_RR ? "SCHED_RR" : "***UNKNOWN***"));
        }

        for(const auto &t : destroyed)
            threads_.erase(t);
    }
};

}

#endif /* BOOSTED_THREADS_DEBUG */

#endif /* !BOOSTED_THREADS_HH */
