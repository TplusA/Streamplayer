/*
 * Copyright (C) 2015  T+A elektroakustik GmbH & Co. KG
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

#ifndef STREAMTIME_H
#define STREAMTIME_H

#include <inttypes.h>

/*!
 * \addtogroup streamtime Stream time types
 *
 * Types for representing temporal positions in a stream.
 *
 * The primary type is #streamtime, a tagged union which contains the other
 * <code>streamtime_...</code> types defined in this file.
 */
/*!@{*/

/*!
 * Stream time in microseconds.
 *
 * Negative numbers mean microseconds from the end of the stream.
 */
struct streamtime_us
{
    int64_t microseconds;
};

/*!
 * Stream time in milliseconds.
 *
 * Negative numbers mean milliseconds from the end of the stream.
 */
struct streamtime_ms
{
    int64_t milliseconds;
};

/*!
 * Stream time in seconds.
 *
 * Negative numbers mean seconds from the end of the stream.
 */
struct streamtime_sec
{
    int64_t seconds;
};

/*!
 * Stream time in frames plus offset in microseconds.
 *
 * Negative frame number means a frame number from the end of the stream. The
 * microseconds offset is still relative to the beginning of that frame.
 */
struct streamtime_frame_us
{
    int32_t frame;
    uint32_t microseconds;
};

/*!
 * Type tag enumeration for tagged union #streamtime.
 */
enum streamtime_type
{
    STREAMTIME_TYPE_END_OF_STREAM,
    STREAMTIME_TYPE_MICROSECONDS,
    STREAMTIME_TYPE_MILLISECONDS,
    STREAMTIME_TYPE_SECONDS,
    STREAMTIME_TYPE_FRAME_NUMBER_PLUS_MICROSECONDS,
};

/*!
 * Stream time in some supported format.
 */
struct streamtime
{
    /*!
     * Type tag for choosing a field from #streamtime::d.
     */
    enum streamtime_type type;

    /*!
     * Data matching the specified #streamtime_type.
     *
     * For each #streamtime_type there is a field in #streamtime::d, except for
     * #STREAMTIME_TYPE_END_OF_STREAM. That type denotes either end of the
     * stream and does not require any additional positional information.
     */
    union
    {
        struct streamtime_us us;
        struct streamtime_ms ms;
        struct streamtime_sec sec;
        struct streamtime_frame_us frame_us;
    }
    d;
};

/*!@}*/

#endif /* !STREAMTIME_H */
