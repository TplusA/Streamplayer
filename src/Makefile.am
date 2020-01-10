#
# Copyright (C) 2018, 2019, 2020  T+A elektroakustik GmbH & Co. KG
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

ACLOCAL_AMFLAGS = -I ../m4

bin_PROGRAMS = streamplayer

streamplayer_SOURCES = \
    streamplayer.cc messages.c messages.h messages_glib.c messages_glib.h \
    messages_dbus.c messages_dbus.h \
    dbus_iface.cc dbus_iface.hh dbus_iface_deep.hh \
    streamer.cc streamer.hh stream_id.h stream_id.hh \
    urlfifo.hh playitem.hh streamdata.hh gvariantwrapper.hh \
    os.c os.h

DBUS_IFACES = $(top_srcdir)/dbus_interfaces

AM_CFLAGS = $(CWARNINGS)

AM_CXXFLAGS = $(CXXWARNINGS)

AM_CPPFLAGS = $(STREAMPLAYER_DEPENDENCIES_CFLAGS)
AM_CPPFLAGS += -I$(DBUS_IFACES)

noinst_LTLIBRARIES = \
    libstreamplayer_dbus.la liburlfifo.la libgvariantwrapper.la

streamplayer_LDADD = $(STREAMPLAYER_DEPENDENCIES_LIBS) $(noinst_LTLIBRARIES)

liburlfifo_la_SOURCES = playitem.cc playitem.hh streamdata.hh urlfifo.hh

libgvariantwrapper_la_SOURCES = gvariantwrapper.hh gvariantwrapper.cc

nodist_libstreamplayer_dbus_la_SOURCES = \
    streamplayer_dbus.c streamplayer_dbus.h \
    artcache_dbus.c artcache_dbus.h \
    audiopath_dbus.c audiopath_dbus.h \
    mounta_dbus.c mounta_dbus.h \
    debug_dbus.c debug_dbus.h

libstreamplayer_dbus_la_CFLAGS = $(CRELAXEDWARNINGS)

BUILT_SOURCES = \
    $(nodist_libstreamplayer_dbus_la_SOURCES) \
    de_tahifi_streamplayer.h \
    de_tahifi_streamplayer.md \
    streamplayer_dbus.stamp \
    de_tahifi_mounta.h \
    de_tahifi_mounta.md \
    mounta_dbus.stamp \
    de_tahifi_artcache.h \
    de_tahifi_artcache.md \
    artcache_dbus.stamp \
    de_tahifi_audiopath.h \
    de_tahifi_audiopath.md \
    audiopath_dbus.stamp \
    de_tahifi_debug.h \
    de_tahifi_debug.md \
    debug_dbus.stamp

CLEANFILES = $(BUILT_SOURCES)

de_tahifi_streamplayer.h: streamplayer_dbus.stamp
de_tahifi_streamplayer.md: streamplayer_dbus.stamp
streamplayer_dbus.c: streamplayer_dbus.stamp
streamplayer_dbus.h: streamplayer_dbus.stamp
streamplayer_dbus.stamp: $(DBUS_IFACES)/de_tahifi_streamplayer.xml
	$(GDBUS_CODEGEN) --generate-c-code=streamplayer_dbus --c-namespace tdbus_splay --interface-prefix de.tahifi.Streamplayer. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_streamplayer.md -H de_tahifi_streamplayer.h -c tdbus_splay -s de.tahifi.Streamplayer. -n "$(PACKAGE_NAME)"
	touch $@

de_tahifi_mounta.h: mounta_dbus.stamp
de_tahifi_mounta.md: mounta_dbus.stamp
mounta_dbus.c: mounta_dbus.stamp
mounta_dbus.h: mounta_dbus.stamp
mounta_dbus.stamp: $(DBUS_IFACES)/de_tahifi_mounta.xml
	$(GDBUS_CODEGEN) --generate-c-code=mounta_dbus --c-namespace tdbus --interface-prefix de.tahifi. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_mounta.md -H de_tahifi_mounta.h -c tdbus -s de.tahifi. -n "mounta"
	touch $@

de_tahifi_artcache.h: artcache_dbus.stamp
de_tahifi_artcache.md: artcache_dbus.stamp
artcache_dbus.c: artcache_dbus.stamp
artcache_dbus.h: artcache_dbus.stamp
artcache_dbus.stamp: $(DBUS_IFACES)/de_tahifi_artcache.xml
	$(GDBUS_CODEGEN) --generate-c-code=artcache_dbus --c-namespace tdbus_artcache --interface-prefix de.tahifi.ArtCache. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_artcache.md -H de_tahifi_artcache.h -c tdbus_artcache -s de.tahifi.ArtCache. -n "$(PACKAGE_NAME) (cover art)"
	touch $@

de_tahifi_audiopath.h: audiopath_dbus.stamp
de_tahifi_audiopath.md: audiopath_dbus.stamp
audiopath_dbus.c: audiopath_dbus.stamp
audiopath_dbus.h: audiopath_dbus.stamp
audiopath_dbus.stamp: $(DBUS_IFACES)/de_tahifi_audiopath.xml
	$(GDBUS_CODEGEN) --generate-c-code=audiopath_dbus --c-namespace tdbus_aupath --interface-prefix de.tahifi.AudioPath. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_audiopath.md -H de_tahifi_audiopath.h -c tdbus_aupath -s de.tahifi.AudioPath. -n 'Audio Paths'
	touch $@

de_tahifi_debug.h: debug_dbus.stamp
de_tahifi_debug.md: debug_dbus.stamp
debug_dbus.c: debug_dbus.stamp
debug_dbus.h: debug_dbus.stamp
debug_dbus.stamp: $(DBUS_IFACES)/de_tahifi_debug.xml
	$(GDBUS_CODEGEN) --generate-c-code=debug_dbus --c-namespace tdbus_debug --interface-prefix de.tahifi.Debug. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_debug.md -H de_tahifi_debug.h -c tdbus_debug -s de.tahifi.Debug. -n "Introspection"
	touch $@
