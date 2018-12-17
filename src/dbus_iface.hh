/*
 * Copyright (C) 2015, 2018  T+A elektroakustik GmbH & Co. KG
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

#ifndef DBUS_IFACE_HH
#define DBUS_IFACE_HH

#include <glib.h>

#include "urlfifo.hh"
#include "playitem.hh"

int dbus_setup(GMainLoop *loop, bool connect_to_session_bus,
               PlayQueue::Queue<PlayQueue::Item> &url_fifo);
void dbus_shutdown(GMainLoop *loop);

#endif /* !DBUS_IFACE_HH */
