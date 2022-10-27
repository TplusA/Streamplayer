#
# Copyright (C) 2015, 2018, 2020  T+A elektroakustik GmbH & Co. KG
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

if WITH_DOCTEST
check_PROGRAMS = test_urlfifo test_playitem

TESTS = run_tests.sh

if WITH_VALGRIND
AM_TESTS_ENVIRONMENT =  VALGRIND="$(VALGRIND)"
AM_TESTS_ENVIRONMENT += VALGRIND_OPTIONS="--suppressions=$(srcdir)/valgrind.suppressions"
TESTS += valgrind.sh
endif

DBUS_IFACES = $(top_srcdir)/dbus_interfaces

EXTRA_DIST = run_tests.sh valgrind.sh
CLEANFILES = *.junit.xml *.valgrind.xml

AM_CPPFLAGS = -DDOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING
AM_CPPFLAGS += -I$(top_srcdir)/src -I$(top_builddir)/src
AM_CPPFLAGS += -I$(top_srcdir)/src/dbus -I$(top_builddir)/src/dbus
AM_CPPFLAGS += -I$(DBUS_IFACES)
AM_CFLAGS = $(CWARNINGS)
AM_CXXFLAGS = $(CXXWARNINGS)

noinst_LTLIBRARIES = libtestrunner.la

libtestrunner_la_SOURCES = testrunner.cc

BUILT_SOURCES = \
    mock_dbus_de_tahifi_airable.cc \
    mock_dbus_de_tahifi_airable.hh \
    mock_dbus_de_tahifi_airable.stamp

CLEANFILES += $(BUILT_SOURCES)

test_urlfifo_SOURCES = \
    test_urlfifo.cc \
    mock_os.hh mock_os.cc \
    mock_messages.hh mock_messages.cc \
    mock_backtrace.hh mock_backtrace.cc \
    mock_expectation.hh
test_urlfifo_LDADD = \
    libtestrunner.la \
    $(top_builddir)/src/liburlfifo.la
test_urlfifo_CPPFLAGS = $(AM_CPPFLAGS)
test_urlfifo_CFLAGS = $(AM_CFLAGS)
test_urlfifo_CXXFLAGS = $(AM_CXXFLAGS)

test_playitem_SOURCES = \
    test_playitem.cc \
    mock_os.hh mock_os.cc \
    mock_messages.hh mock_messages.cc \
    mock_dbus_de_tahifi_airable.hh mock_dbus_de_tahifi_airable.cc \
    mock_expectation.hh
test_playitem_LDADD = \
    libtestrunner.la \
    $(top_builddir)/src/liburlfifo.la \
    $(top_builddir)/src/libstrbo_url.la \
    $(top_builddir)/src/libstopped_reasons.la \
    $(top_builddir)/src/libgvariantwrapper.la \
    $(STREAMPLAYER_DEPENDENCIES_LIBS)
test_playitem_CPPFLAGS = $(AM_CPPFLAGS) $(STREAMPLAYER_DEPENDENCIES_CFLAGS)
test_playitem_CFLAGS = $(AM_CFLAGS)
test_playitem_CXXFLAGS = $(AM_CXXFLAGS)

mock_dbus_de_tahifi_airable.cc: mock_dbus_de_tahifi_airable.stamp
mock_dbus_de_tahifi_airable.hh: mock_dbus_de_tahifi_airable.stamp
mock_dbus_de_tahifi_airable.stamp: $(DBUS_IFACES)/de_tahifi_airable.xml
	PYTHONHOME= $(DBUS_IFACES)/taddybus-mockgen.py -o mock_dbus_de_tahifi_airable.hh -O mock_dbus_de_tahifi_airable.cc --interface-prefix de.tahifi. --c-namespace tdbus --cpp-namespace MockDBusAirable --interface de.tahifi.Airable --pointer-pattern 0xa112ab1e $<

doctest: $(check_PROGRAMS)
	for p in $(check_PROGRAMS); do \
	    if ./$$p $(DOCTEST_EXTRA_OPTIONS); then :; \
	    else \
	        echo "*****"; \
	        echo "***** Running $$p again in gdb due to failures"; \
	        echo "*****"; \
	        gdb -ex run --args ./$$p $(DOCTEST_EXTRA_OPTIONS); \
	    fi; \
	done

doctest-valgrind: $(check_PROGRAMS)
	for p in $(check_PROGRAMS); do $(VALGRIND) --leak-check=full --show-reachable=yes --error-limit=no ./$$p $(DOCTEST_EXTRA_OPTIONS); done
endif
