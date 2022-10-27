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

#ifndef RNFCALL_RESOLVE_AIRABLE_REDIRECT_HH
#define RNFCALL_RESOLVE_AIRABLE_REDIRECT_HH

#include "rnfcall_cookiecall.hh"
#include "dbuslist_exception.hh"
#include "de_tahifi_airable.hh"
#include "taddybus.hh"
#include "gstringwrapper.hh"

/* Declaration of specialization required before using it. */
namespace TDBus { template <> Proxy<tdbusAirable> &get_singleton(); }

namespace DBusRNF
{

class ResolveAirableRedirectResult
{
  public:
    const ListError error_;
    GLibString url_;
    std::chrono::seconds expected_valid_;

    ResolveAirableRedirectResult(ResolveAirableRedirectResult &&) = default;

    explicit ResolveAirableRedirectResult(ListError &&error,
                                          GLibString &&url,
                                          std::chrono::seconds &&expected_valid):
        error_(std::move(error)),
        url_(std::move(url)),
        expected_valid_(std::move(expected_valid))
    {}
};

class ResolveAirableRedirectCall final:
    public DBusRNF::CookieCall<ResolveAirableRedirectResult>
{
  public:
    const std::string redirect_url_;

    explicit ResolveAirableRedirectCall(CookieManagerIface &cm,
                                        std::string &&redirect_url,
                                        std::unique_ptr<ContextData> context_data,
                                        StatusWatcher &&status_watcher):
        CookieCall(cm, std::move(context_data), std::move(status_watcher)),
        redirect_url_(std::move(redirect_url))
    {}

    virtual ~ResolveAirableRedirectCall() final override
    {
        abort_request_on_destroy();
    }

  protected:
    const void *get_proxy_ptr() const final override { return nullptr; }

    uint32_t do_request(std::promise<ResultType> &result) final override
    {
        guint cookie;
        guchar error_code;
        gchar *temp;
        guint expected_valid;

        const bool dbus_ok = TDBus::get_singleton<tdbusAirable>()
            .call_sync<TDBus::AirableResolveRedirect>(redirect_url_.c_str(),
                                                      &cookie, &error_code,
                                                      &temp, &expected_valid);

        if(!dbus_ok)
        {
            msg_vinfo(MESSAGE_LEVEL_IMPORTANT,
                      "Failed resolving Airable redirect %s",
                      redirect_url_.c_str());
            list_error_ = ListError::Code::INTERNAL;
            throw List::DBusListException(ListError::INTERNAL);
        }

        GLibString uri(std::move(temp));

        if(cookie == 0)
            result.set_value(ResolveAirableRedirectResult(
                                        ListError(error_code), std::move(uri),
                                        std::chrono::seconds(expected_valid)));

        return cookie;
    }

    void do_fetch(uint32_t cookie, std::promise<ResultType> &result) final override
    {
        guchar error_code;
        gchar *uri;
        guint expected_valid;

        const bool dbus_ok = TDBus::get_singleton<tdbusAirable>()
            .call_sync<TDBus::AirableResolveRedirectByCookie>(cookie, &error_code,
                                                              &uri, &expected_valid);

        if(!dbus_ok)
        {
            msg_vinfo(MESSAGE_LEVEL_IMPORTANT,
                      "Failed resolving Airable redirect %s by cookie %u",
                      redirect_url_.c_str(), cookie);
            list_error_ = ListError::Code::INTERNAL;
            throw List::DBusListException(ListError::INTERNAL);
        }

        list_error_ = ListError(error_code);
        result.set_value(ResolveAirableRedirectResult(
                            ListError(list_error_), GLibString(std::move(uri)),
                            std::chrono::seconds(expected_valid)));
    }
};

}

#endif /* !RNFCALL_RESOLVE_AIRABLE_REDIRECT_HH */
