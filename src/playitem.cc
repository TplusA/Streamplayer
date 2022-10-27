/*
 * Copyright (C) 2018, 2020, 2022  T+A elektroakustik GmbH & Co. KG
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

#include "playitem.hh"
#include "messages.h"
#include "dump_enum_value.hh"

bool PlayQueue::Item::prefail(StoppedReasons::Reason reason)
{
    msg_log_assert(reason != StoppedReasons::Reason::UNKNOWN);
    if(prefail_reason_ != StoppedReasons::Reason::UNKNOWN)
    {
        msg_error(0, LOG_NOTICE,
                  "Detected extra pre-failure %s for stream ID %u, "
                  "keeping the first one (%s)",
                  as_string(reason), stream_id_, as_string(prefail_reason_));
        return false;
    }

    prefail_reason_ = reason;
    return true;
}

bool PlayQueue::Item::has_prefailed() const
{
    return prefail_reason_ != StoppedReasons::Reason::UNKNOWN;
}

StoppedReasons::Reason PlayQueue::Item::get_prefail_reason() const
{
    return prefail_reason_;
}

bool PlayQueue::Item::fail()
{
    switch(fail_state_)
    {
      case FailState::NOT_FAILED:
        fail_state_ = FailState::FAILURE_DETECTED;
        return true;

      case FailState::FAILURE_DETECTED:
        msg_error(0, LOG_NOTICE,
                  "Detected multiple failures for stream ID %u, "
                  "reporting only the first one", stream_id_);
        break;
    }

    return false;
}

PlayQueue::URLState PlayQueue::Item::get_selected_url_state() const
{
    if(stream_urls_.empty())
        return PlayQueue::URLState::BROKEN;

    if(resolver_ != nullptr)
        return PlayQueue::URLState::RESOLVING_LINK;

    const auto &url(stream_urls_.get_selected());
    bool is_airable_link;

    if(url.get_stream_uri(is_airable_link).empty())
        return is_airable_link
            ? PlayQueue::URLState::KNOWN_AIRABLE_LINK
            : PlayQueue::URLState::BROKEN;
    else
        return is_airable_link
            ? PlayQueue::URLState::KNOWN_RESOLVED_URL
            : PlayQueue::URLState::KNOWN_DIRECT_URL;
}

bool PlayQueue::Item::select_next_url()
{
    auto &url(stream_urls_.get_selected());

    if(url.clear_expired_uri(timebase_))
    {
        /*
         * We have just cleared a non-empty, presumably expired URL that we
         * had previously received via an Airable link.
         *
         * The next URL is the URL retrieved through the Airable link that is
         * already selected (that is, we are trying to refresh the expired
         * URL).
         */
        return true;
    }

    /*
     * Not an Airable link, empty URL, or non-empty, probably still valid URL
     * previously retrieved via Airable link. In any case, the URL has not been
     * cleared, and we advance to the next URL if available.
     */
    return stream_urls_.select_next_index();
}

PlayQueue::Item::PipelineStartRequired
PlayQueue::Item::pipeline_start_required_when_ready()
{
    MSG_BUG_IF(!pipeline_start_required_after_resolve_,
               "Marked item %u for pipeline restart twice", stream_id_);
    pipeline_start_required_after_resolve_ = true;

    switch(get_selected_url_state())
    {
      case URLState::KNOWN_DIRECT_URL:
      case URLState::KNOWN_RESOLVED_URL:
        return PipelineStartRequired::START_NOW;

      case URLState::RESOLVING_LINK:
        return PipelineStartRequired::WHEN_READY;

      case URLState::KNOWN_AIRABLE_LINK:
      case URLState::BROKEN:
        break;
    }

    return PipelineStartRequired::NOT_REQUIRED;
}

void PlayQueue::Item::resolver_begin(DBusRNF::CookieManagerIface &cm,
                                     std::unique_ptr<DBusRNF::ContextData> context_data)
{
    if(resolver_ != nullptr)
    {
        msg_info("Interrupting active Airable resolver");
        resolver_->abort_request();
    }

    resolver_ =
        std::make_shared<DBusRNF::ResolveAirableRedirectCall>(
            cm,
            std::string(stream_urls_.get_selected().get_original_url_string()),
            std::move(context_data),
            [] (const auto &call, DBusRNF::CallState state, bool is_detached)
            {
            });
    auto last_ref = resolver_;
    resolver_->request();
}

PlayQueue::Item::ResolverResult
PlayQueue::Item::resolver_finish(std::string &&uri,
                                 std::chrono::seconds &&expected_valid)
{
    if(uri.empty())
        return resolver_finish(URLState::BROKEN);

    stream_urls_.get_selected().set_resolved_uri(std::move(uri),
                                                 std::move(expected_valid),
                                                 timebase_);
    resolver_ = nullptr;
    return ResolverResult::RESOLVED;
}

PlayQueue::Item::ResolverResult
PlayQueue::Item::resolver_finish(PlayQueue::URLState url_state)
{
    stream_urls_.get_selected().set_resolved_uri("", std::chrono::seconds(0),
                                                 timebase_);
    resolver_ = nullptr;
    return stream_urls_.get_selected_url_index() + 1 < stream_urls_.size()
        ? ResolverResult::HAVE_MORE_URLS
        : ResolverResult::FAILED;
}

const char *PlayQueue::item_state_name(PlayQueue::ItemState state)
{
    static const std::array<const char *const, 6> names
    {
        "IN_QUEUE", "ABOUT_TO_ACTIVATE", "ACTIVE_HALF_PLAYING",
        "ACTIVE_NOW_PLAYING", "ABOUT_TO_PHASE_OUT", "ABOUT_TO_BE_SKIPPED",
    };
    return enum_to_string(names, state);
}

const char *PlayQueue::fail_state_name(PlayQueue::FailState state)
{
    static const std::array<const char *const, 2> names
    {
        "NOT_FAILED", "FAILURE_DETECTED",
    };
    return enum_to_string(names, state);
}

const char *PlayQueue::url_state_name(PlayQueue::URLState state)
{
    static const std::array<const char *const, 5> names
    {
        "KNOWN_DIRECT_URL", "KNOWN_AIRABLE_LINK", "RESOLVING_LINK",
        "KNOWN_RESOLVED_URL", "BROKEN",
    };
    return enum_to_string(names, state);
}
