#include <cutter.h>

#include "urlfifo.h"

void test_urlfifo_static_initialization(void);
void test_urlfifo_clear_all(void);

void test_urlfifo_static_initialization(void)
{
    cut_assert_equal_size(0, urlfifo_get_size());
}

void test_urlfifo_clear_all(void)
{
    cut_assert_equal_size(0, urlfifo_clear(0));
}
