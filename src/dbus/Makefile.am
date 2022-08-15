#
# Copyright (C) 2022  T+A elektroakustik GmbH & Co. KG
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

ACLOCAL_AMFLAGS = -I ../../m4

DBUS_IFACES = $(top_srcdir)/dbus_interfaces

noinst_LTLIBRARIES = \
    libtaddybus.la \
    libmounta_dbus.la \
    libartcache_dbus.la \
    libstreamplayer_dbus.la \
    libaudiopath_dbus.la \
    libdebug_dbus.la

AM_CFLAGS = $(CRELAXEDWARNINGS)
AM_CXXFLAGS = $(CXXRELAXEDWARNINGS)
AM_CPPFLAGS = $(STREAMPLAYER_DEPENDENCIES_CFLAGS)

libtaddybus_la_SOURCES = taddybus.cc taddybus.hh
libtaddybus_la_CPPFLAGS = -I$(srcdir)/.. $(AM_CPPFLAGS)

nodist_libstreamplayer_dbus_la_SOURCES = de_tahifi_streamplayer.c de_tahifi_streamplayer.h de_tahifi_streamplayer.hh
libstreamplayer_dbus_la_CPPFLAGS = $(AM_CPPFLAGS)
libstreamplayer_dbus_la_CFLAGS = $(AM_CFLAGS)
libstreamplayer_dbus_la_CXXFLAGS = $(AM_CXXFLAGS)

nodist_libmounta_dbus_la_SOURCES = de_tahifi_mounta.c de_tahifi_mounta.h de_tahifi_mounta.hh
libmounta_dbus_la_CPPFLAGS = $(AM_CPPFLAGS)
libmounta_dbus_la_CFLAGS = $(AM_CFLAGS)
libmounta_dbus_la_CXXFLAGS = $(AM_CXXFLAGS)

nodist_libartcache_dbus_la_SOURCES = de_tahifi_artcache.c de_tahifi_artcache.h de_tahifi_artcache.hh
libartcache_dbus_la_CPPFLAGS = $(AM_CPPFLAGS)
libartcache_dbus_la_CFLAGS = $(AM_CFLAGS)
libartcache_dbus_la_CXXFLAGS = $(AM_CXXFLAGS)

nodist_libaudiopath_dbus_la_SOURCES = de_tahifi_audiopath.c de_tahifi_audiopath.h de_tahifi_audiopath.hh
libaudiopath_dbus_la_CPPFLAGS = $(AM_CPPFLAGS)
libaudiopath_dbus_la_CFLAGS = $(AM_CFLAGS)
libaudiopath_dbus_la_CXXFLAGS = $(AM_CXXFLAGS)

nodist_libdebug_dbus_la_SOURCES = de_tahifi_debug.c de_tahifi_debug.h de_tahifi_debug.hh
libdebug_dbus_la_CPPFLAGS = $(AM_CPPFLAGS)
libdebug_dbus_la_CFLAGS = $(AM_CFLAGS)
libdebug_dbus_la_CXXFLAGS = $(AM_CXXFLAGS)

BUILT_SOURCES = \
    $(nodist_libstreamplayer_dbus_la_SOURCES) \
    de_tahifi_streamplayer-doc.h \
    de_tahifi_streamplayer-doc.md \
    de_tahifi_streamplayer.stamp \
    $(nodist_libmounta_dbus_la_SOURCES) \
    de_tahifi_mounta-doc.h \
    de_tahifi_mounta-doc.md \
    de_tahifi_mounta.stamp \
    $(nodist_libartcache_dbus_la_SOURCES) \
    de_tahifi_artcache-doc.h \
    de_tahifi_artcache-doc.md \
    de_tahifi_artcache.stamp \
    $(nodist_libaudiopath_dbus_la_SOURCES) \
    de_tahifi_audiopath-doc.h \
    de_tahifi_audiopath-doc.md \
    de_tahifi_audiopath.stamp \
    $(nodist_libdebug_dbus_la_SOURCES) \
    de_tahifi_debug-doc.h \
    de_tahifi_debug-doc.md \
    de_tahifi_debug.stamp

CLEANFILES = $(BUILT_SOURCES)

de_tahifi_streamplayer-doc.h: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer-doc.md: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer.c: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer.h: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer.hh: de_tahifi_streamplayer.stamp
de_tahifi_streamplayer.stamp: $(DBUS_IFACES)/de_tahifi_streamplayer.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_streamplayer --c-namespace tdbus_splay --interface-prefix de.tahifi.Streamplayer. $<
	PYTHONHOME= $(DBUS_IFACES)/taddybus-codegen.py --c-namespace tdbus_splay --interface-prefix de.tahifi.Streamplayer. --cpp-traits-prefix Streamplayer -o de_tahifi_streamplayer.hh $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_streamplayer-doc.md -H de_tahifi_streamplayer-doc.h -c tdbus_splay -s de.tahifi.Streamplayer. -n "$(PACKAGE_NAME)"
	touch $@

de_tahifi_audiopath-doc.h: de_tahifi_audiopath.stamp
de_tahifi_audiopath-doc.md: de_tahifi_audiopath.stamp
de_tahifi_audiopath.c: de_tahifi_audiopath.stamp
de_tahifi_audiopath.h: de_tahifi_audiopath.stamp
de_tahifi_audiopath.hh: de_tahifi_audiopath.stamp
de_tahifi_audiopath.stamp: $(DBUS_IFACES)/de_tahifi_audiopath.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_audiopath --c-namespace tdbus_aupath --interface-prefix de.tahifi.AudioPath. $<
	PYTHONHOME= $(DBUS_IFACES)/taddybus-codegen.py --c-namespace tdbus_aupath --interface-prefix de.tahifi.AudioPath. --cpp-traits-prefix AudioPath -o de_tahifi_audiopath.hh $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_audiopath-doc.md -H de_tahifi_audiopath-doc.h -c tdbus_aupath -s de.tahifi.AudioPath. -n 'Audio Paths'
	touch $@

de_tahifi_mounta-doc.h: de_tahifi_mounta.stamp
de_tahifi_mounta-doc.md: de_tahifi_mounta.stamp
de_tahifi_mounta.c: de_tahifi_mounta.stamp
de_tahifi_mounta.h: de_tahifi_mounta.stamp
de_tahifi_mounta.hh: de_tahifi_mounta.stamp
de_tahifi_mounta.stamp: $(DBUS_IFACES)/de_tahifi_mounta.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_mounta --c-namespace tdbus --interface-prefix de.tahifi. $<
	PYTHONHOME= $(DBUS_IFACES)/taddybus-codegen.py --c-namespace tdbus --interface-prefix de.tahifi. -o de_tahifi_mounta.hh $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_mounta-doc.md -H de_tahifi_mounta-doc.h -c tdbus -s de.tahifi. -n "MounTA"
	touch $@

de_tahifi_artcache-doc.h: de_tahifi_artcache.stamp
de_tahifi_artcache-doc.md: de_tahifi_artcache.stamp
de_tahifi_artcache.c: de_tahifi_artcache.stamp
de_tahifi_artcache.h: de_tahifi_artcache.stamp
de_tahifi_artcache.hh: de_tahifi_artcache.stamp
de_tahifi_artcache.stamp: $(DBUS_IFACES)/de_tahifi_artcache.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_artcache --c-namespace tdbus_artcache --interface-prefix de.tahifi.ArtCache. $<
	PYTHONHOME= $(DBUS_IFACES)/taddybus-codegen.py --c-namespace tdbus_artcache --interface-prefix de.tahifi.ArtCache. --cpp-traits-prefix ArtCache -o de_tahifi_artcache.hh $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_artcache-doc.md -H de_tahifi_artcache-doc.h -c tdbus_artcache -s de.tahifi.ArtCache. -n "$(PACKAGE_NAME) (cover art)"
	touch $@

de_tahifi_debug-doc.h: de_tahifi_debug.stamp
de_tahifi_debug-doc.md: de_tahifi_debug.stamp
de_tahifi_debug.c: de_tahifi_debug.stamp
de_tahifi_debug.h: de_tahifi_debug.stamp
de_tahifi_debug.hh: de_tahifi_debug.stamp
de_tahifi_debug.stamp: $(DBUS_IFACES)/de_tahifi_debug.xml
	$(GDBUS_CODEGEN) --generate-c-code=de_tahifi_debug --c-namespace tdbus_debug --interface-prefix de.tahifi.Debug. $<
	PYTHONHOME= $(DBUS_IFACES)/taddybus-codegen.py --c-namespace tdbus_debug --interface-prefix de.tahifi.Debug. --cpp-traits-prefix Debug -o de_tahifi_debug.hh $<
	$(DBUS_IFACES)/extract_documentation.py -i $< -o de_tahifi_debug-doc.md -H de_tahifi_debug-doc.h -c tdbus_debug -s de.tahifi.Debug. -n "Introspection"
	touch $@
