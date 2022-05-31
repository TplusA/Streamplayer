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
    include_directories: '.')

gnome = import('gnome')

dbus_docs = []

dbus_iface_defs_includes = include_directories('../dbus_interfaces')

streamplayer_dbus_src = gnome.gdbus_codegen('de_tahifi_streamplayer',
    sources: '../dbus_interfaces/de_tahifi_streamplayer.xml',
    interface_prefix: 'de.tahifi.Streamplayer.',
    namespace: 'tdbus_splay')
streamplayer_dbus_lib = static_library('streamplayer_dbus',
    streamplayer_dbus_src, dependencies: glib_deps, c_args: relaxed_dbus_warnings)
streamplayer_dbus_dep = declare_dependency(link_with: streamplayer_dbus_lib)
dbus_docs += custom_target('de_tahifi_streamplayer_docs',
    input: '../dbus_interfaces/de_tahifi_streamplayer.xml',
    output: ['@BASENAME@-doc.md', '@BASENAME@-doc.h'],
    command: [
        extract_docs, '-i', '@INPUT@', '-o', '@OUTPUT0@', '-H', '@OUTPUT1@',
        '-c', 'tdbus_splay', '-s', 'de.tahifi.Streamplayer.', '-n', 'Stream Player'
    ])

artcache_dbus_src = gnome.gdbus_codegen('de_tahifi_artcache',
    sources: '../dbus_interfaces/de_tahifi_artcache.xml',
    interface_prefix: 'de.tahifi.ArtCache.',
    namespace: 'tdbus_artcache')
artcache_dbus_lib = static_library('artcache_dbus',
    artcache_dbus_src, dependencies: glib_deps, c_args: relaxed_dbus_warnings)
artcache_dbus_dep = declare_dependency(link_with: artcache_dbus_lib)
dbus_docs += custom_target('de_tahifi_artcache_docs',
    input: '../dbus_interfaces/de_tahifi_artcache.xml',
    output: ['@BASENAME@-doc.md', '@BASENAME@-doc.h'],
    command: [
        extract_docs, '-i', '@INPUT@', '-o', '@OUTPUT0@', '-H', '@OUTPUT1@',
        '-c', 'tdbus_artcache', '-s', 'de.tahifi.ArtCache.', '-n', 'Cover Art'
    ])

audiopath_dbus_src = gnome.gdbus_codegen('de_tahifi_audiopath',
    sources: '../dbus_interfaces/de_tahifi_audiopath.xml',
    interface_prefix: 'de.tahifi.AudioPath.',
    namespace: 'tdbus_aupath')
audiopath_dbus_lib = static_library('audiopath_dbus',
    audiopath_dbus_src, dependencies: glib_deps, c_args: relaxed_dbus_warnings)
audiopath_dbus_dep = declare_dependency(link_with: audiopath_dbus_lib)
dbus_docs += custom_target('de_tahifi_audiopath_docs',
    input: '../dbus_interfaces/de_tahifi_audiopath.xml',
    output: ['@BASENAME@-doc.md', '@BASENAME@-doc.h'],
    command: [
        extract_docs, '-i', '@INPUT@', '-o', '@OUTPUT0@', '-H', '@OUTPUT1@',
        '-c', 'tdbus_aupath', '-s', 'de.tahifi.AudioPath.', '-n', 'Audio Paths'
    ])

mounta_dbus_src = gnome.gdbus_codegen('de_tahifi_mounta',
    sources: '../dbus_interfaces/de_tahifi_mounta.xml',
    interface_prefix: 'de.tahifi.',
    namespace: 'tdbus')
mounta_dbus_lib = static_library('mounta_dbus',
    mounta_dbus_src, dependencies: glib_deps, c_args: relaxed_dbus_warnings)
mounta_dbus_dep = declare_dependency(link_with: mounta_dbus_lib)
dbus_docs += custom_target('de_tahifi_mounta_docs',
    input: '../dbus_interfaces/de_tahifi_mounta.xml',
    output: ['@BASENAME@-doc.md', '@BASENAME@-doc.h'],
    command: [
        extract_docs, '-i', '@INPUT@', '-o', '@OUTPUT0@', '-H', '@OUTPUT1@',
        '-c', 'tdbus', '-s', 'de.tahifi.', '-n', 'MounTA'
    ])

debug_dbus_src = gnome.gdbus_codegen('de_tahifi_debug',
    sources: '../dbus_interfaces/de_tahifi_debug.xml',
    interface_prefix: 'de.tahifi.Debug.',
    namespace: 'tdbus_debug')
debug_dbus_lib = static_library('debug_dbus',
    debug_dbus_src, dependencies: glib_deps, c_args: relaxed_dbus_warnings)
debug_dbus_dep = declare_dependency(link_with: debug_dbus_lib)
dbus_docs += custom_target('de_tahifi_debug_docs',
    input: '../dbus_interfaces/de_tahifi_debug.xml',
    output: ['@BASENAME@-doc.md', '@BASENAME@-doc.h'],
    command: [
        extract_docs, '-i', '@INPUT@', '-o', '@OUTPUT0@', '-H', '@OUTPUT1@',
        '-c', 'tdbus_debug', '-s', 'de.tahifi.Debug.', '-n', 'Debug Levels'
    ])

urlfifo_lib = static_library('urlfifo',
    ['playitem.cc', 'backtrace.c'],
    dependencies: glib_deps
)

gvariantwrapper_lib = static_library('gvariantwrapper',
    'gvariantwrapper.cc',
    dependencies: glib_deps
)

executable(
    'streamplayer',
    [
        'streamplayer.cc', 'streamer.cc', 'dbus_iface.cc', 'os.c',
        'stopped_reasons.cc', 'queue_filter.cc',
        'strbo_usb_url.cc', 'messages.c', 'messages_glib.c', 'messages_dbus.c',
        version_info
    ],
    include_directories: dbus_iface_defs_includes,
    dependencies: [
        streamplayer_dbus_dep,
        artcache_dbus_dep,
        audiopath_dbus_dep,
        mounta_dbus_dep,
        debug_dbus_dep,
        glib_deps,
        config_h,
    ],
    link_with: [
        urlfifo_lib,
        gvariantwrapper_lib
    ],
    install: true
)
