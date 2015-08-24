/*
 * Copyright (C) 2015  T+A elektroakustik GmbH & Co. KG
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

#ifndef URLFIFO_H
#define URLFIFO_H

#include <unistd.h>
#include <stdbool.h>

#include "streamtime.h"

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

/*!
 * Opaque identifier for items in the URL FIFO.
 */
typedef size_t urlfifo_item_id_t;

/*!
 * URL FIFO item data.
 */
struct urlfifo_item
{
    uint16_t id;
    char url[512];
    struct streamtime start_time;
    struct streamtime end_time;
};

/*!
 * Lock access to the URL FIFO.
 */
void urlfifo_lock(void);

/*!
 * Unlock access to the URL FIFO.
 */
void urlfifo_unlock(void);

/*!
 * Clear URL FIFO, keep number of item on top untouched.
 *
 * \param keep_first_n The number of items to keep untouched. If set to 0, then
 *     the whole FIFO will be cleared.
 *
 * \returns The number of items remaining in the FIFO, guaranteed to be less
 *     than or equal to \p keep_first_n.
 */
size_t urlfifo_clear(size_t keep_first_n);

/*!
 * Append new item to URL FIFO.
 *
 * \param external_id Any ID to be associated with the item. The ID is assigned
 *     by external processes and not assumed to be a true ID; therefore, it is
 *     not used internally for anything except passing it around.
 * \param url The stream URL to play. This parameter may not be \c NULL.
 * \param start, stop The start and stop position of the stretch in a stream to
 *     be played. These may be \c NULL to indicate "natural start of stream"
 *     and "natural end of stream", respectively. A #streamtime with type
 *     #STREAMTIME_TYPE_END_OF_STREAM has the same meaning ("end" referes to
 *     either end, so it means start of stream in case of the \p start
 *     parameter).
 * \param keep_first_n The number of items to keep untouched. If set to 0, then
 *     the whole FIFO will be cleared before adding the new item. If set to
 *     \c SIZE_MAX, then no existing items will be removed.
 * \param item_id Opaque identifier of the newly added item. If this function
 *     fails to insert a new item, then the memory pointed to remains
 *     unchanged. This parameter may be \c NULL in case the caller is not
 *     interested in the identifier.
 *
 * \returns The number of items in the FIFO after inserting the new one, or 0
 *     in case the URL FIFO was full, even after considering \p keep_first_n.
 *     In the latter case, no new item is created and the URL FIFO remains
 *     untouched.
 */
size_t urlfifo_push_item(uint16_t external_id, const char *url,
                         const struct streamtime *start,
                         const struct streamtime *stop,
                         size_t keep_first_n, urlfifo_item_id_t *item_id);

/*!
 * Remove first item in URL FIFO and return a copy in \p dest.
 *
 * \param dest Where to write a copy of the item. This parameter may not be
 *     \c NULL. In case of error, the memory pointed to by \p dest remains
 *     untouched.
 *
 * \returns The number of items remaining in the FIFO after removing the new
 *     one, or -1 in case the URL FIFO was empty.
 */
ssize_t urlfifo_pop_item(struct urlfifo_item *dest);

/*!
 * Retrieve item stored in URL FIFO.
 *
 * This function returns a pointer to the stored data inside the FIFO.
 *
 * \param item_id The identifier of the stored item as returned by
 *     #urlfifo_push_item().
 *
 * \returns A pointer to the stored data.
 *
 * \note The URL FIFO must be locked using #urlfifo_lock() before this function
 *     can be called safely. If the locking is omitted, then the returned
 *     pointer may point to invalid data in the instant this function is
 *     returning.
 */
const struct urlfifo_item *urlfifo_unlocked_peek(urlfifo_item_id_t item_id);

/*!
 * Start searching for item by given URL.
 *
 * This function must be called before starting any search.
 *
 * \returns
 *     An item ID for use with #urlfifo_find_next_item_by_url(). This ID
 *     remains valid as long as the URL FIFO lock is held.
 *
 * \attention
 *     This function must be called with the URL FIFO lock held, i.e.,
 *     #urlfifo_lock() must be called before calling this function.
 */
urlfifo_item_id_t urlfifo_find_item_begin(void);

/*!
 * Return next item containing the given URL.
 *
 * This function may be called successively to iterate over all FIFO items
 * whose stream URL match the URL passed in the \p url parameter. In case there
 * are multiple matching items in the FIFO, they are reported in the order they
 * have been inserted into the FIFO.
 *
 * There is no need for cleaning up anything after the search other than
 * releasing the URL FIFO lock.
 *
 * \param iter
 *     Pointer to an item ID as returned by #urlfifo_find_item_begin().
 *
 * \param url
 *     The URL to be used as search key.
 *
 * \returns
 *     A pointer to an item with matching URL, or \c NULL in case there are no
 *     further items in the FIFO matching the given URL. In the latter case, a
 *     call of #urlfifo_find_item_begin() is required to restart the search,
 *     even if the first call of #urlfifo_find_next_item_by_url() failed
 *     already.
 *
 * \attention
 *     This function must be called with the URL FIFO lock held, i.e.,
 *     #urlfifo_lock() must be called before calling this function.
 *
 * \see #urlfifo_lock(), #urlfifo_unlock(), #urlfifo_find_item_begin().
 */
struct urlfifo_item *urlfifo_find_next_item_by_url(urlfifo_item_id_t *iter,
                                                   const char *url);

/*!
 * Return the number of items in the URL FIFO.
 */
size_t urlfifo_get_size(void);

/*!
 * Whether or not the FIFO is full.
 */
bool urlfifo_is_full(void);

/*!
 * Initialization for unit tests.
 *
 * There is static data inside the URL FIFO implementation to simplify the
 * interface and avoid needless dynamic memory allocation. This function
 * emulates the static initialization usually done by the C startup code.
 *
 * \note Calling this function is not required for production code.
 */
void urlfifo_setup(void);

/*!@}*/

#endif /* !URLFIFO_H */
