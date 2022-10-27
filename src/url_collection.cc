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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <glib.h>

#include "url_collection.hh"
#include "strbo_usb_url.hh"
#include "messages.h"

void URLCollection::URL::set_resolved_uri(std::string &&uri,
                                          std::chrono::seconds &&expected_valid,
                                          const Timebase &timebase)
{
    if(is_airable_link_)
    {
        when_expected_expiry_ = timebase.now() + expected_valid;
        direct_url_ = std::move(uri);
    }
    else
        MSG_BUG("Tried to replace non-link URL with resolved URI");
}

bool URLCollection::URL::clear_expired_uri(const Timebase &timebase)
{
    if(!is_airable_link_)
        return false;

    if(direct_url_.empty())
        return false;

    if(timebase.now() < when_expected_expiry_)
        return false;

    direct_url_.clear();
    when_expected_expiry_ = Timebase::time_point::min();
    return true;
}

static void translate_and_append_url(std::vector<URLCollection::URL> &urls,
                                     std::string &&uri)
{
    bool failed;
    auto xlated_url(StrBoURL::USB::translate_url_to_regular_url(uri.c_str(), failed));

    if(failed)
        msg_error(0, LOG_ERR, "Failed to create regular URL from \"%s\"", uri.c_str());
    else
        urls.emplace_back(std::move(xlated_url), std::move(uri));
}

URLCollection::StreamURLs::StreamURLs(GVariantWrapper &&urls):
    selected_url_(0)
{
    urls_.reserve(g_variant_n_children(GVariantWrapper::get(urls)));

    GVariantIter iter;
    g_variant_iter_init(&iter, GVariantWrapper::get(urls));

    const gchar *uri = nullptr;
    gboolean is_airable_link;

    while(g_variant_iter_loop(&iter, "(sb)", &uri, &is_airable_link))
    {
        if(is_airable_link)
        {
            if(uri[0] == '\0')
                msg_error(0, LOG_ERR, "Received empty Airable link");
            else
                urls_.emplace_back(uri);
        }
        else
            translate_and_append_url(urls_, uri);
    }
}

URLCollection::StreamURLs::StreamURLs(std::string &&direct_url):
    selected_url_(0)
{
    translate_and_append_url(urls_, std::move(direct_url));
}

bool URLCollection::StreamURLs::select_next_index()
{
    if(selected_url_ + 1 < urls_.size())
    {
        ++selected_url_;
        return true;
    }
    else
        return false;
}

bool URLCollection::StreamURLs::select_url_index(size_t idx)
{
    if(idx < urls_.size())
    {
        selected_url_ = idx;
        return true;
    }

    MSG_BUG("Selected stream URL %zu is out of range (exceeds %zu)",
            idx, urls_.size() - 1);
    return false;
}
