/*
 * Copyright (C) 2018, 2020, 2022  T+A elektroakustik GmbH & Co. KG
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

#include "playitem.hh"
#include "messages.h"
#include "dump_enum_value.hh"

bool PlayQueue::Item::prefail(StoppedReasons::Reason reason)
{
    msg_log_assert(reason != StoppedReasons::Reason::UNKNOWN);
    if(prefail_reason_ != StoppedReasons::Reason::UNKNOWN)
    {
        msg_error(0, LOG_NOTICE,
                  "Detected extra pre-failure %s for stream ID %u, "
                  "keeping the first one (%s)",
                  as_string(reason), stream_id_, as_string(prefail_reason_));
        return false;
    }

    prefail_reason_ = reason;
    return true;
}

bool PlayQueue::Item::has_prefailed() const
{
    return prefail_reason_ != StoppedReasons::Reason::UNKNOWN;
}

StoppedReasons::Reason PlayQueue::Item::get_prefail_reason() const
{
    return prefail_reason_;
}

bool PlayQueue::Item::fail()
{
    switch(fail_state_)
    {
      case FailState::NOT_FAILED:
        fail_state_ = FailState::FAILURE_DETECTED;
        return true;

      case FailState::FAILURE_DETECTED:
        msg_error(0, LOG_NOTICE,
                  "Detected multiple failures for stream ID %u, "
                  "reporting only the first one", stream_id_);
        break;
    }

    return false;
}

const char *PlayQueue::item_state_name(PlayQueue::ItemState state)
{
    static const std::array<const char *const, 6> names
    {
        "IN_QUEUE", "ABOUT_TO_ACTIVATE", "ACTIVE_HALF_PLAYING",
        "ACTIVE_NOW_PLAYING", "ABOUT_TO_PHASE_OUT", "ABOUT_TO_BE_SKIPPED",
    };
    return enum_to_string(names, state);
}

const char *PlayQueue::fail_state_name(PlayQueue::FailState state)
{
    static const std::array<const char *const, 2> names
    {
        "NOT_FAILED", "FAILURE_DETECTED",
    };
    return enum_to_string(names, state);
}
