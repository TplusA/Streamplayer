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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include "strbo_url.hh"

#include <gst/gst.h>

#include <array>
#include <algorithm>
#include <cstring>

StreamType StrBoURL::determine_stream_type_from_url(const std::string &url)
{
    if(url.empty())
        return StreamType::EMPTY;

#if GST_CHECK_VERSION(1, 5, 1)
    GstUri *uri = gst_uri_from_string(url.c_str());

    if(uri == nullptr)
        return StreamType::UNKNOWN;

    static const std::array<const std::string, 2> local_file_schemes { "file", "strbo-usb" };
    const char *scheme = gst_uri_get_scheme(uri);
    StreamType retval;

    if(scheme == nullptr)
        retval = StreamType::UNKNOWN;
    else
    {
        retval = StreamType::GENERIC_NETWORK;
        if(std::any_of(local_file_schemes.begin(), local_file_schemes.end(),
                       [scheme] (const auto &it) { return it == scheme; }))
            retval = StreamType::LOCAL_FILE;
    }

    gst_uri_unref(uri);

    return retval;
#else /* pre 1.5.1 */
    static const std::array<const std::string, 2> protocol_prefixes { "file://", "strbo-usb:/" };

    if(std::any_of(protocol_prefixes.begin(), protocol_prefixes.end(),
                   [u = url.c_str()] (const auto &it) { return strncmp(u, it.c_str(), it.length()) == 0; }))
        return StreamType::LOCAL_FILE;

    return StreamType::GENERIC_NETWORK;
#endif /* use GstUri if not older than v1.5.1 */
}
