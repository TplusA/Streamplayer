/*
 * Copyright (C) 2015, 2016  T+A elektroakustik GmbH & Co. KG
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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <glib.h>

#include "urlfifo.h"
#include "messages.h"

static struct
{
    GMutex lock;

    struct urlfifo_item items[URLFIFO_MAX_LENGTH];
    size_t num_of_items;
    size_t first_item;
}
fifo_data;

void urlfifo_lock(void)
{
    g_mutex_lock(&fifo_data.lock);
}

void urlfifo_unlock(void)
{
    g_mutex_unlock(&fifo_data.lock);
}

static inline size_t add_to_id(size_t id, size_t inc)
{
    return (id + inc) % (sizeof(fifo_data.items) / sizeof(fifo_data.items[0]));
}

size_t urlfifo_clear(size_t keep_first_n, uint16_t *ids_removed)
{
    urlfifo_lock();

    size_t removed_count;

    if(ids_removed == NULL)
        removed_count = ((keep_first_n < fifo_data.num_of_items)
                         ? fifo_data.num_of_items - keep_first_n
                         : 0);
    else
    {
        removed_count = 0;

        for(size_t i = keep_first_n; i < fifo_data.num_of_items; ++i)
            ids_removed[removed_count++] = fifo_data.items[add_to_id(fifo_data.first_item, i)].id;
    }

    for(size_t i = keep_first_n; i < fifo_data.num_of_items; ++i)
        urlfifo_free_item(&fifo_data.items[add_to_id(fifo_data.first_item, i)]);

    fifo_data.num_of_items -= removed_count;

    urlfifo_unlock();

    return removed_count;
}

static void init_item(struct urlfifo_item *item, uint16_t external_id,
                      const char *url, const struct streamtime *start,
                      const struct streamtime *stop,
                      void *data, const struct urlfifo_item_data_ops *ops)
{
    item->id = external_id;

    strncpy(item->url, url, sizeof(item->url));
    item->url[sizeof(item->url) - 1] = '\0';

    static const struct streamtime end_of_stream =
    {
        .type = STREAMTIME_TYPE_END_OF_STREAM,
    };

    item->start_time = (start != NULL) ? *start : end_of_stream;
    item->end_time = (stop != NULL) ? *stop : end_of_stream;

    item->data = data;
    item->data_ops = ops;

    if(item->data_ops != NULL)
        item->data_ops->data_init(&item->data);
}

static inline bool urlfifo_unlocked_is_full(void)
{
    return fifo_data.num_of_items >= (sizeof(fifo_data.items) / sizeof(fifo_data.items[0]));
}

size_t urlfifo_push_item(uint16_t external_id, const char *url,
                         const struct streamtime *start,
                         const struct streamtime *stop,
                         size_t keep_first_n, urlfifo_item_id_t *item_id,
                         void *data, const struct urlfifo_item_data_ops *ops)
{
    log_assert(url != NULL);

    urlfifo_lock();

    if(keep_first_n < fifo_data.num_of_items)
        fifo_data.num_of_items = keep_first_n;

    if(urlfifo_unlocked_is_full())
    {
        urlfifo_unlock();
        return 0;
    }

    urlfifo_item_id_t id =
        add_to_id(fifo_data.first_item, fifo_data.num_of_items);

    size_t retval = ++fifo_data.num_of_items;

    if(item_id != NULL)
        *item_id = id;

    init_item(&fifo_data.items[id], external_id, url, start, stop, data, ops);

    urlfifo_unlock();

    return retval;
}

ssize_t urlfifo_pop_item(struct urlfifo_item *dest, bool free_dest)
{
    log_assert(dest != NULL);

    urlfifo_lock();

    if(fifo_data.num_of_items == 0)
    {
        urlfifo_unlock();
        return -1;
    }

    if(free_dest)
        urlfifo_free_item(dest);

    memcpy(dest, &fifo_data.items[fifo_data.first_item], sizeof(*dest));

    fifo_data.first_item = add_to_id(fifo_data.first_item, 1);
    --fifo_data.num_of_items;

    size_t retval = fifo_data.num_of_items;

    urlfifo_unlock();

    return retval;
}

void urlfifo_free_item(struct urlfifo_item *item)
{
    log_assert(item != NULL);

    if(item->data_ops != NULL)
        item->data_ops->data_free(&item->data);

    item->url[0] = '\0';
    item->data_ops = NULL;
}

const struct urlfifo_item *urlfifo_unlocked_peek(urlfifo_item_id_t item_id)
{
    log_assert(item_id < sizeof(fifo_data.items) / sizeof(fifo_data.items[0]));

    return &fifo_data.items[item_id];
}

urlfifo_item_id_t urlfifo_find_item_begin(void)
{
    return 0;
}

struct urlfifo_item *urlfifo_find_next_item_by_url(urlfifo_item_id_t *iter,
                                                   const char *url)
{
    log_assert(iter != NULL);
    log_assert(url != NULL);

    while(*iter < fifo_data.num_of_items)
    {
        struct urlfifo_item *const candidate = &fifo_data.items[*iter];

        ++*iter;

        if(strcmp(candidate->url, url) == 0)
            return candidate;
    }

    return NULL;
}

size_t urlfifo_get_size(void)
{
    return urlfifo_get_queued_ids(NULL);
}

size_t urlfifo_get_queued_ids(uint16_t *ids_in_fifo)
{
    urlfifo_lock();

    size_t retval;

    if(ids_in_fifo == NULL)
        retval = fifo_data.num_of_items;
    else
    {
        for(retval = 0; retval < fifo_data.num_of_items; ++retval)
            ids_in_fifo[retval] = fifo_data.items[retval].id;
    }

    urlfifo_unlock();

    return retval;
}

bool urlfifo_is_full(void)
{
    urlfifo_lock();
    bool retval = urlfifo_unlocked_is_full();
    urlfifo_unlock();

    return retval;
}

void urlfifo_setup(void)
{
    memset(&fifo_data, 0, sizeof(fifo_data));
    g_mutex_init(&fifo_data.lock);
}

void urlfifo_shutdown(void)
{
    g_mutex_clear(&fifo_data.lock);
}
