#ifndef DBUS_IFACE_H
#define DBUS_IFACE_H

#include <stdbool.h>
#include <glib.h>

int dbus_setup(GMainLoop *loop, bool connect_to_session_bus);
void dbus_shutdown(GMainLoop *loop);

#endif /* !DBUS_IFACE_H */
