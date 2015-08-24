/*
 * Copyright (C) 2015  T+A elektroakustik GmbH & Co. KG
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

int streamer_setup(GMainLoop *loop, const guint *soup_http_block_size);
void streamer_shutdown(GMainLoop *loop);

void streamer_start(void);
void streamer_stop(void);
void streamer_pause(void);
void streamer_next(void);

/*
 * Global structure that contains function pointers for operating on URL FIFO
 * item data.
 */
extern const struct urlfifo_item_data_ops streamer_urlfifo_item_data_ops;

#endif /* !STREAMER_H */
