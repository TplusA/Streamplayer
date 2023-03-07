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

#include "cookie_manager_impl.hh"
#include "de_tahifi_airable.hh"

#include <unordered_map>

/*!
 * D-Bus data cookie management, one per list broker
 */
class PendingCookies
{
  public:
    using NotifyFnType = DBusRNF::CookieManagerIface::NotifyByCookieFn;
    using FetchFnType = DBusRNF::CookieManagerIface::FetchByCookieFn;

  private:
    LoggedLock::RecMutex lock_;
    std::unordered_map<uint32_t, NotifyFnType> notification_functions_;
    std::unordered_map<uint32_t, FetchFnType> fetch_functions_;

  public:
    PendingCookies(const PendingCookies &) = delete;
    PendingCookies(PendingCookies &&) = delete;
    PendingCookies &operator=(const PendingCookies &) = delete;
    PendingCookies &operator=(PendingCookies &&) = delete;

    explicit PendingCookies()
    {
        LoggedLock::configure(lock_, "PendingCookies", MESSAGE_LEVEL_DEBUG);
    }

    auto block_notifications()
    {
        LOGGED_LOCK_CONTEXT_HINT;
        return LoggedLock::UniqueLock<LoggedLock::RecMutex>(lock_);
    }

    /*!
     * Store a cookie along with a function for fetching the result.
     *
     * \param cookie
     *     A valid data cookie as returned by Airable list broker.
     *
     * \param notify_fn
     *     This function gets called when notification about the availability
     *     of the requested data associated with a cookie is received. It
     *     should take care of initiating the retrieval of the actual data from
     *     the list broker.
     *
     * \param fetch_fn
     *     This function implements the details of fetching the result from the
     *     list broker after it has notified us. Function \p fetch_fn is not
     *     called by this function, but stored for later invocation.
     *
     * \returns
     *     True if the cookie has been stored, false if the cookie was already
     *     stored (which probably indicates that there is a bug).
     */
    bool add(uint32_t cookie, NotifyFnType &&notify_fn, FetchFnType &&fetch_fn)
    {
        msg_log_assert(cookie != 0);
        msg_log_assert(fetch_fn != nullptr);

        LOGGED_LOCK_CONTEXT_HINT;
        std::lock_guard<LoggedLock::RecMutex> lock(lock_);
        notification_functions_.emplace(cookie, std::move(notify_fn));
        return fetch_functions_.emplace(cookie, std::move(fetch_fn)).second;
    }

    /*!
     * Notify blocking clients about availability of a result for a cookie.
     *
     * This function does not finish the cookie, it only notifies client code
     * which may block while waiting for the result to become available.
     *
     * \bug
     *     This should not be required. Client code should be rewritten to
     *     make use of purely asynchronous interfaces.
     */
    void available(uint32_t cookie)
    {
        available(cookie, ListError(), "availability");
    }

    void available(uint32_t cookie, const ListError &error)
    {
        available(cookie, error, "availability error");
    }

    /*!
     * Invoke fetch function indicating success, remove cookie.
     *
     * Typically, this function is called for each cookie reported by the
     * \c de.tahifi.Lists.Navigation.DataAvailable D-Bus signal.
     */
    void finish(uint32_t cookie)
    {
        finish(cookie, ListError(), "completion");
    }

    /*!
     * Invoke fetch function indicating failure, remove cookie.
     *
     * Typically, this function is called for each cookie reported by the
     * \c de.tahifi.Lists.Navigation.DataError D-Bus signal.
     *
     * \param cookie
     *     The cookie the fetch operation failed for.
     *
     * \param error
     *     The error reported by the list broker.
     */
    void finish(uint32_t cookie, ListError error)
    {
        finish(cookie, error, "completion error");
    }

  private:
    void available(uint32_t cookie, ListError error, const char *what)
    {
        LOGGED_LOCK_CONTEXT_HINT;
        LoggedLock::UniqueLock<LoggedLock::RecMutex> lock(lock_);

        const auto it(notification_functions_.find(cookie));

        if(it == notification_functions_.end())
        {
            MSG_BUG("No notification function for cookie %u (%s)", cookie, what);
            return;
        }

        const auto fn(std::move(it->second));
        notification_functions_.erase(it);

        LOGGED_LOCK_CONTEXT_HINT;
        lock.unlock();

        try
        {
            if(fn != nullptr)
                fn(cookie, error);
        }
        catch(const std::exception &e)
        {
            MSG_BUG("Got exception while notifying cookie %u (%s)", cookie, e.what());
        }
        catch(...)
        {
            MSG_BUG("Got exception while notifying cookie %u", cookie);
        }
    }

    void finish(uint32_t cookie, ListError error, const char *what)
    {
        LOGGED_LOCK_CONTEXT_HINT;
        LoggedLock::UniqueLock<LoggedLock::RecMutex> lock(lock_);

        const auto it(fetch_functions_.find(cookie));

        if(it == fetch_functions_.end())
        {
            MSG_BUG("Got %s notification for unknown cookie %u (finish)", what, cookie);
            return;
        }

        const auto fn(std::move(it->second));
        fetch_functions_.erase(it);

        LOGGED_LOCK_CONTEXT_HINT;
        lock.unlock();

        try
        {
            fn(cookie, error);
        }
        catch(const std::exception &e)
        {
            MSG_BUG("Got exception while fetching cookie %u (%s)", cookie, e.what());
        }
        catch(...)
        {
            MSG_BUG("Got exception while fetching cookie %u", cookie);
        }
    }
};

static PendingCookies pending_cookies_for_airable_proxy;

LoggedLock::UniqueLock<LoggedLock::RecMutex>
CookieManager::block_async_result_notifications(const void *proxy)
{
    return pending_cookies_for_airable_proxy.block_notifications();
}

bool CookieManager::set_pending_cookie(const void *proxy, uint32_t cookie,
                                       NotifyByCookieFn &&notify,
                                       FetchByCookieFn &&fetch)
{
    if(cookie == 0)
    {
        MSG_BUG("Attempted to store invalid cookie");
        return false;
    }

    if(notify == nullptr)
    {
        MSG_BUG("Notify function for cookie not given");
        return false;
    }

    if(fetch == nullptr)
    {
        MSG_BUG("Fetch function for cookie not given");
        return false;
    }

    if(!pending_cookies_for_airable_proxy.add(cookie, std::move(notify),
                                              std::move(fetch)))
    {
        MSG_BUG("Duplicate cookie %u", cookie);
        return false;
    }

    return true;
}

bool CookieManager::abort_cookie(const void *proxy, uint32_t cookie)
{
    if(cookie == 0)
    {
        MSG_BUG("Attempted to drop invalid cookie");
        return false;
    }

    GVariantBuilder b;
    g_variant_builder_init(&b, reinterpret_cast<const GVariantType *>("a(ub)"));
    g_variant_builder_add(&b, "(ub)", cookie, FALSE);

    return TDBus::get_singleton<tdbusAirable>()
                .call_sync<TDBus::AirableDataAbort>(g_variant_builder_end(&b));
}

namespace TDBus
{

void SignalHandlerTraits<AirableDataAvailable>::signal_handler(
                         IfaceType *const object, GVariant *arg_cookies,
                         Proxy<IfaceType> *const proxy)
{
    GVariantIter iter;
    g_variant_iter_init(&iter, arg_cookies);

    guint cookie;
    while(g_variant_iter_next(&iter, "u", &cookie))
    {
        pending_cookies_for_airable_proxy.available(cookie);

        try
        {
            pending_cookies_for_airable_proxy.finish(cookie);
        }
        catch(const std::exception &e)
        {
            MSG_BUG("Exception while finishing cookie %u: %s", cookie, e.what());
        }
    }
}

void SignalHandlerTraits<AirableDataError>::signal_handler(
                         IfaceType *const object, GVariant *arg_errors,
                         Proxy<IfaceType> *const proxy)
{
    GVariantIter iter;
    g_variant_iter_init(&iter, arg_errors);

    guint cookie;
    guchar raw_error_code;
    while(g_variant_iter_next(&iter, "(uy)", &cookie, &raw_error_code))
    {
        pending_cookies_for_airable_proxy.available(cookie, ListError(raw_error_code));

        try
        {
            pending_cookies_for_airable_proxy.finish(cookie, ListError(raw_error_code));
        }
        catch(const std::exception &e)
        {
            MSG_BUG("Exception while finishing faulty cookie %u, error %u: %s",
                    cookie, raw_error_code, e.what());
        }
    }
}

}
