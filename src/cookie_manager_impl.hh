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

#ifndef COOKIE_MANAGER_IMPL_HH
#define COOKIE_MANAGER_IMPL_HH

#include "cookie_manager.hh"

class CookieManager: public DBusRNF::CookieManagerIface
{
  public:
    CookieManager(const CookieManager &) = delete;
    CookieManager(CookieManager &&) = default;
    CookieManager &operator=(const CookieManager &) = delete;
    CookieManager &operator=(CookieManager &&) = default;
    explicit CookieManager() = default;

    LoggedLock::UniqueLock<LoggedLock::RecMutex>
    block_async_result_notifications(const void *proxy) final override;
    bool set_pending_cookie(
            const void *proxy, uint32_t cookie,
            NotifyByCookieFn &&notify, FetchByCookieFn &&fetch) final override;
    bool abort_cookie(const void *proxy, uint32_t cookie) final override;
};

#endif /* !COOKIE_MANAGER_IMPL_HH */
