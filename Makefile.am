#
# Copyright (C) 2015, 2016, 2017  T+A elektroakustik GmbH & Co. KG
#
# This file is part of T+A Streamplayer.
#
# T+A Streamplayer is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 3 as
# published by the Free Software Foundation.
#
# T+A Streamplayer is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with T+A Streamplayer.  If not, see <http://www.gnu.org/licenses/>.
#

SUBDIRS = . dist tests

ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = streamplayer

html_DATA = README.html

EXTRA_DIST = README.md

CLEANFILES = README.html

streamplayer_SOURCES = \
    streamplayer.c messages.c messages.h messages_glib.c messages_glib.h \
    messages_dbus.c messages_dbus.h \
    dbus_iface.c dbus_iface.h dbus_iface_deep.h \
    streamtime.h streamer.c streamer.h \
    os.c os.h

AM_CFLAGS = $(CWARNINGS)

AM_CPPFLAGS = $(STREAMPLAYER_DEPENDENCIES_CFLAGS)

noinst_LTLIBRARIES = libstreamplayer_dbus.la liburlfifo.la

streamplayer_LDADD = $(STREAMPLAYER_DEPENDENCIES_LIBS) $(noinst_LTLIBRARIES)

liburlfifo_la_SOURCES = urlfifo.c urlfifo.h

nodist_libstreamplayer_dbus_la_SOURCES = \
    streamplayer_dbus.c streamplayer_dbus.h \
    artcache_dbus.c artcache_dbus.h \
    audiopath_dbus.c audiopath_dbus.h \
    debug_dbus.c debug_dbus.h

libstreamplayer_dbus_la_CFLAGS = $(CRELAXEDWARNINGS)

EXTRA_DIST += \
    dbus_interfaces/extract_documentation.py \
    dbus_interfaces/de_tahifi_streamplayer.xml \
    dbus_interfaces/de_tahifi_artcache.xml \
    dbus_interfaces/de_tahifi_audiopath.xml \
    dbus_interfaces/de_tahifi_debug.xml

BUILT_SOURCES = \
    $(nodist_libstreamplayer_dbus_la_SOURCES) \
    de_tahifi_streamplayer.h \
    de_tahifi_streamplayer.md \
    streamplayer_dbus.stamp \
    de_tahifi_artcache.h \
    de_tahifi_artcache.md \
    artcache_dbus.stamp \
    de_tahifi_audiopath.h \
    de_tahifi_audiopath.md \
    audiopath_dbus.stamp \
    de_tahifi_debug.h \
    de_tahifi_debug.md \
    debug_dbus.stamp

CLEANFILES += $(BUILT_SOURCES)


de_tahifi_streamplayer.h: streamplayer_dbus.stamp
de_tahifi_streamplayer.md: streamplayer_dbus.stamp
streamplayer_dbus.c: streamplayer_dbus.stamp
streamplayer_dbus.h: streamplayer_dbus.stamp
streamplayer_dbus.stamp: dbus_interfaces/de_tahifi_streamplayer.xml
	$(GDBUS_CODEGEN) --generate-c-code=streamplayer_dbus --c-namespace tdbus_splay --interface-prefix de.tahifi.Streamplayer. $<
	$(abs_srcdir)/dbus_interfaces/extract_documentation.py -i $< -o de_tahifi_streamplayer.md -H de_tahifi_streamplayer.h -c tdbus_splay -s de.tahifi.Streamplayer. -n "$(PACKAGE_NAME)"
	touch $@

de_tahifi_artcache.h: artcache_dbus.stamp
de_tahifi_artcache.md: artcache_dbus.stamp
artcache_dbus.c: artcache_dbus.stamp
artcache_dbus.h: artcache_dbus.stamp
artcache_dbus.stamp: dbus_interfaces/de_tahifi_artcache.xml
	$(GDBUS_CODEGEN) --generate-c-code=artcache_dbus --c-namespace tdbus_artcache --interface-prefix de.tahifi.ArtCache. $<
	$(abs_srcdir)/dbus_interfaces/extract_documentation.py -i $< -o de_tahifi_artcache.md -H de_tahifi_artcache.h -c tdbus_artcache -s de.tahifi.ArtCache. -n "$(PACKAGE_NAME) (cover art)"
	touch $@

de_tahifi_audiopath.h: audiopath_dbus.stamp
de_tahifi_audiopath.md: audiopath_dbus.stamp
audiopath_dbus.c: audiopath_dbus.stamp
audiopath_dbus.h: audiopath_dbus.stamp
audiopath_dbus.stamp: dbus_interfaces/de_tahifi_audiopath.xml
	$(GDBUS_CODEGEN) --generate-c-code=audiopath_dbus --c-namespace tdbus_aupath --interface-prefix de.tahifi.AudioPath. $<
	$(abs_srcdir)/dbus_interfaces/extract_documentation.py -i $< -o de_tahifi_audiopath.md -H de_tahifi_audiopath.h -c tdbus_aupath -s de.tahifi.AudioPath. -n 'Audio Paths'
	touch $@

de_tahifi_debug.h: debug_dbus.stamp
de_tahifi_debug.md: debug_dbus.stamp
debug_dbus.c: debug_dbus.stamp
debug_dbus.h: debug_dbus.stamp
debug_dbus.stamp: dbus_interfaces/de_tahifi_debug.xml
	$(GDBUS_CODEGEN) --generate-c-code=debug_dbus --c-namespace tdbus_debug --interface-prefix de.tahifi.Debug. $<
	$(abs_srcdir)/dbus_interfaces/extract_documentation.py -i $< -o de_tahifi_debug.md -H de_tahifi_debug.h -c tdbus_debug -s de.tahifi.Debug. -n "Introspection"
	touch $@

BUILT_SOURCES += versioninfo.h

versioninfo.h: versioninfo.cache
	(cd $(top_srcdir) && $(AUTOREVISION) -t h -o $(abs_top_builddir)/versioninfo.cache >$(abs_top_builddir)/$@)

.md.html:
	$(MARKDOWN) $< >$@

if CAN_BUILD_DEBIAN_PACKAGE
DEBIAN_PACKAGE_FILES = debian/changelog debian/compat debian/control debian/rules

EXTRA_DIST += $(DEBIAN_PACKAGE_FILES)

deb: dist
	rm -rf "$(distdir)"
	GZIP=$(GZIP_ENV) gzip -dc "$(distdir).tar.gz" | $(am__untar)
	$(am__cd) "$(distdir)" && DH_SHLIBDEPS_ARGS="$(DH_SHLIBDEPS_ARGS)" $(DPKG_BUILDPACKAGE) -rfakeroot -D -b -t$(host_alias)
	rm -rf "$(distdir)"
else
deb:
	@echo "Cannot build Debian package, requirements not met."
	exit 1
endif
