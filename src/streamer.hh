/*
 * Copyright (C) 2015--2020  T+A elektroakustik GmbH & Co. KG
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

#ifndef STREAMER_HH
#define STREAMER_HH

#include "urlfifo.hh"
#include "playitem.hh"

namespace Streamer
{

enum class PlayStatus
{
    STOPPED,
    PLAYING,
    PAUSED,
};

int setup(GMainLoop *loop, guint soup_http_block_size,
          PlayQueue::Queue<PlayQueue::Item> &url_fifo);
void shutdown(GMainLoop *loop);

void activate();
void deactivate();
bool start();
bool stop(const char *reason);
bool pause();
bool seek(int64_t position, const char *units);
bool fast_winding(double factor);
bool fast_winding_stop();
PlayStatus next(bool skip_only_if_not_stopped, uint32_t &out_skipped_id, uint32_t &out_next_id);
bool is_playing();
bool get_current_stream_id(stream_id_t &id);
bool push_item(stream_id_t stream_id, GVariantWrapper &&stream_key,
               const char *stream_url, size_t keep_items);
bool remove_items_for_root_path(const char *root_path);

}

#endif /* !STREAMER_HH */
