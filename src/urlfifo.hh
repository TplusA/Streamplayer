/*
 * Copyright (C) 2015--2019  T+A elektroakustik GmbH & Co. KG
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

#ifndef URLFIFO_HH
#define URLFIFO_HH

#include <deque>
#include <vector>
#include <mutex>
#include <memory>

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
    mutable std::recursive_mutex lock_;

  public:
    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;

    explicit Queue(size_t max_number_of_items = 8):
        max_number_of_items_(max_number_of_items)
    {}

    /*!
     * Lock access to the URL FIFO.
     */
    std::unique_lock<std::recursive_mutex> lock()
    {
        return std::unique_lock<std::recursive_mutex>(lock_);
    }

    template <typename F>
    auto locked_rw(F &&code) -> decltype(code(*this))
    {
        std::lock_guard<std::recursive_mutex> lk(lock_);
        return code(*this);
    }

    template <typename F>
    auto locked_ro(F &&code) -> decltype(code(*this)) const
    {
        std::lock_guard<std::recursive_mutex> lk(lock_);
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
     * \param item
     *     The Item to add to the URL FIFO.
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
        while(keep_first_n < size())
            queue_.pop_back();

        if(full())
            return 0;

        queue_.emplace_back(std::move(item));

        return size();
    }

    /*!
     * Remove first item from URL FIFO and return it.
     *
     * \param[out] remaining
     *     The number of items remaining in the FIFO after removing the head
     *     element, if any.
     *
     * \returns
     *     The first item in the FIFO, or \c nullptr in case the URL FIFO was
     *     empty.
     */
    std::unique_ptr<T> pop(size_t &remaining)
    {
        if(empty())
        {
            remaining = 0;
            return nullptr;
        }

        auto result = std::move(queue_.front());

        queue_.pop_front();
        remaining = size();

        return result;
    }

    /*!
     * Remove first item from URL FIFO and return it.
     *
     * Simplified version for callers not interested in number of remaining
     * items.
     */
    std::unique_ptr<T> pop()
    {
        size_t dummy;
        return pop(dummy);
    }

    /*!
     * Remove first item from URL FIFO, assign to destination if not empty.
     */
    bool pop(std::unique_ptr<T> &dest)
    {
        if(empty())
            return false;

        dest = std::move(queue_.front());
        queue_.pop_front();

        return true;
    }

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
     *     The items removed from the FIFO.
     */
    std::vector<std::unique_ptr<T>> clear(size_t keep_first_n)
    {
        std::vector<std::unique_ptr<T>> result;

        while(keep_first_n < size())
        {
            result.emplace_back(std::move(queue_.back()));
            queue_.pop_back();
        }

        return result;
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

};

}

/*!@}*/

#endif /* !URLFIFO_HH */
