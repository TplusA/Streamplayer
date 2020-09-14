/*
 * Copyright (C) 2018, 2020  T+A elektroakustik GmbH & Co. KG
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
    switch(state)
    {
      case ItemState::IN_QUEUE:
        return "IN_QUEUE";

      case ItemState::ABOUT_TO_ACTIVATE:
        return "ABOUT_TO_ACTIVATE";

      case ItemState::ACTIVE_HALF_PLAYING:
        return "ACTIVE_HALF_PLAYING";

      case ItemState::ACTIVE_NOW_PLAYING:
        return "ACTIVE_NOW_PLAYING";

      case ItemState::ABOUT_TO_PHASE_OUT:
        return "ABOUT_TO_PHASE_OUT";

      case ItemState::ABOUT_TO_BE_SKIPPED:
        return "ABOUT_TO_BE_SKIPPED";
    }

    return "*** UNKNOWN ItemState ***";
}

const char *PlayQueue::fail_state_name(PlayQueue::FailState state)
{
    switch(state)
    {
      case PlayQueue::FailState::NOT_FAILED:
        return "NOT_FAILED";

      case PlayQueue::FailState::FAILURE_DETECTED:
        return "FAILURE_DETECTED";
    }

    return "*** UNKNOWN FailState ***";
}
