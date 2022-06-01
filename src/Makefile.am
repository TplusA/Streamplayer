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

ACLOCAL_AMFLAGS = -I ../m4

bin_PROGRAMS = streamplayer

streamplayer_SOURCES = \
    streamplayer.cc messages.c messages.h messages_glib.c messages_glib.h \
    messages_dbus.c messages_dbus.h \
    backtrace.c backtrace.h \
    dbus_iface.cc dbus_iface.hh dbus_iface_deep.hh \
    strbo_usb_url.cc strbo_usb_url.hh \
    streamer.cc streamer.hh stream_id.h stream_id.hh \
    stopped_reasons.cc stopped_reasons.hh \
    urlfifo.hh playitem.hh streamdata.hh gvariantwrapper.hh \
    gstringwrapper.hh gerrorwrapper.hh \
    os.c os.h

DBUS_IFACES = $(top_srcdir)/dbus_interfaces

AM_CPPFLAGS = $(STREAMPLAYER_DEPENDENCIES_CFLAGS)
AM_CPPFLAGS += -I$(DBUS_IFACES)

AM_CFLAGS = $(CWARNINGS)

AM_CXXFLAGS = $(CXXWARNINGS)

noinst_LTLIBRARIES = \
    libstreamplayer_dbus.la liburlfifo.la libgvariantwrapper.la

streamplayer_LDADD = $(STREAMPLAYER_DEPENDENCIES_LIBS) $(noinst_LTLIBRARIES)

liburlfifo_la_SOURCES = playitem.cc playitem.hh streamdata.hh urlfifo.hh

libgvariantwrapper_la_SOURCES = gvariantwrapper.hh gvariantwrapper.cc

nodist_libstreamplayer_dbus_la_SOURCES = \
    de_tahifi_streamplayer.c de_tahifi_streamplayer.h \
    de_tahifi_artcache.c de_tahifi_artcache.h \
    de_tahifi_audiopath.c de_tahifi_audiopath.h \
    de_tahifi_mounta.c de_tahifi_mounta.h \
    de_tahifi_debug.c de_tahifi_debug.h

libstreamplayer_dbus_la_CFLAGS = $(CRELAXEDWARNINGS)

BUILT_SOURCES = \
    $(nodist_libstreamplayer_dbus_la_SOURCES) \
    de_tahifi_streamplayer-doc.h \
    de_tahifi_streamplayer-doc.md \
    de_tahifi_streamplayer.stamp \
    de_tahifi_mounta-doc.h \
    de_tahifi_mounta-doc.md \
    de_tahifi_mounta.stamp \
    de_tahifi_artcache-doc.h \
    de_tahifi_artcache-doc.md \
    de_tahifi_artcache.stamp \
    de_tahifi_audiopath-doc.h \
    de_tahifi_audiopath-doc.md \
    de_tahifi_audiopath.stamp \
    de_tahifi_debug-doc.h \
    de_tahifi_debug-doc.md \
    de_tahifi_debug.stamp

CLEANFILES = $(BUILT_SOURCES)

de_tahifi_streamplayer-doc.h: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer-doc.md: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer.c: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer.h: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer.stamp: $(DBUS_IFACES)/de_tahifi_streamplayer.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_streamplayer --c-namespace tdbus_splay --interface-prefix de.tahifi.Streamplayer. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_streamplayer-doc.md -H de_tahifi_streamplayer-doc.h -c tdbus_splay -s de.tahifi.Streamplayer. -n "$(PACKAGE_NAME)"
	touch $@

de_tahifi_audiopath-doc.h: de_tahifi_audiopath.stamp
de_tahifi_audiopath-doc.md: de_tahifi_audiopath.stamp
de_tahifi_audiopath.c: de_tahifi_audiopath.stamp
de_tahifi_audiopath.h: de_tahifi_audiopath.stamp
de_tahifi_audiopath.stamp: $(DBUS_IFACES)/de_tahifi_audiopath.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_audiopath --c-namespace tdbus_aupath --interface-prefix de.tahifi.AudioPath. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_audiopath-doc.md -H de_tahifi_audiopath-doc.h -c tdbus_aupath -s de.tahifi.AudioPath. -n 'Audio Paths'
	touch $@

de_tahifi_mounta-doc.h: de_tahifi_mounta.stamp
de_tahifi_mounta-doc.md: de_tahifi_mounta.stamp
de_tahifi_mounta.c: de_tahifi_mounta.stamp
de_tahifi_mounta.h: de_tahifi_mounta.stamp
de_tahifi_mounta.stamp: $(DBUS_IFACES)/de_tahifi_mounta.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_mounta --c-namespace tdbus --interface-prefix de.tahifi. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_mounta-doc.md -H de_tahifi_mounta-doc.h -c tdbus -s de.tahifi. -n "MounTA"
	touch $@

de_tahifi_artcache-doc.h: de_tahifi_artcache.stamp
de_tahifi_artcache-doc.md: de_tahifi_artcache.stamp
de_tahifi_artcache.c: de_tahifi_artcache.stamp
de_tahifi_artcache.h: de_tahifi_artcache.stamp
de_tahifi_artcache.stamp: $(DBUS_IFACES)/de_tahifi_artcache.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_artcache --c-namespace tdbus_artcache --interface-prefix de.tahifi.ArtCache. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_artcache-doc.md -H de_tahifi_artcache-doc.h -c tdbus_artcache -s de.tahifi.ArtCache. -n "$(PACKAGE_NAME) (cover art)"
	touch $@

de_tahifi_debug-doc.h: de_tahifi_debug.stamp
de_tahifi_debug-doc.md: de_tahifi_debug.stamp
de_tahifi_debug.c: de_tahifi_debug.stamp
de_tahifi_debug.h: de_tahifi_debug.stamp
de_tahifi_debug.stamp: $(DBUS_IFACES)/de_tahifi_debug.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_debug --c-namespace tdbus_debug --interface-prefix de.tahifi.Debug. $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_debug-doc.md -H de_tahifi_debug-doc.h -c tdbus_debug -s de.tahifi.Debug. -n "Introspection"
	touch $@
