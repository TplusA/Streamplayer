#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <glib.h>
#include <assert.h>

#include "urlfifo.h"

static struct
{
    GMutex lock;

    struct urlfifo_item items[4];
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

size_t urlfifo_clear(size_t keep_first_n)
{
    urlfifo_lock();

    if(keep_first_n < fifo_data.num_of_items)
        fifo_data.num_of_items = keep_first_n;

    size_t retval = fifo_data.num_of_items;

    urlfifo_unlock();

    return retval;
}

static void init_item(struct urlfifo_item *item, uint16_t external_id,
                      const char *url, const struct streamtime *start,
                      const struct streamtime *stop)
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
}

static inline size_t add_to_id(size_t id, size_t inc)
{
    return (id + inc) % (sizeof(fifo_data.items) / sizeof(fifo_data.items[0]));
}

size_t urlfifo_push_item(uint16_t external_id, const char *url,
                         const struct streamtime *start,
                         const struct streamtime *stop,
                         size_t keep_first_n, urlfifo_item_id_t *item_id)
{
    assert(url != NULL);

    urlfifo_lock();

    if(keep_first_n < fifo_data.num_of_items)
        fifo_data.num_of_items = keep_first_n;

    if(fifo_data.num_of_items >= sizeof(fifo_data.items) / sizeof(fifo_data.items[0]))
    {
        urlfifo_unlock();
        return 0;
    }

    urlfifo_item_id_t id =
        add_to_id(fifo_data.first_item, fifo_data.num_of_items);

    size_t retval = ++fifo_data.num_of_items;

    if(item_id != NULL)
        *item_id = id;

    init_item(&fifo_data.items[id], external_id, url, start, stop);

    urlfifo_unlock();

    return retval;
}

ssize_t urlfifo_pop_item(struct urlfifo_item *dest)
{
    assert(dest != NULL);

    urlfifo_lock();

    if(fifo_data.num_of_items == 0)
    {
        urlfifo_unlock();
        return -1;
    }

    memcpy(dest, &fifo_data.items[fifo_data.first_item], sizeof(*dest));

    fifo_data.first_item = add_to_id(fifo_data.first_item, 1);
    --fifo_data.num_of_items;

    size_t retval = fifo_data.num_of_items;

    urlfifo_unlock();

    return retval;
}

const struct urlfifo_item *urlfifo_unlocked_peek(urlfifo_item_id_t item_id)
{
    assert(item_id < sizeof(fifo_data.items) / sizeof(fifo_data.items[0]));

    return &fifo_data.items[item_id];
}

size_t urlfifo_get_size(void)
{
    urlfifo_lock();

    size_t retval = fifo_data.num_of_items;

    urlfifo_unlock();

    return retval;
}

void urlfifo_setup(void)
{
    memset(&fifo_data, 0, sizeof(fifo_data));
}
