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
#include "messages.h"

#include <gst/gst.h>
#include <cstring>

StreamType StrBoURL::determine_stream_type_from_url(const std::string &url)
{
    if(url.empty())
        return StreamType::EMPTY;

#if GST_CHECK_VERSION(1, 5, 1)
    GstUri *uri = gst_uri_from_string(url.c_str());

    if(uri == nullptr)
        return StreamType::UNKNOWN;

    static const std::string file_scheme("file");

    const char *scheme = gst_uri_get_scheme(uri);
    const auto retval = (scheme == nullptr
                         ? StreamType::EMPTY
                         : (scheme == file_scheme
                            ? StreamType::LOCAL_FILE
                            : StreamType::GENERIC_NETWORK));

    gst_uri_unref(uri);

    return retval;
#else /* pre 1.5.1 */
    static const char protocol_prefix[] = "file://";

    return strncmp(url.c_str(), protocol_prefix, sizeof(protocol_prefix) - 1) == 0
        ? StreamType::LOCAL_FILE
        : StreamType::GENERIC_NETWORK;
#endif /* use GstUri if not older than v1.5.1 */
}
