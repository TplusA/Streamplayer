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

#include "stream_logging.hh"
#include "playitem.hh"
#include "gerrorwrapper.hh"
#include "gstringwrapper.hh"
#include "messages.h"

#include <gst/tag/tag.h>

static void log_next_stream_tags(const GstTagList *tag_list, const char prefix)
{
    if(tag_list == nullptr)
        return;

    const gchar *artist = nullptr;
    const gchar *album = nullptr;
    const gchar *title = nullptr;
    gst_tag_list_peek_string_index(tag_list, GST_TAG_ARTIST, 0, &artist);
    gst_tag_list_peek_string_index(tag_list, GST_TAG_ALBUM, 0, &album);
    gst_tag_list_peek_string_index(tag_list, GST_TAG_TITLE, 0, &title);
    msg_info("%c-Artist: \"%s\"", prefix, artist);
    msg_info("%c-Album : \"%s\"", prefix, album);
    msg_info("%c-Title : \"%s\"", prefix, title);
}

static std::tuple<std::string, const size_t, const size_t, bool>
tokenize_meta_data(const std::string &src)
{
    std::string dest;
    dest.reserve(src.length() + 1);

    size_t artist = src.length();
    size_t album = artist;
    size_t idx = 0;
    bool is_single_string = true;

    for(size_t i = 0; i < src.length(); ++i)
    {
        const char ch = src[i];

        if(ch == '\x1d')
        {
            is_single_string = false;
            dest.push_back('\0');

            if(idx < 2)
            {
                if(idx == 0)
                    artist = i + 1;
                else
                    album = i + 1;

                ++idx;
            }
        }
        else
            dest.push_back(ch);
    }

    dest.push_back('\0');

    return std::make_tuple(std::move(dest), artist, album, is_single_string);
}

void PlayQueue::log_next_stream(const PlayQueue::Item &next_stream)
{
    msg_info("Setting stream %u URL %s",
             next_stream.stream_id_, next_stream.get_url_for_playing().c_str());

    const auto &sd = next_stream.get_stream_data();

    log_next_stream_tags(sd.get_tag_list(), 'T');
    log_next_stream_tags(sd.get_preset_tag_list(), 'P');

    const auto &extra_tags(sd.get_extra_tags());
    const auto &drcpd_title(extra_tags.find("x-drcpd-title"));

    if(drcpd_title != extra_tags.end())
    {
        const auto &tokens(tokenize_meta_data(drcpd_title->second));
        const char *str(std::get<0>(tokens).c_str());

        if(!std::get<3>(tokens))
        {
            msg_info("R-Artist: \"%s\"", &str[std::get<1>(tokens)]);
            msg_info("R-Album : \"%s\"", &str[std::get<2>(tokens)]);
        }

        msg_info("R-Title : \"%s\"", str);
    }
}

static void log_error_or_warning(const char *prefix, const GLibString &debug,
                                 GErrorWrapper &error, const GstMessage *message)
{
    msg_error(0, LOG_ERR, "%s code %d, domain %s from \"%s\"",
              prefix, error->code, g_quark_to_string(error->domain),
              GST_MESSAGE_SRC_NAME(message));
    msg_error(0, LOG_ERR, "%s message: %s", prefix, error->message);
    msg_error(0, LOG_ERR, "%s debug: %s", prefix, debug.get());
    error.noticed();
}

#ifdef __DOXYGEN__
using GstMessage = struct _GstMessage;
#endif /* __DOXYGEN__ */

GErrorWrapper Streamer::log_error_message(GstMessage *message)
{
    GErrorWrapper error;
    const GLibString debug(
        [message, &error] ()
        {
            gchar *temp = nullptr;
            gst_message_parse_error(message, error.await(), &temp);
            return temp;
        });

    log_error_or_warning("ERROR", debug, error, message);
    return error;
}

GErrorWrapper Streamer::log_warning_message(GstMessage *message)
{
    GErrorWrapper error;
    const GLibString debug(
        [message, &error] ()
        {
            gchar *temp = nullptr;
            gst_message_parse_warning(message, error.await(), &temp);
            return temp;
        });

    log_error_or_warning("WARNING", debug, error, message);
    return error;
}
