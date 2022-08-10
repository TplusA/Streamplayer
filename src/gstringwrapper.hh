/*
 * Copyright (C) 2021, 2022  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of the T+A Streaming Board software stack ("StrBoWare").
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

#ifndef GSTRINGWRAPPER_HH
#define GSTRINGWRAPPER_HH

#include <glib.h>
#include <functional>
#include <cstring>

class GLibString
{
  private:
    gchar *str_;

  public:
    GLibString(const GLibString &) = delete;
    GLibString &operator=(const GLibString &) = delete;

    GLibString(GLibString &&src):
        str_(src.str_)
    {
        src.str_ = nullptr;
    }

    GLibString &operator=(GLibString &&src)
    {
        if(this != &src)
        {
            str_ = src.str_;
            src.str_ = nullptr;
        }

        return *this;
    }

    explicit GLibString(): str_(nullptr) {}
    explicit GLibString(gchar *&&str): str_(str) { str = nullptr; }
    explicit GLibString(const std::function<gchar *()> fn): str_(fn()) {}

    ~GLibString() { g_free(str_); }

    const gchar *get() const { return str_; }

    bool operator==(std::nullptr_t) const { return str_ == nullptr; }
    bool operator!=(std::nullptr_t) const { return str_ != nullptr; }
    bool empty() const { return str_ == nullptr || str_[0] == '\0'; }
    size_t size() const { return empty() ? 0 : strlen(str_); }
};

#endif /* !GSTRINGWRAPPER_HH */
