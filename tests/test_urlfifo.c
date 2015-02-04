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

#include <cutter.h>
#include <stdio.h>
#include <string.h>

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

/*!\test
 * It is possible to replace the items in a non-empty URL FIFO by a single item
 * by pushing the new item and specifying the number of items to keep as 0.
 */
void test_push_one_replace_all(void);

/*!\test
 * It is possible to replace the last few items in a non-empty URL FIFO by a
 * single item by pushing the new item and specifying the number of items to
 * keep as 1 (or greater).
 */
void test_push_one_keep_first(void);

/*!\test
 * Replacing the contents of an empty URL FIFO with a new item is possible.
 */
void test_push_one_replace_all_works_on_empty_fifo(void);

/*!\test
 * Replacing the contents of an overflown URL FIFO with a new item is possible.
 */
void test_push_one_replace_all_works_on_full_fifo(void);

/*!\test
 * Removing a non-existent first item from the URL FIFO results in an error
 * returned by #urlfifo_pop_item().
 */
void test_pop_empty_fifo_detects_underflow(void);

/*!\test
 * Remove first item from the URL FIFO which contains a single item.
 */
void test_pop_item_from_single_item_fifo(void);

/*!\test
 * Remove first item from the URL FIFO which contains more that one item.
 */
void test_pop_item_from_multi_item_fifo(void);

/*!\test
 * Stress test push and pop to trigger internal wraparound handling code.
 */
void test_push_pop_chase(void);

/*!\test
 * Basic tests for #urlfifo_is_full().
 */
void test_urlfifo_is_full_interface(void);

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
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(2, urlfifo_get_size());
    cut_assert_equal_size(0, urlfifo_clear(0));
    cut_assert_equal_size(0, urlfifo_get_size());
}

void test_clear_partial_non_empty_fifo(void)
{
    urlfifo_item_id_t id_first;
    urlfifo_item_id_t id_second;

    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX, &id_first));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, &id_second));
    cut_assert_equal_size(3, urlfifo_push_item(5, "third",
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(3, urlfifo_get_size());
    cut_assert_equal_size(2, urlfifo_clear(2));
    cut_assert_equal_size(2, urlfifo_get_size());

    urlfifo_lock();

    const struct urlfifo_item *item = urlfifo_unlocked_peek(id_first);
    cut_assert_not_null(item);
    cut_assert_equal_string("first", item->url);

    item = urlfifo_unlocked_peek(id_second);
    cut_assert_not_null(item);
    cut_assert_equal_string("second", item->url);

    urlfifo_unlock();
}

void test_clear_partial_with_fewer_items_than_to_be_kept_does_nothing(void)
{
    urlfifo_item_id_t id_first;
    urlfifo_item_id_t id_second;

    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX, &id_first));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, &id_second));
    cut_assert_equal_size(2, urlfifo_get_size());
    cut_assert_equal_size(2, urlfifo_clear(3));
    cut_assert_equal_size(2, urlfifo_clear(SIZE_MAX));
    cut_assert_equal_size(2, urlfifo_clear(2));
    cut_assert_equal_size(2, urlfifo_get_size());

    urlfifo_lock();

    const struct urlfifo_item *item = urlfifo_unlocked_peek(id_first);
    cut_assert_not_null(item);
    cut_assert_equal_string("first", item->url);

    item = urlfifo_unlocked_peek(id_second);
    cut_assert_not_null(item);
    cut_assert_equal_string("second", item->url);

    urlfifo_unlock();
}

static const char default_url[] = "http://ta-hifi.de/";

void test_push_single_item(void)
{
    urlfifo_item_id_t id;

    cut_assert_equal_size(1, urlfifo_push_item(42, default_url,
                                               NULL, NULL, SIZE_MAX, &id));
    cut_assert_equal_size(1, urlfifo_get_size());

    urlfifo_lock();

    const struct urlfifo_item *item = urlfifo_unlocked_peek(id);
    cut_assert_not_null(item);
    cut_assert_equal_uint(42, item->id);
    cut_assert_equal_string(default_url, item->url);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item->start_time.type);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item->end_time.type);

    urlfifo_unlock();
}

void test_push_multiple_items(void)
{
    static const size_t count = 3;
    urlfifo_item_id_t ids[count];

    for(unsigned int i = 0; i < count; ++i)
    {
        char temp[sizeof(default_url) + 16];

        snprintf(temp, sizeof(temp), "%s %u", default_url, i);
        cut_assert_equal_size(i + 1,
                              urlfifo_push_item(23 + i, temp,
                                                NULL, NULL, SIZE_MAX, &ids[i]));
    }

    cut_assert_equal_size(count, urlfifo_get_size());

    /* check if we can read back what we've written */
    urlfifo_lock();

    for(unsigned int i = 0; i < count; ++i)
    {
        const struct urlfifo_item *item = urlfifo_unlocked_peek(ids[i]);
        char temp[sizeof(default_url) + 16];

        snprintf(temp, sizeof(temp), "%s %u", default_url, i);

        cut_assert_not_null(item);
        cut_assert_equal_uint(23 + i, item->id);
        cut_assert_equal_string(temp, item->url);
        cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item->start_time.type);
        cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item->end_time.type);
    }

    urlfifo_unlock();
}

void test_push_many_items_does_not_trash_fifo(void)
{
    /* we need this little implementation detail */
    static const size_t fifo_max_size = 4;

    urlfifo_item_id_t ids[fifo_max_size];

    for(unsigned int i = 0; i < fifo_max_size; ++i)
    {
        char temp[sizeof(default_url) + 16];

        snprintf(temp, sizeof(temp), "%s %u", default_url, i + 50);
        cut_assert_equal_size(i + 1,
                              urlfifo_push_item(123 + i, temp,
                                                NULL, NULL, SIZE_MAX, &ids[i]));
    }

    cut_assert_equal_size(fifo_max_size, urlfifo_get_size());

    /* next push should fail */
    urlfifo_item_id_t id = 12345;
    cut_assert_equal_size(0, urlfifo_push_item(0, default_url,
                                               NULL, NULL, SIZE_MAX, &id));

    cut_assert_equal_size(fifo_max_size, urlfifo_get_size());
    cut_assert_equal_size(12345, id);

    /* check that FIFO still has the expected content */
    urlfifo_lock();

    for(unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        const struct urlfifo_item *item = urlfifo_unlocked_peek(ids[i]);
        char temp[sizeof(default_url) + 16];

        snprintf(temp, sizeof(temp), "%s %u", default_url, i + 50);

        cut_assert_not_null(item);
        cut_assert_equal_uint(123 + i, item->id);
        cut_assert_equal_string(temp, item->url);
        cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item->start_time.type);
        cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item->end_time.type);
    }

    urlfifo_unlock();
}

void test_push_one_replace_all(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(42, default_url,
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(43, default_url,
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(3, urlfifo_push_item(44, default_url,
                                               NULL, NULL, SIZE_MAX, NULL));

    cut_assert_equal_size(1, urlfifo_push_item(45, default_url,
                                               NULL, NULL, 0, NULL));
    cut_assert_equal_size(1, urlfifo_get_size());

    struct urlfifo_item item;

    cut_assert_equal_size(0, urlfifo_pop_item(&item));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(45, item.id);
}

void test_push_one_keep_first(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(42, default_url,
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(43, default_url,
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(3, urlfifo_push_item(44, default_url,
                                               NULL, NULL, SIZE_MAX, NULL));

    cut_assert_equal_size(2, urlfifo_push_item(45, default_url,
                                               NULL, NULL, 1, NULL));
    cut_assert_equal_size(2, urlfifo_get_size());

    struct urlfifo_item item;

    cut_assert_equal_size(1, urlfifo_pop_item(&item));
    cut_assert_equal_size(1, urlfifo_get_size());
    cut_assert_equal_uint(42, item.id);

    cut_assert_equal_size(0, urlfifo_pop_item(&item));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(45, item.id);
}

void test_push_one_replace_all_works_on_empty_fifo(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(80, default_url,
                                               NULL, NULL, 0, NULL));
    cut_assert_equal_size(1, urlfifo_get_size());

    struct urlfifo_item item;

    cut_assert_equal_size(0, urlfifo_pop_item(&item));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(80, item.id);
}

void test_push_one_replace_all_works_on_full_fifo(void)
{
    static const uint16_t max_insertions = 10;

    for(uint16_t id = 20; id < 20 + max_insertions; ++id)
    {
        if(urlfifo_push_item(id, default_url, NULL, NULL, SIZE_MAX, NULL) == 0)
            break;
    }

    cut_assert_not_equal_size(max_insertions, urlfifo_get_size());

    cut_assert_equal_size(1, urlfifo_push_item(90, default_url,
                                               NULL, NULL, 0, NULL));
    cut_assert_equal_size(1, urlfifo_get_size());

    struct urlfifo_item item;

    cut_assert_equal_size(0, urlfifo_pop_item(&item));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(90, item.id);
}

void test_pop_empty_fifo_detects_underflow(void)
{
    struct urlfifo_item dummy;
    struct urlfifo_item expected;

    memset(&dummy, 0x55, sizeof(dummy));
    memset(&expected, 0x55, sizeof(expected));
    cut_assert_equal_int(-1, urlfifo_pop_item(&dummy));

    /* cut_assert_equal_memory() hangs on failure, so we'll use plain memcmp()
     * instead */
    if(memcmp(&expected, &dummy, sizeof(expected) != 0))
        cut_fail("urlfifo_pop_item() trashed memory");
}

void test_pop_item_from_single_item_fifo(void)
{
    urlfifo_item_id_t id;

    cut_assert_equal_size(1, urlfifo_push_item(42, default_url,
                                               NULL, NULL, SIZE_MAX, &id));
    cut_assert_equal_size(1, urlfifo_get_size());

    struct urlfifo_item item;

    cut_assert_equal_size(0, urlfifo_pop_item(&item));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(42, item.id);
    cut_assert_equal_string(default_url, item.url);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.start_time.type);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.end_time.type);
}

void test_pop_item_from_multi_item_fifo(void)
{
    urlfifo_item_id_t id_first;
    urlfifo_item_id_t id_second;

    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX, &id_first));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, &id_second));
    cut_assert_equal_size(3, urlfifo_push_item(5, "third",
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(3, urlfifo_get_size());

    struct urlfifo_item item;

    cut_assert_equal_size(2, urlfifo_pop_item(&item));
    cut_assert_equal_size(2, urlfifo_get_size());
    cut_assert_equal_uint(23, item.id);
    cut_assert_equal_string("first", item.url);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.start_time.type);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.end_time.type);

    cut_assert_equal_size(1, urlfifo_pop_item(&item));
    cut_assert_equal_size(1, urlfifo_get_size());
    cut_assert_equal_uint(32, item.id);
    cut_assert_equal_string("second", item.url);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.start_time.type);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.end_time.type);
}

void test_push_pop_chase(void)
{
    static const uint16_t id_base = 100;
    static const unsigned int num_of_iterations = 10;

    cut_assert_equal_size(1, urlfifo_push_item(id_base, default_url,
                                               NULL, NULL, SIZE_MAX, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(id_base + 1, default_url,
                                               NULL, NULL, SIZE_MAX, NULL));

    struct urlfifo_item item;

    for(unsigned int i = 0; i < num_of_iterations; ++i)
    {
        cut_assert_equal_size(3, urlfifo_push_item(i + id_base + 2, default_url,
                                                   NULL, NULL, SIZE_MAX,
                                                   NULL));
        cut_assert_equal_size(3, urlfifo_get_size());

        cut_assert_equal_size(2, urlfifo_pop_item(&item));
        cut_assert_equal_size(2, urlfifo_get_size());
        cut_assert_equal_uint(i + id_base, item.id);
    }

    cut_assert_equal_size(1, urlfifo_pop_item(&item));
    cut_assert_equal_size(1, urlfifo_get_size());
    cut_assert_equal_uint(num_of_iterations + id_base + 0, item.id);

    cut_assert_equal_size(0, urlfifo_pop_item(&item));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(num_of_iterations + id_base + 1, item.id);
}

void test_urlfifo_is_full_interface(void)
{
    cut_assert_false(urlfifo_is_full());

    for(uint16_t i = 0; i < 10; ++i)
    {
        if(urlfifo_is_full())
        {
            cut_assert_equal_size(0, urlfifo_push_item(0, default_url,
                                                       NULL, NULL, SIZE_MAX,
                                                       NULL));
            break;
        }

        cut_assert_equal_size(i + 1, urlfifo_push_item(0, default_url,
                                                       NULL, NULL, SIZE_MAX,
                                                       NULL));
    }

    cut_assert_true(urlfifo_is_full());

    struct urlfifo_item dummy;
    (void)urlfifo_pop_item(&dummy);

    cut_assert_false(urlfifo_is_full());
}
