if WITH_CUTTER
TESTS = run_test.sh

AM_TESTS_ENVIRONMENT = CUTTER="$(CUTTER)"

AM_LDFLAGS = -module -rpath $(libdir) -avoid-version -no-undefined

AM_CPPFLAGS = $(CUTTER_CFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)

LIBS += $(CUTTER_LIBS)

noinst_LTLIBRARIES = test_urlfifo.la

test_urlfifo_la_SOURCES = test_urlfifo.c

test_urlfifo_la_LIBADD = ../liburlfifo.la

CLEANFILES = test_report.xml
endif
