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

#include "boosted_threads.hh"
#include "messages.h"

BoostedThreads::Threads::Threads():
    boosted_sched_policy_(SCHED_RR),
    boosted_sched_priority_(sched_get_priority_max(boosted_sched_policy_)),
    is_boost_enabled_(false)
{
    struct sched_param sp;
    const int res = pthread_getschedparam(pthread_self(), &default_sched_policy_, &sp);
    default_sched_priority_ = sp.sched_priority;

    if(res != 0)
    {
        msg_error(res, LOG_NOTICE, "pthread_getschedparam() failed");
        default_sched_policy_ = SCHED_OTHER;
        default_sched_priority_ = 0;
    }
}

void BoostedThreads::Threads::boost(const char *context)
{
    std::lock_guard<std::mutex> lock(lock_);

#if BOOSTED_THREADS_DEBUG
    msg_info("BoostedThreads: Boosting %zu threads", threads_.size());
#endif /* BOOSTED_THREADS_DEBUG  */

    if(is_boost_enabled_)
    {
#if BOOSTED_THREADS_DEBUG
        msg_info("BoostedThreads: Already boosted");
#endif /* BOOSTED_THREADS_DEBUG  */
        return;
    }

    is_boost_enabled_ = true;

    for(const auto &t : threads_)
        configure_thread(t.first, t.second, true, context);
}

void BoostedThreads::Threads::throttle(const char *context)
{
    std::lock_guard<std::mutex> lock(lock_);

#if BOOSTED_THREADS_DEBUG
    msg_info("BoostedThreads: Throttling %zu threads", threads_.size());
#endif /* BOOSTED_THREADS_DEBUG  */

    if(!is_boost_enabled_)
    {
#if BOOSTED_THREADS_DEBUG
        msg_info("BoostedThreads: Already throttled");
#endif /* BOOSTED_THREADS_DEBUG  */
        return;
    }

    is_boost_enabled_ = false;

    for(const auto &t : threads_)
        configure_thread(t.first, t.second, false, context);
}

void BoostedThreads::Threads::add_self(std::string &&name)
{
    const pthread_t tid = pthread_self();

    std::lock_guard<std::mutex> lock(lock_);
    configure_thread(tid, name, is_boost_enabled_, "entered thread");

#if BOOSTED_THREADS_DEBUG
    auto it(threads_.find(tid));

    if(it == threads_.end())
    {
        msg_info("BoostedThreads: Added %s [%08lx]", name.c_str(), tid);
        threads_[tid] = std::move(name);
    }
    else
    {
        BUG("BoostedThreads: Added %s [%08lx] (thread ID already registered)",
            name.c_str(), tid);
        it->second = std::move(name);
    }
#else /* !BOOSTED_THREADS_DEBUG */
    threads_[tid] = std::move(name);
#endif /* BOOSTED_THREADS_DEBUG  */
}

void BoostedThreads::Threads::remove_self()
{
    const pthread_t tid = pthread_self();

    std::lock_guard<std::mutex> lock(lock_);
    auto it(threads_.find(tid));

    if(it == threads_.end())
    {
        BUG("BoostedThreads: Removed unknown thread %08lx", tid);
        static const std::string unknown("unknown");
        configure_thread(tid, unknown, false, "left thread");
    }
    else
    {
#if BOOSTED_THREADS_DEBUG
        msg_info("BoostedThreads: Removed %s [%08lx]", it->second.c_str(), tid);
#endif /* BOOSTED_THREADS_DEBUG  */
        configure_thread(tid, it->second, false, "left thread");
        threads_.erase(it);
    }
}

void BoostedThreads::Threads::configure_thread(pthread_t tid, const std::string &name,
                                               bool is_boosted, const char *context) const
{
    int sched_policy;
    struct sched_param sp;

    if(is_boosted)
    {
        sched_policy = boosted_sched_policy_;
        sp.sched_priority = boosted_sched_priority_;
    }
    else
    {
        sched_policy = default_sched_policy_;
        sp.sched_priority = default_sched_priority_;
    }

    if(is_boosted)
        msg_info("Boost thread %s [%s]", name.c_str(), context);
    else
        msg_info("Throttle thread %s [%s]", name.c_str(), context);

    const int res = pthread_setschedparam(tid, sched_policy, &sp);
    if(res != 0)
        msg_error(res, LOG_NOTICE, "pthread_setschedparam() failed");
}
