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

#ifndef STREAM_LOGGING_HH
#define STREAM_LOGGING_HH

struct _GstMessage;

class GErrorWrapper;

namespace PlayQueue
{

class Item;

void log_next_stream(const PlayQueue::Item &next_stream, const char *context);

}

namespace Streamer
{

GErrorWrapper log_error_message(struct _GstMessage *message);
GErrorWrapper log_warning_message(struct _GstMessage *message);

}

#endif /* !STREAM_LOGGING_HH */
