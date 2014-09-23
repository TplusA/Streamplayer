#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include "urlfifo.h"

size_t urlfifo_clear(size_t keep_first_n)
{
    return 0;
}

size_t urlfifo_push_item(uint16_t id, const char *url,
                         const struct streamtime *start,
                         const struct streamtime *stop,
                         size_t keep_first_n)
{
    return 0;
}

size_t urlfifo_get_size(void)
{
    return 0;
}
