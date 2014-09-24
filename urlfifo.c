#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <glib.h>

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

size_t urlfifo_push_item(uint16_t id, const char *url,
                         const struct streamtime *start,
                         const struct streamtime *stop,
                         size_t keep_first_n)
{
    urlfifo_lock();

    if(fifo_data.num_of_items >= sizeof(fifo_data.items) / sizeof(fifo_data.items[0]))
    {
        urlfifo_unlock();
        return 0;
    }

    fifo_data.first_item =
        ((fifo_data.first_item + 1) %
         (sizeof(fifo_data.items) / sizeof(fifo_data.items[0])));

    size_t retval = ++fifo_data.num_of_items;

    urlfifo_unlock();

    return retval;
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
