#ifndef DBUS_IFACE_H
#define DBUS_IFACE_H

#include <stdbool.h>

int dbus_setup(bool connect_to_session_bus);
void dbus_shutdown(void);

#endif /* !DBUS_IFACE_H */
