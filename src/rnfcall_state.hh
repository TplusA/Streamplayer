/*
 * Copyright (C) 2022, 2023  T+A elektroakustik GmbH & Co. KG
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

#ifndef RNFCALL_STATE_HH
#define RNFCALL_STATE_HH

namespace DBusRNF
{

/*!
 * The states of a #DBusRNF::CallBase object.
 */
enum class CallState
{
    /*! Object was just constructed and is not in use. */
    INITIALIZED,

    /*! Request was sent, waiting for notification of result availability. */
    WAIT_FOR_NOTIFICATION,

    /*! Notification about result availability was received. */
    READY_TO_FETCH,

    /*! Result is stored in the object (received by fetch or via fast path). */
    RESULT_FETCHED,

    /*! Initiate abortion. */
    ABORTING,

    /*! Request aborted by peer, result not available and cannot be fetched. */
    ABORTED_BY_PEER,

    /*! Hard failure, result not available. */
    FAILED,

    /*! Object is about to be destroyed. */
    ABOUT_TO_DESTROY,

    /*! Sentinel. */
    LAST_VALUE = ABOUT_TO_DESTROY,
};

/*!
 * Whether or not the state indicates that the requested data are available.
 */
static inline bool state_is_success(CallState state)
{
    switch(state)
    {
      case CallState::RESULT_FETCHED:
        return true;

      case CallState::INITIALIZED:
      case CallState::WAIT_FOR_NOTIFICATION:
      case CallState::READY_TO_FETCH:
      case CallState::ABORTING:
      case CallState::ABORTED_BY_PEER:
      case CallState::FAILED:
      case CallState::ABOUT_TO_DESTROY:
        break;
    }

    return false;
}

/*!
 * Whether or not the state indicates a definite failure.
 */
static inline bool state_is_failure(CallState state)
{
    switch(state)
    {
      case CallState::ABORTED_BY_PEER:
      case CallState::FAILED:
        return true;

      case CallState::INITIALIZED:
      case CallState::WAIT_FOR_NOTIFICATION:
      case CallState::ABORTING:
      case CallState::READY_TO_FETCH:
      case CallState::RESULT_FETCHED:
      case CallState::ABOUT_TO_DESTROY:
        break;
    }

    return false;
}

}

#endif /* !RNFCALL_STATE_HH */
