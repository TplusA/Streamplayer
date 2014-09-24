#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>

#include "urlfifo.h"

static struct
{
    struct urlfifo_item items[4];
    size_t num_of_items;
    size_t first_item;
}
fifo_data;

size_t urlfifo_clear(size_t keep_first_n)
{
    if(keep_first_n < fifo_data.num_of_items)
        fifo_data.num_of_items = keep_first_n;

    return fifo_data.num_of_items;
}

size_t urlfifo_push_item(uint16_t id, const char *url,
                         const struct streamtime *start,
                         const struct streamtime *stop,
                         size_t keep_first_n)
{
    if(fifo_data.num_of_items >= sizeof(fifo_data.items) / sizeof(fifo_data.items[0]))
        return 0;

    fifo_data.first_item =
        ((fifo_data.first_item + 1) %
         (sizeof(fifo_data.items) / sizeof(fifo_data.items[0])));

    return ++fifo_data.num_of_items;
}

size_t urlfifo_get_size(void)
{
    return fifo_data.num_of_items;
}

void urlfifo_setup(void)
{
    memset(&fifo_data, 0, sizeof(fifo_data));
}
