#include <cutter.h>

#include "urlfifo.h"

/*!
 * \addtogroup urlfifo_tests Unit tests
 * \ingroup urlfifo
 *
 * URL FIFO unit tests.
 */
/*!@{*/

/*!\test
 * After initialization, the URL FIFO shall be empty.
 */
void test_fifo_is_empty_on_startup(void);

/*!\test
 * Clearing the whole FIFO works as expected with an empty FIFO.
 */
void test_clear_all_on_empty_fifo(void);

/*!\test
 * Clearing the whole FIFO results in an empty FIFO.
 */
void test_clear_non_empty_fifo(void);

/*!\test
 * Clearing the last few items in a non-empty FIFO results in a FIFO with as
 * many entries as have been specified in the argument to #urlfifo_clear().
 */
void test_clear_partial_non_empty_fifo(void);

/*!\test
 * Attempting to clear a FIFO with fewer items than specified in the argument
 * to #urlfifo_clear() results in unchanged FIFO content. No items are removed
 * in this case.
 */
void test_clear_partial_with_fewer_items_than_to_be_kept_does_nothing(void);

/*!\test
 * Add a single item to an empty FIFO.
 */
void test_push_single_item(void);

/*!\test
 * Add more than a single item to an empty FIFO.
 */
void test_push_multiple_items(void);

/*!\test
 * Adding more item to the FIFO than it has slots available results in an error
 * returned by #urlfifo_push_item(). The FIFO is expected to remain changed
 * after such an overflow.
 */
void test_push_many_items_does_not_trash_fifo(void);

/*!@}*/


void cut_setup(void)
{
    urlfifo_setup();
}

void test_fifo_is_empty_on_startup(void)
{
    cut_assert_equal_size(0, urlfifo_get_size());
}

void test_clear_all_on_empty_fifo(void)
{
    cut_assert_equal_size(0, urlfifo_clear(0));
}

void test_clear_non_empty_fifo(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(2, urlfifo_get_size());
    cut_assert_equal_size(0, urlfifo_clear(0));
    cut_assert_equal_size(0, urlfifo_get_size());
}

void test_clear_partial_non_empty_fifo(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(3, urlfifo_push_item(5, "third",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(3, urlfifo_get_size());
    cut_assert_equal_size(2, urlfifo_clear(2));
    cut_assert_equal_size(2, urlfifo_get_size());
}

void test_clear_partial_with_fewer_items_than_to_be_kept_does_nothing(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(2, urlfifo_get_size());
    cut_assert_equal_size(2, urlfifo_clear(3));
    cut_assert_equal_size(2, urlfifo_clear(SIZE_MAX));
    cut_assert_equal_size(2, urlfifo_clear(2));
    cut_assert_equal_size(2, urlfifo_get_size());
}

void test_push_single_item(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(42, "http://ta-hifi.de/",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(1, urlfifo_get_size());
}

void test_push_multiple_items(void)
{
    for(unsigned int i = 0; i < 3; ++i)
    {
        cut_assert_equal_size(i + 1,
                              urlfifo_push_item(23, "http://ta-hifi.de/",
                                                NULL, NULL, SIZE_MAX));
    }

    cut_assert_equal_size(3, urlfifo_get_size());
}

void test_push_many_items_does_not_trash_fifo(void)
{
    /* we need this little implementation detail */
    static const size_t fifo_max_size = 4;

    for(unsigned int i = 0; i < fifo_max_size; ++i)
    {
        cut_assert_equal_size(i + 1, urlfifo_push_item(0, "http://ta-hifi.de/",
                                                       NULL, NULL, SIZE_MAX));
    }

    cut_assert_equal_size(fifo_max_size, urlfifo_get_size());
    cut_assert_equal_size(0, urlfifo_push_item(0, "http://ta-hifi.de/",
                                               NULL, NULL, SIZE_MAX));
    cut_assert_equal_size(fifo_max_size, urlfifo_get_size());
}
