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

#ifndef URL_COLLECTION_HH
#define URL_COLLECTION_HH

#include "gvariantwrapper.hh"
#include "timebase.hh"

#include <vector>
#include <string>

namespace URLCollection
{

/*!
 * A single URL for a stream.
 */
class URL
{
  private:
    /*!
     * Direct link to stream.
     *
     * This string should be passed to GStreamer.
     */
    std::string direct_url_;

    /*!
     * Whatever the client has pushed.
     *
     * This string may include a custom protocol specification and should be
     * used when writing to log. It may also be an Airable link.
     */
    std::string original_url_string_;

    /*!
     * Whether or not #URLCollection::URL::original_url_string_ is an Airable
     * link.
     *
     * If this is \c true, then #URLCollection::URL::direct_url_ is initially
     * empty and needs to be retrieved from Airable using the
     * #URLCollection::URL::original_url_string_ link.
     *
     * If this is \c false, then #URLCollection::URL::direct_url_ is initially
     * set in case #URLCollection::URL::original_url_string_ was translated
     * into a direct URL. It should thus be used for streaming. If no
     * translation took place, then #URLCollection::URL::original_url_string_
     * should be used for streaming.
     */
    bool is_airable_link_;

    /*!
     * When this URL is expected to expire, if ever.
     */
    Timebase::time_point when_expected_expiry_;

  public:
    URL(const URL &) = delete;
    URL(URL &&) = default;
    URL &operator=(const URL &) = delete;
    URL &operator=(URL &&) = default;

    explicit URL(std::string &&direct_url, std::string &&original_url_string):
        direct_url_(std::move(direct_url)),
        original_url_string_(std::move(original_url_string)),
        is_airable_link_(false),
        when_expected_expiry_(Timebase::time_point::max())
    {}

    explicit URL(std::string &&original_url_string):
        original_url_string_(std::move(original_url_string)),
        is_airable_link_(true),
        when_expected_expiry_(Timebase::time_point::min())
    {}

    /*!
     * Get stream URI. Will be empty for unresolved Airable links.
     */
    const std::string &get_stream_uri(bool &is_airable_link) const
    {
        is_airable_link = is_airable_link_;
        return is_airable_link_
            ? direct_url_
            : (direct_url_.empty() ? original_url_string_ : direct_url_);
    }

    /*!
     * Get original URI as sent by client. Use this for logging.
     */
    const std::string &get_original_url_string() const { return original_url_string_; }

    /*!
     * Set direct link retrieved from Airable.
     */
    void set_resolved_uri(std::string &&uri, std::chrono::seconds &&expected_valid,
                          const Timebase &timebase);

    /*!
     * Forget previously resolved, but now expired URI previously retrieved
     * from Airable URI.
     *
     * Returns \c true if the URL is really an expired URI. In case the URI has
     * not expired yet, or the Airable link has not been resolved yet, or the
     * URI is not even an Airable link, this function returns \c false.
     */
    bool clear_expired_uri(const Timebase &timebase);
};

/*!
 * A collection of alternative URLs for the same logical stream.
 */
class StreamURLs
{
  private:
    std::vector<URL> urls_;
    size_t selected_url_;

  public:
    StreamURLs(const StreamURLs &) = delete;
    StreamURLs(StreamURLs &&) = default;
    StreamURLs &operator=(const StreamURLs &) = delete;
    StreamURLs &operator=(StreamURLs &&) = default;

    explicit StreamURLs(GVariantWrapper &&urls);
    explicit StreamURLs(std::string &&direct_url);

    bool empty() const { return urls_.empty(); }
    size_t size() const { return urls_.size(); }
    const URL &operator[](size_t idx) const { return urls_[idx]; }

    bool select_next_index();
    bool select_url_index(size_t idx);
    size_t get_selected_url_index() const { return selected_url_; }
    const URL &get_selected() const { return urls_[selected_url_]; }
    URL &get_selected() { return urls_[selected_url_]; }
};

}

#endif /* !URL_COLLECTION_HH */
