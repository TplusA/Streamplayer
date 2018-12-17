/*
 * Copyright (C) 2018  T+A elektroakustik GmbH & Co. KG
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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cppcutter.h>

#include "playitem.hh"

#include "mock_messages.hh"

/*!
 * \addtogroup playitem_tests Unit tests
 * \ingroup urlfifo
 *
 * Play item tests (items stored inside a URL FIFO).
 */
/*!@{*/

namespace play_item_tests
{

static MockMessages *mock_messages;

void cut_setup()
{
    mock_messages = new MockMessages;
    cppcut_assert_not_null(mock_messages);
    mock_messages->init();
    mock_messages_singleton = mock_messages;
}

void cut_teardown()
{
    mock_messages->check();
    mock_messages_singleton = nullptr;
    delete mock_messages;
    mock_messages = nullptr;
}

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
void test_set_state_does_not_consider_current_state()
{
    PlayQueue::Item it(25, default_key(), default_url(),
                       std::chrono::time_point<std::chrono::nanoseconds>::min(),
                       std::chrono::time_point<std::chrono::nanoseconds>::max());

    cppcut_assert_equal(int(PlayQueue::ItemState::IN_QUEUE), int(it.get_state()));

    static const std::array<PlayQueue::ItemState, 4> states
    {
        PlayQueue::ItemState::ABOUT_TO_ACTIVATE,
        PlayQueue::ItemState::ACTIVE,
        PlayQueue::ItemState::ABOUT_TO_PHASE_OUT,
        PlayQueue::ItemState::ABOUT_TO_BE_SKIPPED,
    };

    static_assert(states.size() == size_t(PlayQueue::ItemState::LAST_ITEM_STATE),
                  "Array size mismatch");

    for(int i = 0; i < 2; ++i)
    {
        for(const auto &s : states)
        {
            it.set_state(s);
            cppcut_assert_equal(int(s), int(it.get_state()));
        }
    }
}

/*!\test
 * Stream data in item can be retrieved.
 */
void test_retrieve_stream_data()
{
    PlayQueue::Item it(123, default_key(), default_url(),
                       std::chrono::time_point<std::chrono::nanoseconds>::min(),
                       std::chrono::time_point<std::chrono::nanoseconds>::max());

    PlayQueue::StreamData &data(it.get_stream_data());

    cppcut_assert_null(data.get_tag_list());
    cppcut_assert_not_equal(static_cast<void *>(&data.get_image_sent_data(false)),
                            static_cast<void *>(&data.get_image_sent_data(true)));
}

/*!\test
 * If defined, the URL FIFO item data fail operation is used when failing an
 * item.
 */
void test_item_can_be_set_to_failed_state()
{
    PlayQueue::Item it(50, default_key(), default_url(),
                       std::chrono::time_point<std::chrono::nanoseconds>::min(),
                       std::chrono::time_point<std::chrono::nanoseconds>::max());

    cut_assert_true(it.fail());
}

/*!\test
 * Multiple errors for the same stream are blocked.
 */
void test_cannot_enter_failed_state_twice()
{
    PlayQueue::Item it(40, default_key(), default_url(),
                       std::chrono::time_point<std::chrono::nanoseconds>::min(),
                       std::chrono::time_point<std::chrono::nanoseconds>::max());

    cut_assert_true(it.fail());

    mock_messages->expect_msg_error_formatted(0, LOG_NOTICE,
        "Detected multiple failures for stream ID 40, reporting only the first one");

    cut_assert_false(it.fail());
}

}

/*!@}*/
