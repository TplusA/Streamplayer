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

#include <doctest.h>

#include <memory>
#include <string>
#include <sstream>
#include <algorithm>

#include "urlfifo.hh"

/*!
 * \addtogroup urlfifo_tests Unit tests
 * \ingroup urlfifo
 *
 * URL FIFO unit tests.
 */
/*!@{*/

TEST_SUITE_BEGIN("Play queue");

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

class Fixture
{
  public:
    static constexpr size_t MAX_QUEUE_LENGTH = 8;

  protected:
    std::unique_ptr<PlayQueue::Queue<TestItem>> queue;

  public:
    explicit Fixture():
        queue(std::make_unique<PlayQueue::Queue<TestItem>>(MAX_QUEUE_LENGTH))
    {}
};

constexpr size_t Fixture::MAX_QUEUE_LENGTH;

/*!\test
 * After initialization, the URL FIFO shall be empty.
 */
TEST_CASE_FIXTURE(Fixture, "Queue is empty on startup")
{
    CHECK(queue->size() == 0);
    CHECK(queue->empty());
    CHECK_FALSE(queue->full());
    CHECK(std::distance(queue->begin(), queue->end()) == 0);
}

/*!\test
 * Clearing the whole FIFO works as expected with an empty FIFO.
 */
TEST_CASE_FIXTURE(Fixture, "Clear an empty queue")
{
    CHECK(queue->clear(0).empty());
}

template <typename StringType>
static unsigned int push(PlayQueue::Queue<TestItem> &q, unsigned int value,
                         const StringType &name, size_t expected_result,
                         size_t keep_first_n = SIZE_MAX)
{
    const size_t result =
        q.push(std::unique_ptr<TestItem>(new TestItem(value, name)),
               keep_first_n);

    CHECK(result == expected_result);

    return result;
}

/*!\test
 * Clearing the whole FIFO results in an empty FIFO.
 */
TEST_CASE_FIXTURE(Fixture, "Clear non-empty queue")
{
    CHECK(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 23, "first", 1);

    CHECK_FALSE(queue->empty());

    push(*queue, 32, "second", 2);

    CHECK_FALSE(queue->empty());
    CHECK(queue->size() == 2);
    CHECK(std::distance(queue->begin(), queue->end()) == 2);

    CHECK(queue->clear(0).size() == 2);

    CHECK(queue->empty());
    CHECK(queue->size() == 0);
    CHECK(std::distance(queue->begin(), queue->end()) == 0);
}

/*!\test
 * Clearing the last few items in a non-empty FIFO results in a FIFO with as
 * many entries as have been specified in the argument to
 * #PlayQueue::Queue::clear().
 */
TEST_CASE_FIXTURE(Fixture, "Clear partial non-empty queue")
{
    push(*queue, 23, "first",  1);
    push(*queue, 32, "second", 2);

    auto removed(queue->clear(1));
    REQUIRE(removed.size() == 1);

    CHECK(removed[0]->value_ == 32);
    CHECK(removed[0]->name_ == "second");

    CHECK(queue->size() == 1);

    const TestItem *item = queue->peek();
    REQUIRE(item != nullptr);
    CHECK(item->value_ == 23);
    CHECK(item->name_ == "first");
}

/*!\test
 * Clearing the last few items in a non-empty FIFO after popping some items
 * results in a FIFO with as many entries as have been specified in the
 * argument to #PlayQueue::Queue::clear().
 *
 * Because we are operating on a ring buffer.
 */
TEST_CASE_FIXTURE(Fixture, "Partial clear after pop from queue with multi items")
{
    CHECK(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 23, "first", 1);
    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 32, "second", 2);
    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 123, "third", 3);
    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 132, "fourth", 4);
    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 223, "fifth", 5);
    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 232, "sixth", 6);
    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 323, "seventh", 7);
    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());

    push(*queue, 332, "eighth", 8);
    CHECK_FALSE(queue->empty());
    CHECK(queue->full());

    CHECK(queue->size() == 8);

    size_t remaining;
    auto item = queue->pop(remaining);

    REQUIRE(item != nullptr);
    CHECK(remaining == 7);
    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());
    CHECK(item->value_ == 23);
    CHECK(item->name_ == "first");

    push(*queue, 42, "ninth", 8);
    CHECK_FALSE(queue->empty());
    CHECK(queue->full());

    auto removed = queue->clear(1);
    CHECK(removed.size() == 7);

    CHECK_FALSE(queue->empty());
    CHECK_FALSE(queue->full());
    CHECK(queue->size() == 1);
    CHECK(removed[0]->value_ == 42);
    CHECK(removed[1]->value_ == 332);
    CHECK(removed[2]->value_ == 323);
    CHECK(removed[3]->value_ == 232);
    CHECK(removed[4]->value_ == 223);
    CHECK(removed[5]->value_ == 132);
    CHECK(removed[6]->value_ == 123);

    item = queue->pop(remaining);

    REQUIRE(item != nullptr);
    CHECK(remaining == 0);

    CHECK(queue->empty());
    CHECK_FALSE(queue->full());
    CHECK(item->value_ == 32);
    CHECK(item->name_ == "second");
}

/*!\test
 * Attempting to clear a FIFO with fewer items than specified in the argument
 * to #PlayQueue::Queue::clear() results in unchanged FIFO content. No items
 * are removed in this case.
 */
TEST_CASE_FIXTURE(Fixture,
                  "Partial clear with fewer items than available keeps queue unchanged")
{
    push(*queue, 23, "first", 1);
    push(*queue, 32, "second", 2);
    CHECK(queue->size() == 2);

    CHECK(queue->clear(3).empty());
    CHECK(queue->clear(SIZE_MAX).empty());
    CHECK(queue->clear(2).empty());

    CHECK(queue->size() == 2);

    const TestItem *item = queue->peek();
    REQUIRE(item != nullptr);
    CHECK(item->name_ == "first");

    item = queue->peek(1);
    REQUIRE(item != nullptr);
    CHECK(item->name_ == "second");
}

static const std::string default_url("http://ta-hifi.de/");

/*!\test
 * Add a single item to an empty FIFO.
 */
TEST_CASE_FIXTURE(Fixture, "Push single item")
{
    push(*queue, 42, default_url, 1);
    CHECK(queue->size() == 1);

    const TestItem *item = queue->peek();
    REQUIRE(item != nullptr);
    CHECK(item->value_ == 42);
    CHECK(item->name_ == default_url);
}

/*!\test
 * Add more than a single item to an empty FIFO.
 */
TEST_CASE_FIXTURE(Fixture, "Push multiple items")
{
    static constexpr size_t count = 2;

    for(unsigned int i = 0; i < count; ++i)
    {
        std::ostringstream temp;
        temp << default_url << ' ' << i;
        CHECK(push(*queue, 23 + i, temp.str(), i + 1) == i + 1);
    }

    CHECK(queue->size() == count);

    /* check if we can read back what we've written */
    for(unsigned int i = 0; i < count; ++i)
    {
        const TestItem *item = queue->peek(i);

        std::ostringstream temp;
        temp <<  default_url << ' ' << i;

        REQUIRE(item != nullptr);
        CHECK(item->value_ == 23 + i);
        CHECK(item->name_ == temp.str());
    }
}

/*!\test
 * Adding more item to the FIFO than it has slots available results in an error
 * returned by #PlayQueue::Queue::push(). The FIFO is expected to remain
 * changed after such an overflow.
 */
TEST_CASE_FIXTURE(Fixture, "Items pushed into full queue get ignored")
{
    for(unsigned int i = 0; i < MAX_QUEUE_LENGTH; ++i)
    {
        std::ostringstream temp;
        temp <<  default_url << ' ' << i + 50;
        push(*queue, 123 + i, temp.str(), i + 1);
    }

    CHECK(queue->size() == MAX_QUEUE_LENGTH);

    /* next push should fail */
    push(*queue, 0, default_url, 0);

    CHECK(queue->size() == MAX_QUEUE_LENGTH);

    /* check that FIFO still has the expected content */
    for(unsigned int i = 0; i < MAX_QUEUE_LENGTH; ++i)
    {
        const TestItem *item = queue->peek(i);

        std::ostringstream temp;
        temp <<  default_url << ' ' << i + 50;

        REQUIRE(item != nullptr);
        CHECK(item->value_ == 123 + i);
        CHECK(item->name_ == temp.str());
    }
}

/*!\test
 * It is possible to replace the items in a non-empty URL FIFO by a single item
 * by pushing the new item and specifying the number of items to keep as 0.
 */
TEST_CASE_FIXTURE(Fixture, "Push one item to replace all others")
{
    push(*queue, 42, default_url, 1);
    push(*queue, 43, default_url, 2);
    push(*queue, 45, default_url, 1, 0);

    CHECK(queue->size() == 1);

    size_t remaining = 987;
    auto item = queue->pop(remaining);

    CHECK(queue->empty());
    CHECK(remaining == 0);
    REQUIRE(item != nullptr);
    CHECK(item->value_ == 45);
}

/*!\test
 * It is possible to replace the last few items in a non-empty URL FIFO by a
 * single item by pushing the new item and specifying the number of items to
 * keep as 1 (or greater).
 */
TEST_CASE_FIXTURE(Fixture, "Push one item and keep first in queue")
{
    push(*queue, 42, default_url, 1);
    push(*queue, 43, default_url, 2);
    push(*queue, 45, default_url, 2, 1);

    CHECK(queue->size() == 2);

    size_t remaining = 987;
    auto item = queue->pop(remaining);

    CHECK(remaining == 1);
    REQUIRE(item != nullptr);
    CHECK(queue->size() == 1);
    CHECK(item->value_ == 42);

    item = queue->pop();

    CHECK(queue->size() == 0);
    CHECK(queue->empty());
    CHECK(item->value_ == 45);
}

/*!\test
 * Replacing the contents of an empty URL FIFO with a new item is possible.
 */
TEST_CASE_FIXTURE(Fixture, "Push one item to replace all others on empty queue")
{
    push(*queue, 80, default_url, 1, 0);
    CHECK(queue->size() == 1);

    std::unique_ptr<TestItem> item;
    CHECK(queue->pop(item));

    CHECK(queue->empty());
    REQUIRE(item != nullptr);
    CHECK(item->value_ == 80);
    CHECK(item->name_ == default_url);
}

/*!\test
 * Replacing the contents of an overflown URL FIFO with a new item is possible.
 */
TEST_CASE_FIXTURE(Fixture, "Push one item and keep first in queue on empty queue")
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

    CHECK(queue->size() < max_insertions);

    push(*queue, 90, default_url, 1, 0);
    CHECK(queue->size() == 1);

    auto item = queue->pop();

    CHECK(queue->empty());
    REQUIRE(item != nullptr);
    CHECK(item->value_ == 90);
    CHECK(item->name_ == default_url);
}

/*!\test
 * Empty URL FIFO is handled correctly.
 */
TEST_CASE_FIXTURE(Fixture, "Peek empty queue returns nullptr")
{
    CHECK(queue->peek() == nullptr);
    CHECK(queue->peek(0) == nullptr);
    CHECK(queue->peek(1) == nullptr);
    CHECK(queue->peek(2) == nullptr);
}

/*!\test
 * Head element can be inspected, also several times.
 */
TEST_CASE_FIXTURE(Fixture, "Peek returns same head element on each call")
{
    push(*queue, 16, default_url, 1);

    const TestItem *item = queue->peek();

    REQUIRE(item != nullptr);
    CHECK(item->value_ == 16);
    CHECK(item->name_ == default_url);

    CHECK(queue->peek() == item);

    push(*queue, 17, default_url, 2);

    CHECK(queue->peek() == item);
    CHECK(queue->clear(0).size() == 2);
}

/*!\test
 * Removing a non-existent first item from the URL FIFO results in an error
 * returned by #PlayQueue::Queue::pop().
 */
TEST_CASE_FIXTURE(Fixture, "Pop from empty queue detects underflow")
{
    std::unique_ptr<TestItem> dummy;
    CHECK_FALSE(queue->pop(dummy));
    CHECK(dummy == nullptr);
    CHECK(queue->pop() == nullptr);
}

/*!\test
 * Remove first item from the URL FIFO which contains a single item.
 */
TEST_CASE_FIXTURE(Fixture, "Pop from queue with single item moves the item to caller")
{
    push(*queue, 42, default_url, 1);

    CHECK(queue->size() == 1);

    size_t remaining = 123;
    auto item = queue->pop(remaining);

    CHECK(queue->empty());
    CHECK(remaining == 0);
    REQUIRE(item != nullptr);
    CHECK(item->value_ == 42);
    CHECK(item->name_ == default_url);
}

/*!\test
 * Remove first item from the URL FIFO which contains more that one item.
 */
TEST_CASE_FIXTURE(Fixture, "Pop multiple items from queue")
{
    push(*queue, 23, "first", 1);
    push(*queue, 32, "second", 2);

    CHECK(queue->size() == 2);

    auto item = queue->pop();

    CHECK(queue->size() == 1);
    REQUIRE(item != nullptr);
    CHECK(item->value_ == 23);
    CHECK(item->name_ == "first");

    item = queue->pop();

    CHECK(queue->empty());
    REQUIRE(item != nullptr);
    CHECK(item->value_ == 32);
    CHECK(item->name_ == "second");
}

/*!\test
 * Stress test push and pop to trigger internal wraparound handling code.
 */
TEST_CASE_FIXTURE(Fixture, "Push/pop chase stress test")
{
    static constexpr unsigned int id_base = 100;
    static constexpr unsigned int num_of_iterations = 10;

    push(*queue, id_base, default_url, 1);

    for(unsigned int i = 0; i < num_of_iterations; ++i)
    {
        push(*queue, i + id_base + 1, default_url, 2);
        CHECK(queue->size() == 2);

        auto item = queue->pop();

        CHECK(queue->size() == 1);
        REQUIRE(item != nullptr);
        CHECK(item->value_ == i + id_base);
    }

    size_t remaining = 5;
    auto item = queue->pop(remaining);

    CHECK(queue->empty());
    REQUIRE(item != nullptr);
    CHECK(remaining == 0);
    CHECK(item->value_ == num_of_iterations + id_base + 0);
}

/*!\test
 * No items to iterate in empty URL FIFO.
 */
TEST_CASE_FIXTURE(Fixture, "Number of queued stream IDs in empty queue is 0")
{
    CHECK(queue->begin() == queue->end());
    CHECK(std::distance(queue->begin(), queue->end()) == 0);
}

/*!\test
 * Queued IDs are returned.
 */
TEST_CASE_FIXTURE(Fixture, "Queued stream IDs can be read out from filled queue")
{
    for(size_t i = 0; i < MAX_QUEUE_LENGTH; ++i)
        push(*queue, 100 + i, "item", i + 1);

    CHECK(std::distance(queue->begin(), queue->end()) == MAX_QUEUE_LENGTH);

    unsigned int expected_id = 100;
    for(const auto &it : *queue)
    {
        CHECK(it->value_ == expected_id);
        CHECK(it->name_ == "item");
        ++expected_id;
    }
}

/*!\test
 * Basic tests for #PlayQueue::Queue::full().
 */
TEST_CASE_FIXTURE(Fixture, "Properties of full() function member")
{
    CHECK_FALSE(queue->full());

    for(size_t i = 0; i < 10; ++i)
    {
        if(queue->full())
        {
            push(*queue, 0, default_url, 0);
            break;
        }

        push(*queue, 0, default_url, i + 1);
    }

    CHECK(queue->full());

    queue->pop();

    CHECK_FALSE(queue->full());
}

TEST_SUITE_END();

/*!@}*/
