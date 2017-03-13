/*
 * Copyright (C) 2015, 2016, 2017  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of T+A Streamplayer.
 *
 * T+A Streamplayer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3 as
 * published by the Free Software Foundation.
 *
 * T+A Streamplayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with T+A Streamplayer.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef STREAMER_H
#define STREAMER_H

#include <stdbool.h>
#include <inttypes.h>

enum PlayStatus
{
    PLAY_STATUS_STOPPED,
    PLAY_STATUS_PLAYING,
    PLAY_STATUS_PAUSED,
};

int streamer_setup(GMainLoop *loop, guint soup_http_block_size);
void streamer_shutdown(GMainLoop *loop);

void streamer_activate(void);
void streamer_deactivate(void);
bool streamer_start(void);
bool streamer_stop(void);
bool streamer_pause(void);
bool streamer_seek(guint64 position, const char *units);
enum PlayStatus streamer_next(bool skip_only_if_not_stopped,
                              uint32_t *out_skipped_id, uint32_t *out_next_id);
bool streamer_is_playing(void);
bool streamer_get_current_stream_id(uint16_t *id);
bool streamer_push_item(uint16_t stream_id, GVariant *stream_key,
                        const char *stream_url, size_t keep_items);

#endif /* !STREAMER_H */
