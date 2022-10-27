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

#ifndef DBUSLIST_EXCEPTION_HH
#define DBUSLIST_EXCEPTION_HH

#include <gio/gio.h>

#include "gerrorwrapper.hh"
#include "de_tahifi_lists_errors.hh"

/*!
 * \addtogroup dbus_list_exception Exception for reading lists from D-Bus
 * \ingroup dbus_list
 *
 * This exception is possibly thrown while reading lists from D-Bus.
 */
/*!@{*/

namespace List
{

class DBusListException
{
  public:
    enum class InternalDetail
    {
        UNKNOWN,
        UNEXPECTED_SUCCESS,     /* another flavor of "unknown" */
        DBUS_NO_REPLY,
    };

  private:
    const ListError error_;
    const InternalDetail detail_;

  public:
    DBusListException(const DBusListException &) = delete;
    DBusListException &operator=(const DBusListException &) = delete;
    DBusListException(DBusListException &&) = default;

    constexpr explicit DBusListException(ListError error) noexcept:
        error_(error),
        detail_(InternalDetail::UNKNOWN)
    {}

    constexpr explicit DBusListException(ListError::Code error) noexcept:
        error_(error),
        detail_(InternalDetail::UNKNOWN)
    {}

    constexpr explicit DBusListException(InternalDetail detail) noexcept:
        error_(ListError::Code::INTERNAL),
        detail_(detail)
    {}

    explicit DBusListException(const GErrorWrapper &error) noexcept:
        error_(ListError::Code::INTERNAL),
        detail_(error.failed()
                ? (error->domain == G_DBUS_ERROR && error->code == G_DBUS_ERROR_NO_REPLY
                   ? InternalDetail::DBUS_NO_REPLY
                   : InternalDetail::UNKNOWN)
                : InternalDetail::UNEXPECTED_SUCCESS)
    {}

    InternalDetail get_detail() const noexcept
    {
        return error_ == ListError::Code::INTERNAL
            ? detail_
            : InternalDetail::UNKNOWN;
    }

    const char *get_internal_detail_string_or_fallback(const char *fallback) const
    {
        if(error_ != ListError::Code::INTERNAL)
            return fallback;

        switch(detail_)
        {
          case InternalDetail::UNKNOWN:
            break;

          case InternalDetail::UNEXPECTED_SUCCESS:
            return "internal (blank GLib error struct)";

          case InternalDetail::DBUS_NO_REPLY:
            return "internal (D-Bus no reply, timeout)";
        }

        return "internal (unknown)";
    }

    ListError::Code get() const noexcept
    {
        return error_.get();
    }

    const char *what() const noexcept
    {
        return error_.to_string();
    }
};

}

/*!@}*/

#endif /* !DBUSLIST_EXCEPTION_HH */
