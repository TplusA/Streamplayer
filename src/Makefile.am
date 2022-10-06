#
# Copyright (C) 2018--2022  T+A elektroakustik GmbH & Co. KG
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

SUBDIRS = dbus

ACLOCAL_AMFLAGS = -I ../m4

AM_CFLAGS = $(CWARNINGS)
AM_CXXFLAGS = $(CXXWARNINGS)
AM_CPPFLAGS = $(STREAMPLAYER_DEPENDENCIES_CFLAGS)
AM_CPPFLAGS += -Idbus

bin_PROGRAMS = streamplayer

streamplayer_SOURCES = \
    streamplayer.cc messages.c messages.h messages_glib.c messages_glib.h \
    messages_dbus.c messages_dbus.h debug_levels.cc \
    backtrace.c backtrace.h \
    dbus.cc dbus.hh \
    strbo_usb_url.cc strbo_usb_url.hh \
    streamer.cc streamer.hh stream_id.h stream_id.hh buffering.hh \
    stopped_reasons.cc stopped_reasons.hh \
    urlfifo.hh playitem.hh streamdata.hh gvariantwrapper.hh \
    gstringwrapper.hh gerrorwrapper.hh \
    os.c os.h
streamplayer_LDADD = $(STREAMPLAYER_DEPENDENCIES_LIBS) $(noinst_LTLIBRARIES)

noinst_LTLIBRARIES = \
    liburlfifo.la \
    libgvariantwrapper.la

liburlfifo_la_SOURCES = playitem.cc playitem.hh streamdata.hh urlfifo.hh
liburlfifo_la_CFLAGS = $(AM_CFLAGS)
liburlfifo_la_CXXFLAGS = $(AM_CXXFLAGS)

libgvariantwrapper_la_SOURCES = gvariantwrapper.hh gvariantwrapper.cc
libgvariantwrapper_la_CFLAGS = $(AM_CFLAGS)
libgvariantwrapper_la_CXXFLAGS = $(AM_CXXFLAGS)

LIBS += \
    dbus/libtaddybus.la \
    dbus/libmounta_dbus.la \
    dbus/libartcache_dbus.la \
    dbus/libstreamplayer_dbus.la \
    dbus/libaudiopath_dbus.la \
    dbus/libdebug_dbus.la
