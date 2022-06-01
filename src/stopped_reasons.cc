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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include "stopped_reasons.hh"
#include "messages.h"
#include "gerrorwrapper.hh"
#include "gstringwrapper.hh"

#include <gst/gst.h>

const char *StoppedReasons::as_string(Reason reason)
{
    /*!
     * String IDs that can be used as a reason as to why the stream was
     * stopped.
     *
     * Must be sorted according to values in #StoppedReasons::Reason
     * enumeration.
     */
    static const char *reasons[] =
    {
        "flow.unknown",
        "flow.nourl",
        "flow.stopped",
        "io.media",
        "io.net",
        "io.nourl",
        "io.protocol",
        "io.auth",
        "io.unavailable",
        "io.type",
        "io.denied",
        "data.codec",
        "data.format",
        "data.broken",
        "data.encrypted",
        "data.nodecrypter",
    };

    static_assert(G_N_ELEMENTS(reasons) == size_t(Reason::LAST_VALUE) + 1U,
                  "Array size mismatch");

    return reasons[size_t(reason)];
}

static StoppedReasons::Reason core_error_to_reason(GstCoreError code,
                                                   bool is_local_error)
{
    switch(code)
    {
      case GST_CORE_ERROR_MISSING_PLUGIN:
      case GST_CORE_ERROR_DISABLED:
        return StoppedReasons::Reason::MISSING_CODEC;

      case GST_CORE_ERROR_FAILED:
      case GST_CORE_ERROR_TOO_LAZY:
      case GST_CORE_ERROR_NOT_IMPLEMENTED:
      case GST_CORE_ERROR_STATE_CHANGE:
      case GST_CORE_ERROR_PAD:
      case GST_CORE_ERROR_THREAD:
      case GST_CORE_ERROR_NEGOTIATION:
      case GST_CORE_ERROR_EVENT:
      case GST_CORE_ERROR_SEEK:
      case GST_CORE_ERROR_CAPS:
      case GST_CORE_ERROR_TAG:
      case GST_CORE_ERROR_CLOCK:
      case GST_CORE_ERROR_NUM_ERRORS:
        break;
    }

    BUG("Failed to convert GstCoreError code %d to reason code", code);

    return StoppedReasons::Reason::UNKNOWN;
}

static StoppedReasons::Reason library_error_to_reason(GstLibraryError code,
                                                      bool is_local_error)
{
    BUG("Failed to convert GstLibraryError code %d to reason code", code);
    return StoppedReasons::Reason::UNKNOWN;
}

static StoppedReasons::Reason resource_error_to_reason(GstResourceError code,
                                                       bool is_local_error)
{
    switch(code)
    {
      case GST_RESOURCE_ERROR_NOT_FOUND:
        return StoppedReasons::Reason::DOES_NOT_EXIST;

      case GST_RESOURCE_ERROR_OPEN_READ:
        return is_local_error
            ? StoppedReasons::Reason::PHYSICAL_MEDIA_IO
            : StoppedReasons::Reason::NET_IO;

      case GST_RESOURCE_ERROR_READ:
      case GST_RESOURCE_ERROR_SEEK:
        return StoppedReasons::Reason::PROTOCOL;

      case GST_RESOURCE_ERROR_NOT_AUTHORIZED:
        return StoppedReasons::Reason::PERMISSION_DENIED;

      case GST_RESOURCE_ERROR_FAILED:
      case GST_RESOURCE_ERROR_TOO_LAZY:
      case GST_RESOURCE_ERROR_BUSY:
      case GST_RESOURCE_ERROR_OPEN_WRITE:
      case GST_RESOURCE_ERROR_OPEN_READ_WRITE:
      case GST_RESOURCE_ERROR_CLOSE:
      case GST_RESOURCE_ERROR_WRITE:
      case GST_RESOURCE_ERROR_SYNC:
      case GST_RESOURCE_ERROR_SETTINGS:
      case GST_RESOURCE_ERROR_NO_SPACE_LEFT:
      case GST_RESOURCE_ERROR_NUM_ERRORS:
        break;
    }

    BUG("Failed to convert GstResourceError code %d to reason code", code);

    return StoppedReasons::Reason::UNKNOWN;
}

static StoppedReasons::Reason stream_error_to_reason(GstStreamError code,
                                                     bool is_local_error)
{
    switch(code)
    {
      case GST_STREAM_ERROR_FAILED:
      case GST_STREAM_ERROR_TYPE_NOT_FOUND:
      case GST_STREAM_ERROR_WRONG_TYPE:
        return StoppedReasons::Reason::WRONG_TYPE;

      case GST_STREAM_ERROR_CODEC_NOT_FOUND:
        return StoppedReasons::Reason::MISSING_CODEC;

      case GST_STREAM_ERROR_DECODE:
      case GST_STREAM_ERROR_DEMUX:
        return StoppedReasons::Reason::BROKEN_STREAM;

      case GST_STREAM_ERROR_FORMAT:
        return StoppedReasons::Reason::WRONG_STREAM_FORMAT;

      case GST_STREAM_ERROR_DECRYPT:
        return StoppedReasons::Reason::DECRYPTION_NOT_SUPPORTED;

      case GST_STREAM_ERROR_DECRYPT_NOKEY:
        return StoppedReasons::Reason::ENCRYPTED;

      case GST_STREAM_ERROR_TOO_LAZY:
      case GST_STREAM_ERROR_NOT_IMPLEMENTED:
      case GST_STREAM_ERROR_ENCODE:
      case GST_STREAM_ERROR_MUX:
      case GST_STREAM_ERROR_NUM_ERRORS:
        break;
    }

    BUG("Failed to convert GstStreamError code %d to reason code", code);

    return StoppedReasons::Reason::UNKNOWN;
}

StoppedReasons::Reason StoppedReasons::from_gerror(const GErrorWrapper &error,
                                                   bool is_local_error)
{
    if(error->domain == GST_CORE_ERROR)
        return core_error_to_reason((GstCoreError)error->code,
                                    is_local_error);

    if(error->domain == GST_LIBRARY_ERROR)
        return library_error_to_reason((GstLibraryError)error->code,
                                       is_local_error);

    if(error->domain == GST_RESOURCE_ERROR)
        return resource_error_to_reason((GstResourceError)error->code,
                                        is_local_error);

    if(error->domain == GST_STREAM_ERROR)
        return stream_error_to_reason((GstStreamError)error->code,
                                      is_local_error);

    BUG("Unknown error domain %u for error code %d",
        error->domain, error->code);

    return Reason::UNKNOWN;
}

static bool is_local_error_by_url(const char *url)
{
#if GST_CHECK_VERSION(1, 5, 1)
    GstUri *uri = gst_uri_from_string(url);

    if(uri == nullptr)
        return true;

    static const std::string file_scheme("file");

    const char *scheme = gst_uri_get_scheme(uri);
    const bool retval = (scheme == nullptr || scheme == file_scheme);

    gst_uri_unref(uri);

    return retval;
#else /* pre 1.5.1 */
    static const char protocol_prefix[] = "file://";

    return strncmp(url, protocol_prefix, sizeof(protocol_prefix) - 1) == 0;
#endif /* use GstUri if not older than v1.5.1 */
}

bool StoppedReasons::determine_is_local_error_by_url(const GLibString &url)
{
    return url.empty() || is_local_error_by_url(url.get());
}

bool StoppedReasons::determine_is_local_error_by_url(const std::string &url)
{
    return url.empty() || is_local_error_by_url(url.c_str());
}
