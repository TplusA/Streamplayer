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

#ifndef URLFIFO_HH
#define URLFIFO_HH

#include "logged_lock.hh"

#include <deque>
#include <unordered_set>
#include <memory>
#include <functional>

/*!
 * \addtogroup urlfifo URL FIFO
 *
 * Functions around URL FIFO singleton.
 *
 * The URL FIFO is a short queue of URLs to play with start and end position
 * information. We need this FIFO as a hint what to play next after the
 * currently playing stream to enable gapless playback.
 *
 * Entries in the URL FIFO are streams which are going to be played next, not
 * ones which are currently being played.
 *
 * The URL FIFO is not to be confused with a playlist, which is a list usually
 * manipulated by some user. It is nothing more than a purely technical,
 * low-level mechanism for letting us know which stream is most likely going to
 * be played next. There is no information about streams which have been played
 * in the past. The FIFO is therefore very short and needs to be frequently fed
 * by a controlling program that knows how to handle real playlists and/or cue
 * sheets. Thus, the URL FIFO may frequently contain a small fraction of some
 * playlist, but other than that, there is no relation between the two
 * concepts.
 */
/*!@{*/

namespace PlayQueue
{

/*!
 * Class template for a FIFO of items to play.
 *
 * \note
 *     The queue's lock must be acquired before calling any of its methods.
 */
template <typename T>
class Queue
{
  private:
    const size_t max_number_of_items_;

    std::deque<std::unique_ptr<T>> queue_;
    std::deque<std::unique_ptr<T>> removed_;
    std::unordered_set<typename T::stream_id_t> dropped_;
    mutable LoggedLock::RecMutex lock_;

  public:
    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;

    explicit Queue(size_t max_number_of_items = 8):
        max_number_of_items_(max_number_of_items)
    {
        LoggedLock::configure(lock_, "Queue", MESSAGE_LEVEL_DEBUG);
    }

    /*!
     * Lock access to the URL FIFO.
     */
    LoggedLock::UniqueLock<LoggedLock::RecMutex> lock()
    {
        return LoggedLock::UniqueLock<LoggedLock::RecMutex>(lock_);
    }

    template <typename F>
    auto locked_rw(F &&code) -> decltype(code(*this))
    {
        std::lock_guard<LoggedLock::RecMutex> lk(lock_);
        return code(*this);
    }

    template <typename F>
    auto locked_ro(F &&code) -> decltype(code(*this)) const
    {
        std::lock_guard<LoggedLock::RecMutex> lk(lock_);
        return code(*this);
    }

    typename std::deque<std::unique_ptr<T>>::const_iterator begin() const { return queue_.begin(); }
    typename std::deque<std::unique_ptr<T>>::const_iterator end() const   { return queue_.end(); }

    /*!
     * Return the number of items in the URL FIFO.
     */
    size_t size() const { return queue_.size(); }

    /*!
     * Whether or not the URL FIFO is empty.
     */
    bool empty() const { return queue_.empty(); }

    /*!
     * Whether or not the FIFO is full.
     */
    bool full() const { return size() >= max_number_of_items_; }

    /*!
     * Append new item to URL FIFO.
     *
     * Items which get removed from the URL FIFO are moved to an internal store
     * for deferred retrieval or disposal by #PlayQueue::Queue::get_removed().
     *
     * \param item
     *     The Item to add to the URL FIFO.
     *
     * \param keep_first_n
     *     The number of items to keep untouched at top of the FIFO. If set to
     *     0, then the whole FIFO will be cleared before adding the new item.
     *     If set to \c SIZE_MAX, then no existing items will be removed.
     *
     * \returns
     *     The number of items in the FIFO after inserting the new one, or 0
     *     in case the URL FIFO was full, even after considering \p keep_first_n.
     *     In the latter case, no new item is created and the URL FIFO remains
     *     untouched.
     */
    size_t push(std::unique_ptr<T> item, size_t keep_first_n)
    {
        if(clear(keep_first_n) == 0 && full())
            return 0;

        queue_.emplace_back(std::move(item));

        return size();
    }

    /*!
     * Remove first item from URL FIFO and return it.
     *
     * \param[out] dest
     *     The removed head element of the FIFO is returned here. In case the
     *     FIFO was empty, the object will remain untouched.
     *
     * \param what
     *     String providing context in case of a bug.
     *
     * \returns
     *     \c True if an item was moved to \p dest, \c false in case the URL
     *     FIFO was empty.
     */
    bool pop(std::unique_ptr<T> &dest, const char *what = nullptr)
    {
        if(empty())
            return false;

        dest = std::move(queue_.front());
        queue_.pop_front();

        return true;
    }

    /*!
     * Remove first item from URL FIFO, and discard it.
     */
    bool pop_drop()
    {
        if(empty())
            return false;

        removed_.push_back(std::move(queue_.front()));
        queue_.pop_front();

        return true;
    }

    void mark_as_dropped(typename T::stream_id_t id) { dropped_.insert(id); }

    /*!
     * Clear URL FIFO, keep number of items at top untouched.
     *
     * This is a bit like a pop operation, but for an implicitly specified
     * amount of items and for item at the back of the FIFO.
     *
     * \param keep_first_n
     *     The number of items to keep untouched. If set to 0, then
     *     the whole FIFO will be cleared.
     *
     * \returns
     *     The number of items removed from the FIFO.
     */
    size_t clear(size_t keep_first_n)
    {
        const size_t result = keep_first_n < size() ? size() - keep_first_n : 0;

        if(result > 0)
        {
            removed_.resize(removed_.size() + result);
            auto it(removed_.rbegin());

            for(size_t i = 0; i < result; ++i)
            {
                *it++ = std::move(queue_.back());
                queue_.pop_back();
            }
        }

        return result;
    }

    size_t clear_while(const std::function<bool(const T &item)> &pred)
    {
        size_t count = 0;

        while(!empty())
        {
            if(!pred(*queue_.front()))
                break;

            removed_.emplace_back(std::move(queue_.front()));
            queue_.pop_front();
            ++count;
        }

        return count;
    }

    /*!
     * Return raw pointer to first item in URL FIFO.
     *
     * The item will still be owned by the URL FIFO.
     */
    T *peek()
    {
        return empty() ? nullptr : queue_.front().get();
    }

    const T *peek() const
    {
        return const_cast<Queue<T> *>(this)->peek();
    }

    /*!
     * Return raw pointer to any item in URL FIFO.
     *
     * The item will still be owned by the URL FIFO.
     */
    T *peek(size_t pos)
    {
        try
        {
            return queue_.at(pos).get();
        }
        catch(const std::out_of_range &e)
        {
            return nullptr;
        }
    }

    const T *peek(size_t pos) const
    {
        return const_cast<Queue<T> *>(this)->peek(pos);
    }

    auto get_removed() { return std::move(removed_); }

    auto get_dropped() { return std::move(dropped_); }
};

}

/*!@}*/

#endif /* !URLFIFO_HH */
