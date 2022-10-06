/*
 * Copyright (C) 2022  T+A elektroakustik GmbH & Co. KG
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

#ifndef BUFFERING_HH
#define BUFFERING_HH

#include "messages.h"

namespace Buffering
{

/*!
 * Buffering states and pause modes.
 *
 * When buffering, we must distinguish between pause mode for filling up the
 * buffer and pause mode requsted by the user. The first is a technicaly
 * necessity, the latter is the user's intention.
 *
 * Entering pause mode for buffering also requires a workaround for a bug in
 * \c playbin3. In case the pipeline is set to \c PLAYING and then reports 0%
 * buffer level, we must set it to \c PAUSED. Internally, \c playbin3 may have
 * to set the pipeline to \c PLAYING state in order to continue during preroll,
 * which interferes with our \c PAUSED request (which overrides \c PLAYING
 * state set internally by \c playbin3). At some point, we get a 100% buffer
 * level and set the pipeline back to \c PLAYING state. These state transitions
 * confuse \c playbin3 so that the pipeline does not continue and appears to
 * hang.
 *
 * The workaround consist in requesting \c PAUSED state at 0% buffer level,
 * then wait for the \c PAUSED state to be reached *and* having 100% buffer
 * level. Only if both conditions apply, switch back to \c PLAYING state if
 * this coincides with the user's original intention.
 */
enum class State
{
    NOT_BUFFERING,          //!< Not buffering
    PAUSED_PENDING,         //!< Paused for buffering, waiting for definite \c PAUSED state
    PAUSED_FOR_BUFFERING,   //!< In \c PAUSED state and actually buffering
    PAUSED_PIGGYBACK,       //!< Buffering while user has paused: keep paused after buffering
};

/*!
 * Buffer level in qualitative terms.
 */
enum class Level
{
    FULL,
    MEDIUM,
    UNDERRUN,
};

/*!
 * If an how the buffer level has changed, given a new fill level.
 */
enum class LevelChange
{
    NONE,
    FULL_DETECTED,
    UNDERRUN_DETECTED,
};

/*!
 * How to continue after having tried to leave buffering mode.
 */
enum class LeaveBufferingResult
{
    PLEASE_RESUME,
    PLEASE_KEEP_PAUSED,
    STILL_BUFFERING,
    NOT_BUFFERING,
};

/*!
 * Data for buffering states.
 */
class Data
{
  private:
    State state_;
    Level level_;

  public:
    Data(const Data &) = delete;
    Data(Data &&) = default;
    Data &operator=(const Data &) = delete;
    Data &operator=(Data &&) = default;

    explicit Data():
        state_(State::NOT_BUFFERING),
        level_(Level::FULL)
    {}

    bool is_buffering() const { return state_ != State::NOT_BUFFERING; }

    State get_state() const { return state_; }

    LevelChange set_buffer_level(unsigned int percent)
    {
        LevelChange result;

        if(percent >= 100)
        {
            result = level_ != Level::FULL ? LevelChange::FULL_DETECTED : LevelChange::NONE;
            level_ = Level::FULL;
        }
        else if(percent == 0)
        {
            result =
                level_ != Level::UNDERRUN ? LevelChange::UNDERRUN_DETECTED : LevelChange::NONE;
            level_ = Level::UNDERRUN;
        }
        else
        {
            result = LevelChange::NONE;
            level_ = Level::MEDIUM;
        }

        return result;
    }

    void start_buffering(bool need_wait_for_pause)
    {
        BUG_IF(state_ != State::NOT_BUFFERING,
               "Bad buffering state %d on start", int(state_));
        state_ = need_wait_for_pause ? State::PAUSED_PENDING : State::PAUSED_PIGGYBACK;
    }

    bool entered_pause()
    {
        switch(state_)
        {
          case State::NOT_BUFFERING:
            break;

          case State::PAUSED_PENDING:
            state_ = State::PAUSED_FOR_BUFFERING;
            return true;

          case State::PAUSED_FOR_BUFFERING:
          case State::PAUSED_PIGGYBACK:
            return true;
        }

        return false;
    }

    LeaveBufferingResult try_leave_buffering_state()
    {
        switch(state_)
        {
          case State::NOT_BUFFERING:
            return LeaveBufferingResult::NOT_BUFFERING;

          case State::PAUSED_PENDING:
            break;

          case State::PAUSED_FOR_BUFFERING:
          case State::PAUSED_PIGGYBACK:
            if(level_ == Level::FULL)
            {
                const auto result = state_ == State::PAUSED_FOR_BUFFERING
                    ? LeaveBufferingResult::PLEASE_RESUME
                    : LeaveBufferingResult::PLEASE_KEEP_PAUSED;
                state_ = State::NOT_BUFFERING;
                return result;
            }

            break;
        }

        return LeaveBufferingResult::STILL_BUFFERING;
    }

    void reset()
    {
        state_ = State::NOT_BUFFERING;
    }
};

}

#endif /* !BUFFERING_HH */
