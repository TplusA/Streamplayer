/*
 * Copyright (C) 2015, 2016, 2017  T+A elektroakustik GmbH & Co. KG
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
#include "messages.h"

/* there is no message mock in these tests */
void msg_error(int error_code, int priority, const char *error_format, ...) {}

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
 * Like #test_clear_partial_non_empty_fifo(), but pop items beforehand.
 *
 * Because we are operating on a ring buffer.
 */
void test_partial_clear_after_pop_item_from_multi_item_fifo(void);

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
 * If defined, the URL FIFO item data fail operation is used when failing an
 * item.
 */
void test_item_data_callback_is_called_for_fail(void);

/*!\test
 * Failing an item multiple times is a bug, fail function is called only once.
 */
void test_item_should_fail_only_once(void);

/*!\test
 * If defined, the URL FIFO item free operation is used when popping an item
 * "over" an initialized item for the item that is being overwritten.
 */
void test_item_data_callbacks_are_called_for_push_pop(void);

/*!\test
 * Popping an item to a \c NULL pointer removes the item from the URL FIFO and
 * frees the item.
 */
void test_item_data_callbacks_are_called_for_pop_to_drop(void);

/*!\test
 * If defined, the URL FIFO item data operations are used when pushing data,
 * then clearing the FIFO.
 */
void test_item_data_callbacks_are_called_for_push_clear(void);

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
 * Empty URL FIFO is handled correctly.
 */
void test_peek_empty_fifo_returns_null(void);

/*!\test
 * Head element can be inspected, also several times.
 */
void test_peek_fifo_returns_head_element(void);

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
 * Number of queued IDs is 0 for empty URL FIFO.
 */
void test_get_queued_ids_count_for_empty_fifo(void);

/*!\test
 * No queued IDs are returned for empty URL FIFO.
 */
void test_get_queued_ids_for_empty_fifo(void);

/*!\test
 * Queued IDs are returned.
 */
void test_get_queued_ids_for_filled_fifo(void);

/*!\test
 * Basic tests for #urlfifo_is_full().
 */
void test_urlfifo_is_full_interface(void);

/*!\test
 * Trying to find anything in an empty FIFO never returns an item.
 */
void test_urlfifo_find_item_by_url_in_empty_fifo_returns_null(void);

/*!\test
 * Find the only matching item in a FIFO with a single entry.
 */
void test_urlfifo_find_item_by_url_in_single_entry_fifo_find_match(void);

/*!\test
 * Find the only matching item in a filled FIFO.
 */
void test_urlfifo_find_item_by_url_in_filled_fifo_finds_match(void);

/*!\test
 * Find the only matching item in a filled FIFO which is also the last item in
 * the FIFO.
 */
void test_urlfifo_find_item_by_url_in_filled_fifo_finds_last_match(void);

/*!\test
 * Find multiple matching items in a filled FIFO, youngest first.
 */
void test_urlfifo_find_item_by_url_in_filled_fifo_finds_multiple_matches(void);

/*!\test
 * Searching for an item can be restarted at any time.
 */
void test_urlfifo_find_item_by_url_can_be_restarted(void);

/*!\test
 * Search for the given URL in a filled FIFO returns no item.
 */
void test_urlfifo_find_item_by_url_in_filled_fifo_may_find_nothing(void);

/*!@}*/


void cut_setup(void)
{
    urlfifo_setup();
}

void cut_teardown(void)
{
    urlfifo_shutdown();
}

void test_fifo_is_empty_on_startup(void)
{
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_true(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
}

void test_clear_all_on_empty_fifo(void)
{
    stream_id_t ids[URLFIFO_MAX_LENGTH];
    memset(ids, 0x55, sizeof(ids));

    cut_assert_equal_size(0, urlfifo_clear(0, ids));

    for(size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        cut_assert_equal_uint(0x5555, ids[i]);
}

void test_clear_non_empty_fifo(void)
{
    cut_assert_true(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());

    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_equal_size(2, urlfifo_get_size());
    cut_assert_equal_size(2, urlfifo_get_queued_ids(NULL));

    stream_id_t ids[3 * URLFIFO_MAX_LENGTH];
    memset(ids, 0x55, sizeof(ids));

    cut_assert_equal_size(2, urlfifo_clear(0, &ids[URLFIFO_MAX_LENGTH]));

    cut_assert_true(urlfifo_is_empty());

    for(size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        if((i == URLFIFO_MAX_LENGTH + 0) || (i == URLFIFO_MAX_LENGTH + 1))
            continue;

        cut_assert_equal_uint(0x5555, ids[i]);
    }
    cut_assert_equal_uint(23, ids[URLFIFO_MAX_LENGTH + 0]);
    cut_assert_equal_uint(32, ids[URLFIFO_MAX_LENGTH + 1]);

    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_size(0, urlfifo_get_queued_ids(NULL));
}

void test_clear_partial_non_empty_fifo(void)
{
    urlfifo_item_id_t id_first;
    urlfifo_item_id_t id_second;

    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX, &id_first,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, &id_second,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_get_size());
    cut_assert_equal_size(2, urlfifo_get_queued_ids(NULL));

    stream_id_t ids[URLFIFO_MAX_LENGTH];
    memset(ids, 0x55, sizeof(ids));

    cut_assert_equal_size(1, urlfifo_clear(1, ids));

    cut_assert_equal_uint(32,     ids[0]);
    cut_assert_equal_uint(0x5555, ids[1]);

    cut_assert_equal_size(1, urlfifo_get_size());
    cut_assert_equal_size(1, urlfifo_get_queued_ids(NULL));

    urlfifo_lock();

    const struct urlfifo_item *item = urlfifo_unlocked_peek(id_first);
    cut_assert_not_null(item);
    cut_assert_equal_string("first", item->url);

    urlfifo_unlock();
}

void test_partial_clear_after_pop_item_from_multi_item_fifo(void)
{
    /* we want to trigger wrap-around, and the test is hard-coded against the
     * maximum URL FIFO size */
    cut_assert_equal_size(8, URLFIFO_MAX_LENGTH);

    cut_assert_true(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());

    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_size(3, urlfifo_push_item(123, "third",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_size(4, urlfifo_push_item(132, "fourth",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_size(5, urlfifo_push_item(223, "fifth",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_size(6, urlfifo_push_item(232, "sixth",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_size(7, urlfifo_push_item(323, "seventh",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_size(8, urlfifo_push_item(332, "eighth",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_true(urlfifo_is_full());
    cut_assert_equal_size(8, urlfifo_get_size());

    struct urlfifo_item item = { 0 };

    cut_assert_equal_size(7, urlfifo_pop_item(&item, false));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_uint(23, item.id);
    cut_assert_equal_string("first", item.url);

    /* this one ends up in the first slot */
    cut_assert_equal_size(8, urlfifo_push_item(42, "ninth",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_false(urlfifo_is_empty());
    cut_assert_true(urlfifo_is_full());

    /* we now have |42|32|123|132|223|232|323|332|, with 32 being the head
     * element */
    stream_id_t ids[URLFIFO_MAX_LENGTH];
    memset(ids, 0x55, sizeof(ids));

    cut_assert_equal_size(7, urlfifo_clear(1, ids));

    cut_assert_false(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_size(1, urlfifo_get_size());
    cut_assert_equal_uint(123,    ids[0]);
    cut_assert_equal_uint(132,    ids[1]);
    cut_assert_equal_uint(223,    ids[2]);
    cut_assert_equal_uint(232,    ids[3]);
    cut_assert_equal_uint(323,    ids[4]);
    cut_assert_equal_uint(332,    ids[5]);
    cut_assert_equal_uint(42,     ids[6]);
    cut_assert_equal_uint(0x5555, ids[7]);

    cut_assert_equal_size(0, urlfifo_pop_item(&item, true));
    cut_assert_true(urlfifo_is_empty());
    cut_assert_false(urlfifo_is_full());
    cut_assert_equal_uint(32, item.id);
    cut_assert_equal_string("second", item.url);

    urlfifo_free_item(&item);
}

void test_clear_partial_with_fewer_items_than_to_be_kept_does_nothing(void)
{
    urlfifo_item_id_t id_first;
    urlfifo_item_id_t id_second;

    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX, &id_first,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, &id_second,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_get_size());

    stream_id_t ids[URLFIFO_MAX_LENGTH];
    memset(ids, 0x55, sizeof(ids));

    cut_assert_equal_size(0, urlfifo_clear(3, ids));
    cut_assert_equal_uint(0x5555, ids[0]);
    cut_assert_equal_size(0, urlfifo_clear(SIZE_MAX, ids));
    cut_assert_equal_uint(0x5555, ids[0]);
    cut_assert_equal_size(0, urlfifo_clear(2, ids));
    cut_assert_equal_uint(0x5555, ids[0]);
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
                                               NULL, NULL, SIZE_MAX, &id,
                                               NULL, NULL));
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
    static const size_t count = 2;
    urlfifo_item_id_t ids[count];

    for(unsigned int i = 0; i < count; ++i)
    {
        char temp[sizeof(default_url) + 16];

        snprintf(temp, sizeof(temp), "%s %u", default_url, i);
        cut_assert_equal_size(i + 1,
                              urlfifo_push_item(23 + i, temp,
                                                NULL, NULL, SIZE_MAX, &ids[i],
                                                NULL, NULL));
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

static void item_data_fail(void *data, void *user_data)
{
    cut_assert_not_null(data);
    cut_fail("unexpected call");
}

static void item_data_fail_set_uint32_data(void *data, void *user_data)
{
    cut_assert_not_null(data);
    *(uint32_t *)data = 0x12345678;
}

static void item_data_free(void **data)
{
    cut_assert_not_null(data);
    cut_assert_equal_uint(0x12345678, **(uint32_t **)data);
    **(uint32_t **)data = 0x87654321;
}

const struct urlfifo_item_data_ops test_data_ops =
{
    .data_fail = item_data_fail,
    .data_free = item_data_free,
};

const struct urlfifo_item_data_ops test_data_ops_with_fail =
{
    .data_fail = item_data_fail_set_uint32_data,
    .data_free = item_data_free,
};

void test_item_data_callback_is_called_for_fail(void)
{
    uint32_t test_data = 0;
    urlfifo_item_id_t id;

    cut_assert_equal_size(1, urlfifo_push_item(851, default_url,
                                               NULL, NULL, SIZE_MAX, &id,
                                               &test_data,
                                               &test_data_ops_with_fail));

    struct urlfifo_item item = { 0 };
    cut_assert_equal_size(0, urlfifo_pop_item(&item, false));

    cut_assert_true(urlfifo_is_item_valid(&item));
    cut_assert_equal_pointer(&test_data, item.data);
    cut_assert_equal_uint(0, test_data);

    urlfifo_fail_item(&item, NULL);

    cut_assert_true(urlfifo_is_item_valid(&item));
    cut_assert_equal_uint(0x12345678, test_data);

    urlfifo_free_item(&item);

    cut_assert_false(urlfifo_is_item_valid(&item));
    cut_assert_equal_uint(0x87654321, test_data);
}

void test_item_should_fail_only_once(void)
{
    uint32_t test_data = 0;
    urlfifo_item_id_t id;

    cut_assert_equal_size(1, urlfifo_push_item(907, default_url,
                                               NULL, NULL, SIZE_MAX, &id,
                                               &test_data,
                                               &test_data_ops_with_fail));

    struct urlfifo_item item = { 0 };
    cut_assert_equal_size(0, urlfifo_pop_item(&item, false));

    cut_assert_true(urlfifo_is_item_valid(&item));
    cut_assert_equal_int(URLFIFO_FAIL_STATE_NOT_FAILED, item.fail_state);
    cut_assert_equal_pointer(&test_data, item.data);
    cut_assert_equal_uint(0, test_data);

    urlfifo_fail_item(&item, NULL);

    cut_assert_true(urlfifo_is_item_valid(&item));
    cut_assert_equal_int(URLFIFO_FAIL_STATE_FAILURE_DETECTED, item.fail_state);
    cut_assert_equal_uint(0x12345678, test_data);

    test_data = 0;
    urlfifo_fail_item(&item, NULL);

    cut_assert_true(urlfifo_is_item_valid(&item));
    cut_assert_equal_int(URLFIFO_FAIL_STATE_FAILURE_DETECTED, item.fail_state);
    cut_assert_equal_uint(0, test_data);

    test_data = 0x12345678;
    urlfifo_free_item(&item);

    cut_assert_false(urlfifo_is_item_valid(&item));
    cut_assert_equal_uint(0x87654321, test_data);
}

void test_item_data_callbacks_are_called_for_push_pop(void)
{
    uint32_t test_data[2] = { 0, 0 };
    urlfifo_item_id_t ids[2];

    cut_assert_equal_size(1, urlfifo_push_item(851, default_url,
                                               NULL, NULL, SIZE_MAX, &ids[0],
                                               &test_data[0], &test_data_ops));
    cut_assert_equal_size(2, urlfifo_push_item(158, default_url,
                                               NULL, NULL, SIZE_MAX, &ids[1],
                                               &test_data[1], &test_data_ops));

    const struct urlfifo_item *item = urlfifo_unlocked_peek(ids[0]);
    cut_assert_not_null(item);
    cut_assert_equal_pointer(&test_data[0], item->data);
    cut_assert_equal_uint(0, test_data[0]);

    struct urlfifo_item popped = { 0 };
    cut_assert_equal_size(1, urlfifo_pop_item(&popped, false));

    cut_assert_equal_pointer(&test_data[0], popped.data);
    cut_assert_equal_uint(0, test_data[0]);

    test_data[0] = 0x12345678;
    cut_assert_equal_size(0, urlfifo_pop_item(&popped, true));
    cut_assert_equal_uint(0x87654321, test_data[0]);
    cut_assert_equal_uint(0, test_data[1]);

    test_data[1] = 0x12345678;
    urlfifo_free_item(&popped);
    cut_assert_equal_uint(0x87654321, test_data[1]);
}

void test_item_data_callbacks_are_called_for_pop_to_drop(void)
{
    uint32_t test_data[2] = { 0, 0 };
    urlfifo_item_id_t ids[2];

    cut_assert_equal_size(1, urlfifo_push_item(642, default_url,
                                               NULL, NULL, SIZE_MAX, &ids[0],
                                               &test_data[0], &test_data_ops));
    cut_assert_equal_size(2, urlfifo_push_item(172, default_url,
                                               NULL, NULL, SIZE_MAX, &ids[1],
                                               &test_data[1], &test_data_ops));

    const struct urlfifo_item *item = urlfifo_unlocked_peek(ids[0]);
    cut_assert_not_null(item);
    cut_assert_equal_pointer(item, urlfifo_peek());
    cut_assert_equal_pointer(&test_data[0], item->data);
    cut_assert_equal_uint(0, test_data[0]);

    test_data[0] = 0x12345678;
    cut_assert_equal_size(1, urlfifo_pop_item(NULL, false));
    cut_assert_equal_uint(0x87654321, test_data[0]);
    cut_assert_equal_uint(0, test_data[1]);

    item = urlfifo_unlocked_peek(ids[1]);
    cut_assert_not_null(item);
    cut_assert_equal_pointer(item, urlfifo_peek());
    cut_assert_equal_pointer(&test_data[1], item->data);
    cut_assert_equal_uint(0, test_data[1]);

    test_data[0] = 0;
    test_data[1] = 0x12345678;
    cut_assert_equal_size(0, urlfifo_pop_item(NULL, false));
    cut_assert_equal_uint(0, test_data[0]);
    cut_assert_equal_uint(0x87654321, test_data[1]);
}

void test_item_data_callbacks_are_called_for_push_clear(void)
{
    uint32_t test_data[2] = { 0, 0 };
    urlfifo_item_id_t ids[2];

    cut_assert_equal_size(1, urlfifo_push_item(851, default_url,
                                               NULL, NULL, SIZE_MAX, &ids[0],
                                               &test_data[0], &test_data_ops));
    cut_assert_equal_size(2, urlfifo_push_item(158, default_url,
                                               NULL, NULL, SIZE_MAX, &ids[1],
                                               &test_data[1], &test_data_ops));

    const struct urlfifo_item *item = urlfifo_unlocked_peek(ids[0]);
    cut_assert_not_null(item);
    cut_assert_equal_pointer(&test_data[0], item->data);
    cut_assert_equal_uint(0, test_data[0]);

    item = urlfifo_unlocked_peek(ids[1]);
    cut_assert_not_null(item);
    cut_assert_equal_pointer(&test_data[1], item->data);
    cut_assert_equal_uint(0, test_data[1]);

    test_data[0] = 0x12345678;
    test_data[1] = 0x12345678;

    urlfifo_clear(0, NULL);

    cut_assert_equal_uint(0x87654321, test_data[0]);
    cut_assert_equal_uint(0x87654321, test_data[1]);
}

void test_push_many_items_does_not_trash_fifo(void)
{
    urlfifo_item_id_t ids[URLFIFO_MAX_LENGTH];

    for(unsigned int i = 0; i < URLFIFO_MAX_LENGTH; ++i)
    {
        char temp[sizeof(default_url) + 16];

        snprintf(temp, sizeof(temp), "%s %u", default_url, i + 50);
        cut_assert_equal_size(i + 1,
                              urlfifo_push_item(123 + i, temp,
                                                NULL, NULL, SIZE_MAX, &ids[i],
                                                NULL, NULL));
    }

    cut_assert_equal_size(URLFIFO_MAX_LENGTH, urlfifo_get_size());

    /* next push should fail */
    urlfifo_item_id_t id = 12345;
    cut_assert_equal_size(0, urlfifo_push_item(0, default_url,
                                               NULL, NULL, SIZE_MAX, &id,
                                               NULL, NULL));

    cut_assert_equal_size(URLFIFO_MAX_LENGTH, urlfifo_get_size());
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
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(43, default_url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    cut_assert_equal_size(1, urlfifo_push_item(45, default_url,
                                               NULL, NULL, 0, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(1, urlfifo_get_size());

    struct urlfifo_item item = { 0 };

    cut_assert_equal_size(0, urlfifo_pop_item(&item, false));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(45, item.id);

    urlfifo_free_item(&item);
}

void test_push_one_keep_first(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(42, default_url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(43, default_url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    cut_assert_equal_size(2, urlfifo_push_item(45, default_url,
                                               NULL, NULL, 1, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_get_size());

    struct urlfifo_item item = { 0 };

    cut_assert_equal_size(1, urlfifo_pop_item(&item, false));
    cut_assert_equal_size(1, urlfifo_get_size());
    cut_assert_equal_uint(42, item.id);

    cut_assert_equal_size(0, urlfifo_pop_item(&item, true));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(45, item.id);

    urlfifo_free_item(&item);
}

void test_push_one_replace_all_works_on_empty_fifo(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(80, default_url,
                                               NULL, NULL, 0, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(1, urlfifo_get_size());

    struct urlfifo_item item = { 0 };

    cut_assert_equal_size(0, urlfifo_pop_item(&item, false));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(80, item.id);

    urlfifo_free_item(&item);
}

void test_push_one_replace_all_works_on_full_fifo(void)
{
    static const uint16_t max_insertions = 10;

    for(stream_id_t id = 20; id < 20 + max_insertions; ++id)
    {
        if(urlfifo_push_item(id, default_url, NULL, NULL, SIZE_MAX, NULL,
                             NULL, NULL) == 0)
            break;
    }

    cut_assert_not_equal_size(max_insertions, urlfifo_get_size());

    cut_assert_equal_size(1, urlfifo_push_item(90, default_url,
                                               NULL, NULL, 0, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(1, urlfifo_get_size());

    struct urlfifo_item item = { 0 };

    cut_assert_equal_size(0, urlfifo_pop_item(&item, false));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(90, item.id);

    urlfifo_free_item(&item);
}

void test_peek_empty_fifo_returns_null(void)
{
    cut_assert_null(urlfifo_peek());
}

void test_peek_fifo_returns_head_element(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(16, default_url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    const struct urlfifo_item *item = urlfifo_peek();

    cut_assert_not_null(item);
    cut_assert_equal_uint(16, item->id);
    cut_assert_equal_string(default_url, item->url);

    cut_assert_equal_pointer(item, urlfifo_peek());

    cut_assert_equal_size(2, urlfifo_push_item(17, default_url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    cut_assert_equal_pointer(item, urlfifo_peek());

    cut_assert_equal_size(2, urlfifo_clear(0, NULL));
}

void test_pop_empty_fifo_detects_underflow(void)
{
    struct urlfifo_item dummy = { 0 };
    struct urlfifo_item expected;

    memset(&dummy, 0x55, sizeof(dummy));
    memset(&expected, 0x55, sizeof(expected));
    cut_assert_equal_int(-1, urlfifo_pop_item(&dummy, false));

    /* cut_assert_equal_memory() hangs on failure, so we'll use plain memcmp()
     * instead */
    if(memcmp(&expected, &dummy, sizeof(expected) != 0))
        cut_fail("urlfifo_pop_item() trashed memory");
}

void test_pop_item_from_single_item_fifo(void)
{
    urlfifo_item_id_t id;

    cut_assert_equal_size(1, urlfifo_push_item(42, default_url,
                                               NULL, NULL, SIZE_MAX, &id,
                                               NULL, NULL));
    cut_assert_equal_size(1, urlfifo_get_size());

    struct urlfifo_item item = { 0 };

    cut_assert_equal_size(0, urlfifo_pop_item(&item, false));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(42, item.id);
    cut_assert_equal_string(default_url, item.url);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.start_time.type);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.end_time.type);

    urlfifo_free_item(&item);
}

void test_pop_item_from_multi_item_fifo(void)
{
    urlfifo_item_id_t id_first;
    urlfifo_item_id_t id_second;

    cut_assert_equal_size(1, urlfifo_push_item(23, "first",
                                               NULL, NULL, SIZE_MAX, &id_first,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(32, "second",
                                               NULL, NULL, SIZE_MAX, &id_second,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_get_size());

    struct urlfifo_item item = { 0 };

    cut_assert_equal_size(1, urlfifo_pop_item(&item, false));
    cut_assert_equal_size(1, urlfifo_get_size());
    cut_assert_equal_uint(23, item.id);
    cut_assert_equal_string("first", item.url);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.start_time.type);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.end_time.type);

    cut_assert_equal_size(0, urlfifo_pop_item(&item, true));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(32, item.id);
    cut_assert_equal_string("second", item.url);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.start_time.type);
    cut_assert_equal_int(STREAMTIME_TYPE_END_OF_STREAM, item.end_time.type);

    urlfifo_free_item(&item);
}

void test_push_pop_chase(void)
{
    static const stream_id_t id_base = 100;
    static const unsigned int num_of_iterations = 10;

    cut_assert_equal_size(1, urlfifo_push_item(id_base, default_url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    struct urlfifo_item item = { 0 };

    for(unsigned int i = 0; i < num_of_iterations; ++i)
    {
        cut_assert_equal_size(2, urlfifo_push_item(i + id_base + 1, default_url,
                                                   NULL, NULL, SIZE_MAX,
                                                   NULL, NULL, NULL));
        cut_assert_equal_size(2, urlfifo_get_size());

        cut_assert_equal_size(1, urlfifo_pop_item(&item, false));
        cut_assert_equal_size(1, urlfifo_get_size());
        cut_assert_equal_uint(i + id_base, item.id);
        urlfifo_free_item(&item);
    }

    cut_assert_equal_size(0, urlfifo_pop_item(&item, false));
    cut_assert_equal_size(0, urlfifo_get_size());
    cut_assert_equal_uint(num_of_iterations + id_base + 0, item.id);
    urlfifo_free_item(&item);
}

void test_get_queued_ids_count_for_empty_fifo(void)
{
    cut_assert_equal_size(0, urlfifo_get_queued_ids(NULL));
}

void test_get_queued_ids_for_empty_fifo(void)
{
    stream_id_t ids[3 * URLFIFO_MAX_LENGTH];
    memset(ids, 0x55, sizeof(ids));

    cut_assert_equal_size(0, urlfifo_get_queued_ids(&ids[URLFIFO_MAX_LENGTH]));

    for(size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        cut_assert_equal_uint(0x5555, ids[i]);
}

void test_get_queued_ids_for_filled_fifo(void)
{
    for(size_t i = 0; i < URLFIFO_MAX_LENGTH; ++i)
        cut_assert_equal_size(i + 1, urlfifo_push_item(100 + i, "item",
                                                       NULL, NULL, SIZE_MAX,
                                                       NULL, NULL, NULL));

    stream_id_t ids[3 * URLFIFO_MAX_LENGTH];
    memset(ids, 0x55, sizeof(ids));

    cut_assert_equal_size(URLFIFO_MAX_LENGTH,
                          urlfifo_get_queued_ids(&ids[URLFIFO_MAX_LENGTH]));

    for(size_t i = 0 * URLFIFO_MAX_LENGTH; i < 1 * URLFIFO_MAX_LENGTH; ++i)
        cut_assert_equal_uint(0x5555, ids[i]);

    stream_id_t expected_id = 100;
    for(size_t i = 1 * URLFIFO_MAX_LENGTH; i < 2 * URLFIFO_MAX_LENGTH; ++i)
    {
        cut_assert_equal_uint(expected_id, ids[i]);
        ++expected_id;
    }

    for(size_t i = 2 * URLFIFO_MAX_LENGTH; i < 3 * URLFIFO_MAX_LENGTH; ++i)
        cut_assert_equal_uint(0x5555, ids[i]);
}

void test_urlfifo_is_full_interface(void)
{
    cut_assert_false(urlfifo_is_full());

    for(size_t i = 0; i < 10; ++i)
    {
        if(urlfifo_is_full())
        {
            cut_assert_equal_size(0, urlfifo_push_item(0, default_url,
                                                       NULL, NULL, SIZE_MAX,
                                                       NULL, NULL, NULL));
            break;
        }

        cut_assert_equal_size(i + 1, urlfifo_push_item(0, default_url,
                                                       NULL, NULL, SIZE_MAX,
                                                       NULL, NULL, NULL));
    }

    cut_assert_true(urlfifo_is_full());

    struct urlfifo_item dummy = { 0 };
    (void)urlfifo_pop_item(&dummy, false);
    urlfifo_free_item(&dummy);

    cut_assert_false(urlfifo_is_full());
}

void test_urlfifo_find_item_by_url_in_empty_fifo_returns_null(void)
{
    const char *urls[] =
    {
        "http://awesome.stream.com:8080/",
        "x",
        "",
    };

    for(size_t i = 0; i < sizeof(urls) / sizeof(urls[0]); ++i)
    {
        urlfifo_item_id_t iter = urlfifo_find_item_begin();

        cut_assert_null(urlfifo_find_next_item_by_url(&iter, urls[i]));
    }
}

void test_urlfifo_find_item_by_url_in_single_entry_fifo_find_match(void)
{
    const char url[] = "http://find.me/";

    cut_assert_equal_size(1, urlfifo_push_item(19, url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    urlfifo_item_id_t iter = urlfifo_find_item_begin();
    const struct urlfifo_item *item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_not_null(item);
    cut_assert_equal_string(url, item->url);

    item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_null(item);
}

void test_urlfifo_find_item_by_url_in_filled_fifo_finds_match(void)
{
    const char url[] = "http://find.me/";

    cut_assert_equal_size(1, urlfifo_push_item(19, url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(10, "second",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    urlfifo_item_id_t iter = urlfifo_find_item_begin();
    const struct urlfifo_item *item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_not_null(item);
    cut_assert_equal_string(url, item->url);

    item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_null(item);
}

void test_urlfifo_find_item_by_url_in_filled_fifo_finds_last_match(void)
{
    const char url[] = "http://find.me/";

    cut_assert_equal_size(1, urlfifo_push_item(10, "first",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(25, url,
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    urlfifo_item_id_t iter = urlfifo_find_item_begin();
    const struct urlfifo_item *item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_not_null(item);
    cut_assert_equal_string(url, item->url);

    item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_null(item);
}

void test_urlfifo_find_item_by_url_in_filled_fifo_finds_multiple_matches(void)
{
    const char url[] = "http://find.me/";

    urlfifo_item_id_t expected_first_found_id;
    urlfifo_item_id_t expected_second_found_id;

    cut_assert_equal_size(1, urlfifo_push_item(4, url,
                                               NULL, NULL, SIZE_MAX,
                                               &expected_first_found_id,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(25, url,
                                               NULL, NULL, SIZE_MAX,
                                               &expected_second_found_id,
                                               NULL, NULL));

    urlfifo_item_id_t iter = urlfifo_find_item_begin();
    const struct urlfifo_item *item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_not_null(item);
    cut_assert_equal_string(url, item->url);
    cut_assert_equal_pointer(urlfifo_unlocked_peek(expected_first_found_id), item);

    item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_not_null(item);
    cut_assert_equal_string(url, item->url);
    cut_assert_equal_pointer(urlfifo_unlocked_peek(expected_second_found_id), item);

    item = urlfifo_find_next_item_by_url(&iter, url);
    cut_assert_null(item);
}

void test_urlfifo_find_item_by_url_can_be_restarted(void)
{
    const char url[] = "http://find.me/";

    urlfifo_item_id_t expected_first_found_id;
    urlfifo_item_id_t expected_second_found_id;

    cut_assert_equal_size(1, urlfifo_push_item(4, url,
                                               NULL, NULL, SIZE_MAX,
                                               &expected_first_found_id,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(25, url,
                                               NULL, NULL, SIZE_MAX,
                                               &expected_second_found_id,
                                               NULL, NULL));

    for(int i = 0; i < 3; ++i)
    {
        urlfifo_item_id_t iter = urlfifo_find_item_begin();
        const struct urlfifo_item *item = urlfifo_find_next_item_by_url(&iter, url);
        cut_assert_not_null(item);
        cut_assert_equal_string(url, item->url);
        cut_assert_equal_pointer(urlfifo_unlocked_peek(expected_first_found_id), item);

        item = urlfifo_find_next_item_by_url(&iter, url);
        cut_assert_not_null(item);
        cut_assert_equal_string(url, item->url);
        cut_assert_equal_pointer(urlfifo_unlocked_peek(expected_second_found_id), item);

        item = urlfifo_find_next_item_by_url(&iter, url);
        cut_assert_null(item);
    }
}

void test_urlfifo_find_item_by_url_in_filled_fifo_may_find_nothing(void)
{
    cut_assert_equal_size(1, urlfifo_push_item(1, "first",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));
    cut_assert_equal_size(2, urlfifo_push_item(11, "second",
                                               NULL, NULL, SIZE_MAX, NULL,
                                               NULL, NULL));

    urlfifo_item_id_t iter = urlfifo_find_item_begin();

    cut_assert_null(urlfifo_find_next_item_by_url(&iter, default_url));
}
