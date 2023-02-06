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

#ifndef STOPPED_REASONS_HH
#define STOPPED_REASONS_HH

#include <string>

class GLibString;
class GErrorWrapper;

namespace StoppedReasons
{

enum class Reason
{
    /*! Reason not known. Should be used very rarely, if ever. */
    UNKNOWN,

    /*! Cannot play because URL FIFO is empty. */
    QUEUE_EMPTY,

    /*! Cannot stop because the player is already stopped. */
    ALREADY_STOPPED,

    /*! I/O error on physical medium (e.g., read error on some USB drive). */
    PHYSICAL_MEDIA_IO,

    /*! I/O error on the network (e.g., broken network connection). */
    NET_IO,

    /*! Have no URL. */
    URL_MISSING,

    /*! Network protocol error. */
    PROTOCOL,

    /*! Authentication with some external system has failed. */
    AUTHENTICATION,

    /*! Resource does not exist. */
    DOES_NOT_EXIST,

    /*! Resource has wrong type. */
    WRONG_TYPE,

    /*! Cannot access resource due to restricted permissions. */
    PERMISSION_DENIED,

    /*! Failed decoding stream because of a missing codec. */
    MISSING_CODEC,

    /*! Stream codec is known, but format wrong. */
    WRONG_STREAM_FORMAT,

    /*! Decoding failed. */
    BROKEN_STREAM,

    /*! Decryption key missing. */
    ENCRYPTED,

    /*! Cannot decrypt because this is not implemented/supported. */
    DECRYPTION_NOT_SUPPORTED,

    /*! Stable name for the highest-valued code. */
    LAST_VALUE = DECRYPTION_NOT_SUPPORTED,
};

const char *as_string(Reason reason);
Reason from_gerror(const GErrorWrapper &error, bool is_local_error);

}

#endif /* !STOPPED_REASONS_HH */
