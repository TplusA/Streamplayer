#
# Copyright (C) 2022, 2023  T+A elektroakustik GmbH & Co. KG
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

gnome = import('gnome')

dbus_iface_dir = '../../dbus_interfaces'
dbus_iface_defs_includes = include_directories(dbus_iface_dir)

dbus_iface_data = {
    'de_tahifi_debug': [
        'de.tahifi.Debug.',         'tdbus_debug',    'Debug',
        'Debug Levels',
    ],
    'de_tahifi_streamplayer': [
        'de.tahifi.Streamplayer.',  'tdbus_splay',    'Streamplayer',
        'Stream Player',
    ],
    'de_tahifi_audiopath': [
        'de.tahifi.AudioPath.',     'tdbus_aupath',   'AudioPath',
        'Audio Paths',
    ],
    'de_tahifi_artcache': [
        'de.tahifi.ArtCache.',      'tdbus_artcache', 'ArtCache',
        'Cover Art'
    ],
    'de_tahifi_mounta': [
        'de.tahifi.',               'tdbus',          '',
        'T+A mount daemon'
    ],
    'de_tahifi_airable': [
        'de.tahifi.',               'tdbus',          '',
        'Airable'
    ],
}

dbus_deps = []
dbus_docs = []
dbus_toc_inputs = []
tdbus_headers = []

foreach name, d : dbus_iface_data
    dbus_deps += declare_dependency(
        link_with: static_library(
            name.split('_')[-1] + '_dbus',
            gnome.gdbus_codegen(name,
                                sources: dbus_iface_dir / name + '.xml',
                                interface_prefix: d[0],
                                namespace: d[1]),
            dependencies: [glib_deps, config_h],
            c_args: relaxed_dbus_warnings
        ),
        include_directories: dbus_iface_defs_includes
    )

    doc_target = custom_target(name + '_docs',
        input: dbus_iface_dir / name + '.xml',
        output: ['@BASENAME@-doc.mdp', '@BASENAME@-doc.h'],
        command: [
            extract_docs, '-i', '@INPUT@', '-o', '@OUTPUT0@', '-H', '@OUTPUT1@',
            '-c', d[1], '-s', d[0], '-n', d[3]
        ]
    )
    dbus_toc_inputs += doc_target[0]
    dbus_docs += doc_target
    doc_target = []

    tdbus_headers += custom_target(name + '_tdbus',
        input: dbus_iface_dir / name + '.xml',
        output: '@BASENAME@.hh',
        command: [
            taddybus_codegen, '-o', '@OUTPUT@', '--interface-prefix', d[0],
            '--c-namespace', d[1], '--cpp-traits-prefix', d[2], '@INPUT@'
        ]
    )
endforeach

dbus_docs += custom_target('all_interfaces_toc.md',
    input: dbus_toc_inputs,
    output: ['all_interfaces_toc.md', 'all_interfaces.md'],
    command: [
        combine_iface_pages, '-toc', '@OUTPUT0@', '-ifaces', '@OUTPUT1@', '@INPUT@'
    ]
)

taddybus_lib = static_library('taddybus',
    ['taddybus.cc', tdbus_headers, dbus_docs],
    dependencies: [glib_deps, config_h],
)
