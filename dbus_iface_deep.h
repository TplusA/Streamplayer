/*
 * Copyright (C) 2015, 2017  T+A elektroakustik GmbH & Co. KG
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

#ifndef DBUS_IFACE_DEEP_H
#define DBUS_IFACE_DEEP_H

#include "streamplayer_dbus.h"
#include "artcache_dbus.h"
#include "audiopath_dbus.h"

#ifdef __cplusplus
extern "C" {
#endif

tdbussplayURLFIFO *dbus_get_urlfifo_iface(void);
tdbussplayPlayback *dbus_get_playback_iface(void);

tdbusartcacheWrite *dbus_artcache_get_write_iface(void);

tdbusaupathManager *dbus_audiopath_get_manager_iface(void);

bool dbus_handle_error(GError **error);

#ifdef __cplusplus
}
#endif

#endif /* !DBUS_IFACE_DEEP_H */
