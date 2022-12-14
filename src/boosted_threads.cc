/*
 * Copyright (C) 2022, 2023  T+A elektroakustik GmbH & Co. KG
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
    boosted_sched_priorities_{
        sched_get_priority_max(boosted_sched_policy_),
        sched_get_priority_max(boosted_sched_policy_) - 1,
        sched_get_priority_min(boosted_sched_policy_),
    },
    is_boost_enabled_(false)
{
    LoggedLock::configure(lock_, "BoostedThreads::Threads", MESSAGE_LEVEL_DEBUG);

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
    LOGGED_LOCK_CONTEXT_HINT;
    std::lock_guard<LoggedLock::Mutex> lock(lock_);

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
        configure_thread(t.first, t.second.first, t.second.second, context);
}

void BoostedThreads::Threads::throttle(const char *context)
{
    LOGGED_LOCK_CONTEXT_HINT;
    std::lock_guard<LoggedLock::Mutex> lock(lock_);

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
        configure_thread(t.first, t.second.first, Priority::NONE, context);
}

void BoostedThreads::Threads::add_self(std::string &&name, Priority prio)
{
    msg_log_assert(prio != Priority::NONE);

    const pthread_t tid = pthread_self();

    LOGGED_LOCK_CONTEXT_HINT;
    std::lock_guard<LoggedLock::Mutex> lock(lock_);
    configure_thread(tid, name, is_boost_enabled_ ? prio : Priority::NONE,
                     "entered thread");

#if BOOSTED_THREADS_DEBUG
    auto it(threads_.find(tid));

    if(it == threads_.end())
    {
        msg_info("BoostedThreads: Added %s [%08lx]", name.c_str(), tid);
        threads_[tid] = std::make_pair(std::move(name), prio);
    }
    else
    {
        MSG_BUG("BoostedThreads: Added %s [%08lx] (thread ID already registered)",
                name.c_str(), tid);
        it->second = std::make_pair(std::move(name) ,prio);
    }
#else /* !BOOSTED_THREADS_DEBUG */
    threads_[tid] = std::make_pair(std::move(name), prio);
#endif /* BOOSTED_THREADS_DEBUG  */
}

void BoostedThreads::Threads::remove_self()
{
    const pthread_t tid = pthread_self();

    LOGGED_LOCK_CONTEXT_HINT;
    std::lock_guard<LoggedLock::Mutex> lock(lock_);
    auto it(threads_.find(tid));

    if(it == threads_.end())
    {
        MSG_BUG("BoostedThreads: Removed unknown thread %08lx", tid);
        static const std::string unknown("unknown");
        configure_thread(tid, unknown, Priority::NONE, "left thread");
    }
    else
    {
#if BOOSTED_THREADS_DEBUG
        msg_info("BoostedThreads: Removed %s [%08lx]",
                 it->second.first.c_str(), tid);
#endif /* BOOSTED_THREADS_DEBUG  */
        configure_thread(tid, it->second.first, Priority::NONE, "left thread");
        threads_.erase(it);
    }
}

void BoostedThreads::Threads::configure_thread(pthread_t tid, const std::string &name,
                                               Priority prio, const char *context) const
{
    int sched_policy;
    struct sched_param sp;

    switch(prio)
    {
      case Priority::HIGHEST:
      case Priority::HIGH:
      case Priority::MODERATE:
        sched_policy = boosted_sched_policy_;
        sp.sched_priority = boosted_sched_priorities_[size_t(prio)];
        msg_info("Boost thread %lu %s [%s], prio %d", tid, name.c_str(), context, sp.sched_priority);
        break;

      case Priority::NONE:
      default:
        sched_policy = default_sched_policy_;
        sp.sched_priority = default_sched_priority_;
        msg_info("Throttle thread %lu %s [%s]", tid, name.c_str(), context);
        break;
    }

    const int res = pthread_setschedparam(tid, sched_policy, &sp);
    if(res != 0)
        msg_error(res, LOG_NOTICE, "pthread_setschedparam() failed");
}
