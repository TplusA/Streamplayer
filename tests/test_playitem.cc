/*
 * Copyright (C) 2018, 2020, 2021, 2022  T+A elektroakustik GmbH & Co. KG
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

#include <array>

#include "playitem.hh"

#define MOCK_EXPECTATION_WITH_EXPECTATION_SEQUENCE_SINGLETON
#include "mock_messages.hh"

/*!
 * \addtogroup playitem_tests Unit tests
 * \ingroup urlfifo
 *
 * Play item tests (items stored inside a URL FIFO).
 */
/*!@{*/

std::shared_ptr<MockExpectationSequence> mock_expectation_sequence_singleton =
    std::make_shared<MockExpectationSequence>();

TEST_SUITE_BEGIN("Play queue item");

class Fixture
{
  protected:
    std::unique_ptr<MockMessages::Mock> mock_messages;

  public:
    explicit Fixture():
        mock_messages(std::make_unique<MockMessages::Mock>())
    {
        mock_expectation_sequence_singleton->reset();
        MockMessages::singleton = mock_messages.get();
    }

    ~Fixture()
    {
        try
        {
            mock_expectation_sequence_singleton->done();
            mock_messages->done();
        }
        catch(...)
        {
            /* no throwing from dtors */
        }

        MockMessages::singleton = nullptr;
    }
};

static GVariantWrapper default_key()
{
    return GVariantWrapper(g_variant_new_string("StreamKey"));
}

static std::string default_url()
{
    return "http://stream.me/now";
}

/*!\test
 * The various states can be set freely.
 */
TEST_CASE_FIXTURE(Fixture, "Set state does not consider current state")
{
    PlayQueue::Item it(25, default_key(), default_url(), "", true, "", {}, nullptr,
                       std::chrono::time_point<std::chrono::nanoseconds>::min(),
                       std::chrono::time_point<std::chrono::nanoseconds>::max());

    CHECK(it.get_state() == PlayQueue::ItemState::IN_QUEUE);

    static const std::array<PlayQueue::ItemState, 5> states
    {
        PlayQueue::ItemState::ABOUT_TO_ACTIVATE,
        PlayQueue::ItemState::ACTIVE_HALF_PLAYING,
        PlayQueue::ItemState::ACTIVE_NOW_PLAYING,
        PlayQueue::ItemState::ABOUT_TO_PHASE_OUT,
        PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED,
    };

    static_assert(states.size() == size_t(PlayQueue::ItemState::LAST_VALUE),
                  "Array size mismatch");

    for(int i = 0; i < 2; ++i)
    {
        for(const auto &s : states)
        {
            it.set_state(s);
            CHECK(it.get_state() == s);
        }
    }
}

/*!\test
 * Stream data in item can be retrieved.
 */
TEST_CASE_FIXTURE(Fixture, "Retrieve stream data")
{
    PlayQueue::Item it(123, default_key(), default_url(), "", true, "", {}, nullptr,
                       std::chrono::time_point<std::chrono::nanoseconds>::min(),
                       std::chrono::time_point<std::chrono::nanoseconds>::max());

    PlayQueue::StreamData &data(it.get_stream_data());

    CHECK(data.get_tag_list() == nullptr);
    CHECK(static_cast<void *>(&data.get_image_sent_data(false)) != static_cast<void *>(&data.get_image_sent_data(true)));
}

/*!\test
 * If defined, the URL FIFO item data fail operation is used when failing an
 * item.
 */
TEST_CASE_FIXTURE(Fixture, "Item can be set to failed state")
{
    PlayQueue::Item it(50, default_key(), default_url(), "", true, "", {}, nullptr,
                       std::chrono::time_point<std::chrono::nanoseconds>::min(),
                       std::chrono::time_point<std::chrono::nanoseconds>::max());

    CHECK(it.fail());
}

/*!\test
 * Multiple errors for the same stream are blocked.
 */
TEST_CASE_FIXTURE(Fixture, "Cannot enter failed state twice")
{
    PlayQueue::Item it(40, default_key(), default_url(), "", true, "", {}, nullptr,
                       std::chrono::time_point<std::chrono::nanoseconds>::min(),
                       std::chrono::time_point<std::chrono::nanoseconds>::max());

    CHECK(it.fail());

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_NOTICE,
        "Detected multiple failures for stream ID 40, reporting only the first one",
        false);

    CHECK_FALSE(it.fail());
}

TEST_SUITE_END();

/*!@}*/
