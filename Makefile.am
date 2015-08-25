#
# Copyright (C) 2015  T+A elektroakustik GmbH & Co. KG
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
    streamplayer.c messages.c messages.h \
    dbus_iface.c dbus_iface.h dbus_iface_deep.h \
    streamtime.h streamer.c streamer.h

AM_CFLAGS = $(CWARNINGS)

AM_CPPFLAGS = $(STREAMPLAYER_DEPENDENCIES_CFLAGS)

noinst_LTLIBRARIES = libstreamplayer_dbus.la liburlfifo.la

streamplayer_LDADD = $(STREAMPLAYER_DEPENDENCIES_LIBS) $(noinst_LTLIBRARIES)

liburlfifo_la_SOURCES = urlfifo.c urlfifo.h

nodist_libstreamplayer_dbus_la_SOURCES = streamplayer_dbus.c streamplayer_dbus.h

libstreamplayer_dbus_la_CFLAGS = $(CRELAXEDWARNINGS)

EXTRA_DIST += dbus_interfaces/extract_documentation.py dbus_interfaces/de_tahifi_streamplayer.xml

BUILT_SOURCES = $(nodist_libstreamplayer_dbus_la_SOURCES) de_tahifi_streamplayer.h de_tahifi_streamplayer.md streamplayer_dbus.stamp

CLEANFILES += $(BUILT_SOURCES)


de_tahifi_streamplayer.h: streamplayer_dbus.stamp
de_tahifi_streamplayer.md: streamplayer_dbus.stamp
streamplayer_dbus.c: streamplayer_dbus.stamp
streamplayer_dbus.h: streamplayer_dbus.stamp
streamplayer_dbus.stamp: dbus_interfaces/de_tahifi_streamplayer.xml
	$(GDBUS_CODEGEN) --generate-c-code=streamplayer_dbus --c-namespace tdbus_splay --interface-prefix de.tahifi.Streamplayer. $<
	$(abs_srcdir)/dbus_interfaces/extract_documentation.py -i $< -o de_tahifi_streamplayer.md -H de_tahifi_streamplayer.h -c tdbus_splay -s de.tahifi.Streamplayer. -n "$(PACKAGE_NAME)"
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
