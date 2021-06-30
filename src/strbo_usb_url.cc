/*
 * Copyright (C) 2021  T+A elektroakustik GmbH & Co. KG
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

#include "strbo_usb_url.hh"
#include "messages.h"

#include <gst/gst.h>
#include <algorithm>
#include <cstring>

class Tempfile
{
  private:
    static constexpr char NAME_TEMPLATE[] = "/tmp/streamplayer_mounta.XXXXXX";

    char name_[sizeof(NAME_TEMPLATE)];
    int fd_;
    int errno_;
    bool suceeded_;

  public:
    Tempfile(const Tempfile &) = delete;
    Tempfile &operator=(const Tempfile &) = delete;

    explicit Tempfile(bool keep_open = false):
        errno_(0),
        suceeded_(true)
    {
        std::copy(NAME_TEMPLATE, NAME_TEMPLATE + sizeof(NAME_TEMPLATE), name_);

        fd_ = mkstemp(name_);

        if(fd_ < 0)
        {
            errno_ = errno;
            suceeded_ =  false;
            name_[0] = '\0';
            msg_error(errno_, LOG_ERR, "Failed creating temporary file");
        }
        else if(!keep_open)
        {
            os_file_close(fd_);
            fd_ = -1;
        }
    }

    ~Tempfile()
    {
        if(fd_ >= 0)
            os_file_close(fd_);

        if(name_[0] != '\0')
            os_file_delete(name_);
    }

    bool created() const { return suceeded_; }
    int error_code() const { return errno_; }

    const char *name() const { return name_; }
};

constexpr char Tempfile::NAME_TEMPLATE[];

static const char *beyond_fake_uuid_prefix(const char *uuid)
{
    const char fake_prefix[] = "DO-NOT-STORE:";

    return
        strncmp(uuid, fake_prefix, sizeof(fake_prefix) - 1) != 0
        ? nullptr
        : uuid + sizeof(fake_prefix) - 1;
}

static std::string no_mountpoint_found_error(const char *uuid)
{
    msg_error(ENOMEDIUM, LOG_NOTICE,
              "No mountpoint found for UUID '%s'", uuid);
    return std::string();
}

static std::string translate(const char *uuid, const GList *path_segments)
{
    Tempfile tempfile;

    if(!tempfile.created())
    {
        msg_error(ENOENT, LOG_NOTICE, "Failed writing tempfile");
        return std::string();
    }

    std::string device_name;
    const char *const fake_uuid_content = beyond_fake_uuid_prefix(uuid);

    if(fake_uuid_content != nullptr)
        std::transform(
            fake_uuid_content, fake_uuid_content + strlen(fake_uuid_content),
            std::back_inserter(device_name),
            [] (const char &ch) { return ch == '_' ? '/' : ch; });
    else
        device_name =
            std::string("$(/usr/bin/realpath /dev/disk/by-uuid/") + uuid + ")";

    const auto cmd = "/bin/grep \"" + device_name +
                     "\" /proc/mounts | cut -f 2 -d ' ' >" + tempfile.name();

    if(os_system_formatted(msg_is_verbose(MESSAGE_LEVEL_DEBUG), cmd.c_str()))
        return no_mountpoint_found_error(uuid);

    struct os_mapped_file_data output;
    if(os_map_file_to_memory(&output, tempfile.name()) < 0)
    {
        msg_error(ENOENT, LOG_NOTICE, "Failed reading tempfile");
        return std::string();
    }

    auto tempstring = output.length > 0
        ? std::string(static_cast<const char *>(output.ptr), output.length - 1)
        : std::string();
    os_unmap_file(&output);

    if(tempstring.empty())
        return no_mountpoint_found_error(uuid);

    for(const GList *it = path_segments; it != nullptr; it = it->next)
    {
        tempstring += '/';
        tempstring += static_cast<const char *>(it->data);
    }

    char *file_uri = gst_filename_to_uri(tempstring.c_str(), nullptr);

    if(file_uri == nullptr)
    {
        msg_error(ENOMEM, LOG_NOTICE,
                  "Failed creating file URI from StrBo link");
        return std::string();
    }

    tempstring = file_uri;
    g_free(file_uri);

    return tempstring;
}

std::string StrBo::translate_url_to_regular_url(const char *url, bool &failed)
{
    GstUri *uri = gst_uri_from_string(url);

    if(uri == nullptr)
    {
        failed = true;
        return std::string();
    }

    const char *scheme = gst_uri_get_scheme(uri);
    static const std::string strbo_scheme("strbo-usb");
    std::string result;

    if(scheme != nullptr && strbo_scheme == scheme)
    {
        GList *segs = gst_uri_get_path_segments(uri);
        if(segs != nullptr && segs->next != nullptr)
            result = translate(static_cast<const char *>(segs->next->data),
                               segs->next->next);
        g_list_free_full(segs, g_free);
        failed = result.empty();
    }
    else
        failed = false;

    gst_uri_unref(uri);
    return result;
}
