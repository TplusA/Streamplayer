/*
 * Copyright (C) 2015--2018, 2020  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of T+A Streamplayer.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cppcutter.h>
#include <memory>
#include <string>
#include <algorithm>

#include "urlfifo.hh"

/*!
 * \addtogroup urlfifo_tests Unit tests
 * \ingroup urlfifo
 *
 * URL FIFO unit tests.
 */
/*!@{*/

namespace urlfifo_tests
{

class TestItem
{
  public:
    const unsigned int value_;
    const std::string name_;

    TestItem(const TestItem &) = delete;
    TestItem &operator=(const TestItem &) = delete;

    explicit TestItem(unsigned int value, const std::string &name):
        value_(value),
        name_(name)
    {}
};

static constexpr size_t MAX_QUEUE_LENGTH = 8;
PlayQueue::Queue<TestItem> *queue;

void cut_setup()
{
    queue = new PlayQueue::Queue<TestItem>(MAX_QUEUE_LENGTH);
}

void cut_teardown()
{
    delete queue;
    queue = nullptr;
}

/*!\test
 * After initialization, the URL FIFO shall be empty.
 */
void test_fifo_is_empty_on_startup()
{
    cut_assert_equal_size(0, queue->size());
    cut_assert_true(queue->empty());
    cut_assert_false(queue->full());
    cppcut_assert_equal(size_t(0),
                        size_t(std::distance(queue->begin(), queue->end())));
}

/*!\test
 * Clearing the whole FIFO works as expected with an empty FIFO.
 */
void test_clear_all_on_empty_fifo()
{
    cut_assert_true(queue->clear(0).empty());
}

template <typename StringType>
static unsigned int push(PlayQueue::Queue<TestItem> &q, unsigned int value,
                         const StringType &name, size_t expected_result,
                         size_t keep_first_n = SIZE_MAX)
{
    const size_t result =
        q.push(std::unique_ptr<TestItem>(new TestItem(value, name)),
               keep_first_n);

    cppcut_assert_equal(expected_result, result);

    return result;
}

/*!\test
 * Clearing the whole FIFO results in an empty FIFO.
 */
void test_clear_non_empty_fifo()
{
    cut_assert_true(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 23, "first", 1);

    cut_assert_false(queue->empty());

    push(*queue, 32, "second", 2);

    cut_assert_false(queue->empty());
    cppcut_assert_equal(size_t(2), queue->size());
    cppcut_assert_equal(size_t(2),
                        size_t(std::distance(queue->begin(), queue->end())));

    cppcut_assert_equal(size_t(2), queue->clear(0).size());

    cut_assert_true(queue->empty());
    cppcut_assert_equal(size_t(0), queue->size());
    cppcut_assert_equal(size_t(0),
                        size_t(std::distance(queue->begin(), queue->end())));
}

/*!\test
 * Clearing the last few items in a non-empty FIFO results in a FIFO with as
 * many entries as have been specified in the argument to
 * #PlayQueue::Queue::clear().
 */
void test_clear_partial_non_empty_fifo()
{
    push(*queue, 23, "first",  1);
    push(*queue, 32, "second", 2);

    auto removed(queue->clear(1));
    cppcut_assert_equal(size_t(1), removed.size());

    cppcut_assert_equal(32U,      removed[0]->value_);
    cppcut_assert_equal("second", removed[0]->name_.c_str());

    cppcut_assert_equal(size_t(1), queue->size());

    const TestItem *item = queue->peek();
    cppcut_assert_not_null(item);
    cppcut_assert_equal(23U,     item->value_);
    cppcut_assert_equal("first", item->name_.c_str());
}

/*!\test
 * Like #test_clear_partial_non_empty_fifo(), but pop items beforehand.
 *
 * Because we are operating on a ring buffer.
 */
void test_partial_clear_after_pop_item_from_multi_item_fifo()
{
    cut_assert_true(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 23, "first", 1);
    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 32, "second", 2);
    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 123, "third", 3);
    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 132, "fourth", 4);
    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 223, "fifth", 5);
    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 232, "sixth", 6);
    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 323, "seventh", 7);
    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());

    push(*queue, 332, "eighth", 8);
    cut_assert_false(queue->empty());
    cut_assert_true(queue->full());

    cppcut_assert_equal(size_t(8), queue->size());

    size_t remaining;
    auto item = queue->pop(remaining);

    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(size_t(7), remaining);
    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());
    cppcut_assert_equal(23U,     item->value_);
    cppcut_assert_equal("first", item->name_.c_str());

    push(*queue, 42, "ninth", 8);
    cut_assert_false(queue->empty());
    cut_assert_true(queue->full());

    auto removed = queue->clear(1);
    cppcut_assert_equal(size_t(7), removed.size());

    cut_assert_false(queue->empty());
    cut_assert_false(queue->full());
    cppcut_assert_equal(size_t(1), queue->size());
    cppcut_assert_equal(42U,  removed[0]->value_);
    cppcut_assert_equal(332U, removed[1]->value_);
    cppcut_assert_equal(323U, removed[2]->value_);
    cppcut_assert_equal(232U, removed[3]->value_);
    cppcut_assert_equal(223U, removed[4]->value_);
    cppcut_assert_equal(132U, removed[5]->value_);
    cppcut_assert_equal(123U, removed[6]->value_);

    item = queue->pop(remaining);

    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(size_t(0), remaining);

    cut_assert_true(queue->empty());
    cut_assert_false(queue->full());
    cppcut_assert_equal(32U,      item->value_);
    cppcut_assert_equal("second", item->name_.c_str());
}

/*!\test
 * Attempting to clear a FIFO with fewer items than specified in the argument
 * to #PlayQueue::Queue::clear() results in unchanged FIFO content. No items
 * are removed in this case.
 */
void test_clear_partial_with_fewer_items_than_to_be_kept_does_nothing()
{
    push(*queue, 23, "first", 1);
    push(*queue, 32, "second", 2);
    cut_assert_equal_size(2, queue->size());

    cut_assert_true(queue->clear(3).empty());
    cut_assert_true(queue->clear(SIZE_MAX).empty());
    cut_assert_true(queue->clear(2).empty());

    cppcut_assert_equal(size_t(2), queue->size());

    const TestItem *item = queue->peek();
    cppcut_assert_not_null(item);
    cppcut_assert_equal("first", item->name_.c_str());

    item = queue->peek(1);
    cppcut_assert_not_null(item);
    cppcut_assert_equal("second", item->name_.c_str());
}

static const std::string default_url("http://ta-hifi.de/");

/*!\test
 * Add a single item to an empty FIFO.
 */
void test_push_single_item()
{
    push(*queue, 42, default_url, 1);
    cppcut_assert_equal(size_t(1), queue->size());

    const TestItem *item = queue->peek();
    cppcut_assert_not_null(item);
    cppcut_assert_equal(42U, item->value_);
    cppcut_assert_equal(default_url, item->name_);
}

/*!\test
 * Add more than a single item to an empty FIFO.
 */
void test_push_multiple_items()
{
    static constexpr size_t count = 2;

    for(unsigned int i = 0; i < count; ++i)
    {
        std::ostringstream temp;
        temp << default_url << ' ' << i;
        cppcut_assert_equal(i + 1, push(*queue, 23 + i, temp.str(), i + 1));
    }

    cppcut_assert_equal(count, queue->size());

    /* check if we can read back what we've written */
    for(unsigned int i = 0; i < count; ++i)
    {
        const TestItem *item = queue->peek(i);

        std::ostringstream temp;
        temp <<  default_url << ' ' << i;

        cppcut_assert_not_null(item);
        cppcut_assert_equal(23 + i, item->value_);
        cppcut_assert_equal(temp.str(), item->name_);
    }
}

/*!\test
 * Adding more item to the FIFO than it has slots available results in an error
 * returned by #PlayQueue::Queue::push(). The FIFO is expected to remain
 * changed after such an overflow.
 */
void test_push_many_items_does_not_trash_fifo()
{
    for(unsigned int i = 0; i < MAX_QUEUE_LENGTH; ++i)
    {
        std::ostringstream temp;
        temp <<  default_url << ' ' << i + 50;
        push(*queue, 123 + i, temp.str(), i + 1);
    }

    cppcut_assert_equal(MAX_QUEUE_LENGTH, queue->size());

    /* next push should fail */
    push(*queue, 0, default_url, 0);

    cppcut_assert_equal(MAX_QUEUE_LENGTH, queue->size());

    /* check that FIFO still has the expected content */
    for(unsigned int i = 0; i < MAX_QUEUE_LENGTH; ++i)
    {
        const TestItem *item = queue->peek(i);

        std::ostringstream temp;
        temp <<  default_url << ' ' << i + 50;

        cppcut_assert_not_null(item);
        cppcut_assert_equal(123 + i, item->value_);
        cppcut_assert_equal(temp.str(), item->name_);
    }
}

/*!\test
 * It is possible to replace the items in a non-empty URL FIFO by a single item
 * by pushing the new item and specifying the number of items to keep as 0.
 */
void test_push_one_replace_all()
{
    push(*queue, 42, default_url, 1);
    push(*queue, 43, default_url, 2);
    push(*queue, 45, default_url, 1, 0);

    cppcut_assert_equal(size_t(1), queue->size());

    size_t remaining = 987;
    auto item = queue->pop(remaining);

    cut_assert_true(queue->empty());
    cppcut_assert_equal(size_t(0), remaining);
    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(45U, item->value_);
}

/*!\test
 * It is possible to replace the last few items in a non-empty URL FIFO by a
 * single item by pushing the new item and specifying the number of items to
 * keep as 1 (or greater).
 */
void test_push_one_keep_first()
{
    push(*queue, 42, default_url, 1);
    push(*queue, 43, default_url, 2);
    push(*queue, 45, default_url, 2, 1);

    cppcut_assert_equal(size_t(2), queue->size());

    size_t remaining = 987;
    auto item = queue->pop(remaining);

    cppcut_assert_equal(size_t(1), remaining);
    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(size_t(1), queue->size());
    cppcut_assert_equal(42U, item->value_);

    item = queue->pop();

    cppcut_assert_equal(size_t(0), queue->size());
    cut_assert_true(queue->empty());
    cppcut_assert_equal(45U, item->value_);
}

/*!\test
 * Replacing the contents of an empty URL FIFO with a new item is possible.
 */
void test_push_one_replace_all_works_on_empty_fifo()
{
    push(*queue, 80, default_url, 1, 0);
    cppcut_assert_equal(size_t(1), queue->size());

    std::unique_ptr<TestItem> item;
    cut_assert_true(queue->pop(item));

    cut_assert_true(queue->empty());
    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(80U, item->value_);
    cppcut_assert_equal(default_url, item->name_);
}

/*!\test
 * Replacing the contents of an overflown URL FIFO with a new item is possible.
 */
void test_push_one_replace_all_works_on_full_fifo()
{
    static constexpr unsigned int max_insertions = 10;

    for(unsigned int id = 20; id < 20 + max_insertions; ++id)
    {
        const size_t result =
            queue->push(std::unique_ptr<TestItem>(new TestItem(id, default_url)),
                        SIZE_MAX);

        if(result == 0)
            break;
    }

    cppcut_assert_operator(size_t(max_insertions), >, queue->size());

    push(*queue, 90, default_url, 1, 0);
    cppcut_assert_equal(size_t(1), queue->size());

    auto item = queue->pop();

    cut_assert_true(queue->empty());
    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(90U, item->value_);
    cppcut_assert_equal(default_url, item->name_);
}

/*!\test
 * Empty URL FIFO is handled correctly.
 */
void test_peek_empty_fifo_returns_null()
{
    cppcut_assert_null(queue->peek());
    cppcut_assert_null(queue->peek(0));
    cppcut_assert_null(queue->peek(1));
    cppcut_assert_null(queue->peek(2));
}

/*!\test
 * Head element can be inspected, also several times.
 */
void test_peek_fifo_returns_head_element()
{
    push(*queue, 16, default_url, 1);

    const TestItem *item = queue->peek();

    cppcut_assert_not_null(item);
    cppcut_assert_equal(16U, item->value_);
    cppcut_assert_equal(default_url, item->name_);

    cppcut_assert_equal(item, queue->peek());

    push(*queue, 17, default_url, 2);

    cppcut_assert_equal(item, queue->peek());

    cppcut_assert_equal(size_t(2), queue->clear(0).size());
}

/*!\test
 * Removing a non-existent first item from the URL FIFO results in an error
 * returned by #PlayQueue::Queue::pop().
 */
void test_pop_empty_fifo_detects_underflow()
{
    std::unique_ptr<TestItem> dummy;
    cut_assert_false(queue->pop(dummy));
    cppcut_assert_null(dummy.get());

    cppcut_assert_null(queue->pop().get());
}

/*!\test
 * Remove first item from the URL FIFO which contains a single item.
 */
void test_pop_item_from_single_item_fifo()
{
    push(*queue, 42, default_url, 1);

    cppcut_assert_equal(size_t(1), queue->size());

    size_t remaining = 123;
    auto item = queue->pop(remaining);

    cut_assert_true(queue->empty());
    cppcut_assert_equal(size_t(0), remaining);
    cppcut_assert_equal(42U, item->value_);
    cppcut_assert_equal(default_url, item->name_);
}

/*!\test
 * Remove first item from the URL FIFO which contains more that one item.
 */
void test_pop_item_from_multi_item_fifo()
{
    push(*queue, 23, "first", 1);
    push(*queue, 32, "second", 2);

    cppcut_assert_equal(size_t(2), queue->size());

    auto item = queue->pop();

    cppcut_assert_equal(size_t(1), queue->size());
    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(23U, item->value_);
    cppcut_assert_equal("first", item->name_.c_str());

    item = queue->pop();

    cut_assert_true(queue->empty());
    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(32U, item->value_);
    cppcut_assert_equal("second", item->name_.c_str());
}

/*!\test
 * Stress test push and pop to trigger internal wraparound handling code.
 */
void test_push_pop_chase()
{
    static constexpr unsigned int id_base = 100;
    static constexpr unsigned int num_of_iterations = 10;

    push(*queue, id_base, default_url, 1);

    for(unsigned int i = 0; i < num_of_iterations; ++i)
    {
        push(*queue, i + id_base + 1, default_url, 2);
        cppcut_assert_equal(size_t(2), queue->size());

        auto item = queue->pop();

        cppcut_assert_equal(size_t(1), queue->size());
        cppcut_assert_not_null(item.get());
        cppcut_assert_equal(i + id_base, item->value_);
    }

    size_t remaining = 5;
    auto item = queue->pop(remaining);

    cut_assert_true(queue->empty());
    cppcut_assert_not_null(item.get());
    cppcut_assert_equal(size_t(0), remaining);
    cppcut_assert_equal(num_of_iterations + id_base + 0, item->value_);
}

/*!\test
 * No items to iterate in empty URL FIFO.
 */
void test_get_queued_ids_count_for_empty_fifo()
{
    cut_assert_true(queue->begin() == queue->end());
    cppcut_assert_equal(ssize_t(0), std::distance(queue->begin(), queue->end()));
}

/*!\test
 * Queued IDs are returned.
 */
void test_get_queued_ids_for_filled_fifo()
{
    for(size_t i = 0; i < MAX_QUEUE_LENGTH; ++i)
        push(*queue, 100 + i, "item", i + 1);

    cppcut_assert_equal(ssize_t(MAX_QUEUE_LENGTH),
                        std::distance(queue->begin(), queue->end()));

    unsigned int expected_id = 100;
    for(const auto &it : *queue)
    {
        cppcut_assert_equal(expected_id, it->value_);
        cppcut_assert_equal("item", it->name_.c_str());
        ++expected_id;
    }
}

/*!\test
 * Basic tests for #PlayQueue::Queue::full().
 */
void test_urlfifo_is_full_interface()
{
    cut_assert_false(queue->full());

    for(size_t i = 0; i < 10; ++i)
    {
        if(queue->full())
        {
            push(*queue, 0, default_url, 0);
            break;
        }

        push(*queue, 0, default_url, i + 1);
    }

    cut_assert_true(queue->full());

    queue->pop();

    cut_assert_false(queue->full());
}

}

/*!@}*/
