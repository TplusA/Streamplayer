/*
 * Copyright (C) 2018, 2020, 2021  T+A elektroakustik GmbH & Co. KG
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

#ifndef STREAMDATA_HH
#define STREAMDATA_HH

#include <gst/tag/tag.h>

#include "gvariantwrapper.hh"

namespace PlayQueue
{

struct ImageSentData
{
    uint8_t *data;
    size_t size;
    uint8_t priority;

    ImageSentData():
        data(nullptr),
        size(0),
        priority(0)
    {}

    void reset()
    {
        data = nullptr;
        size = 0;
        priority = 0;
    }
};

class StreamData
{
  private:
    GstTagList *tag_list_;
    GstTagList *preset_tag_list_;
    std::string cover_art_url_;
    ImageSentData big_image_;
    ImageSentData preview_image_;

  public:
    GVariantWrapper stream_key_;

    StreamData(const StreamData &) = delete;
    StreamData &operator=(const StreamData &) = delete;

    explicit StreamData(GstTagList *preset_tag_list,
                        std::string &&cover_art_url,
                        GVariantWrapper &&stream_key):
        tag_list_(nullptr),
        preset_tag_list_(preset_tag_list),
        cover_art_url_(std::move(cover_art_url)),
        stream_key_(std::move(stream_key))
    {}

    ~StreamData()
    {
        if(tag_list_ != nullptr)
            gst_tag_list_unref(tag_list_);

        if(preset_tag_list_ != nullptr)
            gst_tag_list_unref(preset_tag_list_);
    }

    void clear_meta_data()
    {
        if(tag_list_ != nullptr)
            gst_tag_list_unref(tag_list_);

        if(preset_tag_list_ != nullptr)
            tag_list_ = gst_tag_list_copy(preset_tag_list_);
        else
            tag_list_ = gst_tag_list_new_empty();

        big_image_.reset();
        preview_image_.reset();
    }

    const std::string &get_cover_art_url() const { return cover_art_url_; }

    void merge_tag_list(GstTagList *tags)
    {
        if(tags == nullptr)
            return;

        if(tag_list_ != nullptr)
        {
            GstTagList *merged =
                gst_tag_list_merge(tag_list_, tags, GST_TAG_MERGE_PREPEND);
            gst_tag_list_unref(tag_list_);
            tag_list_ = merged;
        }
        else
        {
            gst_tag_list_ref(tags);
            tag_list_ = tags;
        }
    }

    const GstTagList *get_tag_list() const { return tag_list_; }

    ImageSentData &get_image_sent_data(bool is_big_image)
    {
        return is_big_image ? big_image_ : preview_image_;
    }
};

}

#endif /* !STREAMDATA_HH */
