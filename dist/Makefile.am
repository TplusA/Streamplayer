#
# Copyright (C) 2015, 2020  T+A elektroakustik GmbH & Co. KG
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
