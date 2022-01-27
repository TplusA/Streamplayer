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

project('Streamplayer', ['cpp', 'c'],
    version: '5.18',
    default_options: ['warning_level=3', 'cpp_std=c++14', 'buildtype=plain'])

PACKAGE = 'streamplayer'

config_data = configuration_data()
config_data.set('PACKAGE', PACKAGE)
config_data.set('PACKAGE_NAME', meson.project_name())
config_data.set('PACKAGE_VERSION', meson.project_version())
config_data.set('abs_srcdir', meson.current_source_dir())
config_data.set('abs_builddir', meson.build_root())
config_data.set('bindir', get_option('prefix') / get_option('bindir'))

add_project_arguments('-DHAVE_CONFIG_H', language: ['cpp', 'c'])

relaxed_dbus_warnings = ['-Wno-bad-function-cast']

glib_deps = [
    dependency('gmodule-2.0'),
    dependency('gio-2.0'),
    dependency('gio-unix-2.0'),
    dependency('gthread-2.0'),
    dependency('gstreamer-tag-1.0'),
    dependency('gstreamer-plugins-base-1.0'),
]

autorevision = find_program('autorevision')
markdown = find_program('markdown')
extract_docs = find_program('dbus_interfaces/extract_documentation.py')
doxygen = find_program('doxygen', required: false)

doxyfile = configure_file(input: 'Doxyfile.in', output: 'Doxyfile',
                          configuration: config_data)

version_info = custom_target(
    'version information file',
    input: configure_file(input: 'versioninfo.cache.in',
                          output: 'versioninfo.cache', copy: true),
    output: 'versioninfo.h',
    command: [
        autorevision, '-C', meson.current_source_dir(),
        '-t', 'h',
        '-o', 'versioninfo.cache'
    ],
    capture: true
)

subdir('src')
subdir('tests')

custom_target('doxygen', output: 'doxygen.stamp',
              input: doxyfile, depends: dbus_docs, command: doxygen)

custom_target('documentation',
    output: 'README.html', input: 'README.md',
    command: [markdown, '-o', '@OUTPUT@', '@INPUT@'],
    install: true, install_dir: get_option('datadir') / 'doc/streamplayer')
