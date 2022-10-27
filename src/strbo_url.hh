/*
 * Copyright (C) 2023  T+A elektroakustik GmbH & Co. KG
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

#ifndef STRBO_URL_HH
#define STRBO_URL_HH

#include "url_collection.hh"

enum StreamType
{
    EMPTY,
    UNKNOWN,
    LOCAL_FILE,
    GENERIC_NETWORK,
    LIVE_INTERNET_RADIO,
    CANNED_NETWORK_STREAM,
};

namespace StrBoURL
{

StreamType determine_stream_type_from_url(const URLCollection::URL &url);

}

#endif /* !STRBO_URL_HH */
