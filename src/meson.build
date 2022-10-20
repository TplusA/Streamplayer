#
# Copyright (C) 2020, 2021, 2022  T+A elektroakustik GmbH & Co. KG
#
# This file is part of T+A Streamplayer.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA  02110-1301, USA.
#

config_h = declare_dependency(
    sources: configure_file(input: '../config.h.meson',
                            output: 'config.h',
                            configuration: config_data),
    include_directories: '.'
)

subdir('dbus')

urlfifo_lib = static_library('urlfifo',
    'playitem.cc',
    dependencies: [glib_deps, config_h],
)

stopped_reasons_lib = static_library('stopped_reasons',
    'stopped_reasons.cc',
    dependencies: [glib_deps, config_h],
)

gvariantwrapper_lib = static_library('gvariantwrapper',
    'gvariantwrapper.cc',
    dependencies: [glib_deps, config_h],
)

executable(
    'streamplayer',
    [
        'streamplayer.cc', 'streamer.cc', 'dbus.cc', 'debug_levels.cc',
        'backtrace.c', 'strbo_usb_url.cc',
        'messages.c', 'messages_glib.c', 'messages_dbus.c', 'os.c',
        version_info
    ],
    include_directories: include_directories('dbus'),
    dependencies: [
        dbus_deps,
        glib_deps,
        config_h,
    ],
    link_with: [
        urlfifo_lib,
        stopped_reasons_lib,
        gvariantwrapper_lib,
        taddybus_lib,
    ],
    install: true
)
