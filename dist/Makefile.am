dbusservicedir = $(datadir)/dbus-1/services

dbusservice_in_files = de.tahifi.Streamplayer.in

dbusservice_DATA = $(dbusservice_in_files:.in=.service)

EXTRA_DIST = $(dbusservice_in_files)

CLEANFILES = $(dbusservice_DATA)

V_SUBST = $(v_SUBST_@AM_V@)
v_SUBST_ = $(v_SUBST_@AM_DEFAULT_V@)
v_SUBST_0 = @echo "  GEN     " $@;
v_SUBST_1 =

do_subst = sed -e 's,[@]bindir[@],$(bindir),g'

.in.service: Makefile
	$(V_SUBST)$(do_subst) <$< >$@
