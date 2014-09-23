#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <errno.h>
#include <assert.h>

#include "dbus_iface.h"
#include "streamplayer_dbus.h"
#include "messages.h"

struct dbus_data
{
    GThread *thread;
    GMainLoop *loop;
    guint owner_id;
    int acquired;
    tdbussplayPlayback *playback_iface;
    tdbussplayURLFIFO *urlfifo_iface;
};

static struct dbus_data dbus_data;

static gpointer process_dbus(gpointer user_data)
{
    struct dbus_data *data = user_data;

    assert(data->loop != NULL);

    g_main_loop_run(data->loop);
    return NULL;
}

static void try_export_iface(GDBusConnection *connection,
                             GDBusInterfaceSkeleton *iface)
{
    GError *error = NULL;

    g_dbus_interface_skeleton_export(iface, connection, "/de/tahifi/Streamplayer", &error);

    if(error)
    {
        msg_error(0, LOG_EMERG, "%s", error->message);
        g_error_free(error);
    }
}

static void bus_acquired(GDBusConnection *connection,
                         const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    data->playback_iface = tdbus_splay_playback_skeleton_new();
    data->urlfifo_iface = tdbus_splay_urlfifo_skeleton_new();

    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->playback_iface));
    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->urlfifo_iface));
}

static void name_acquired(GDBusConnection *connection,
                          const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_info("D-Bus name \"%s\" acquired", name);
    data->acquired = 1;
}

static void name_lost(GDBusConnection *connection,
                      const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_info("D-Bus name \"%s\" lost", name);
    data->acquired = -1;
}

static void destroy_notification(gpointer data)
{
    msg_info("Bus destroyed.");
}

int dbus_setup(bool connect_to_session_bus)
{
#if !GLIB_CHECK_VERSION(2, 36, 0)
    g_type_init();
#endif

    memset(&dbus_data, 0, sizeof(dbus_data));

    dbus_data.loop = g_main_loop_new(NULL, FALSE);
    if(dbus_data.loop == NULL)
    {
        msg_error(ENOMEM, LOG_EMERG, "Failed creating GLib main loop");
        return -1;
    }

    GBusType bus_type =
        connect_to_session_bus ? G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM;

    static const char bus_name[] = "de.tahifi.Streamplayer";

    dbus_data.owner_id =
        g_bus_own_name(bus_type, bus_name, G_BUS_NAME_OWNER_FLAGS_NONE,
                       bus_acquired, name_acquired, name_lost, &dbus_data,
                       destroy_notification);

    while(dbus_data.acquired == 0)
    {
        /* do whatever has to be done behind the scenes until one of the
         * guaranteed callbacks gets called */
        g_main_context_iteration(NULL, TRUE);
    }

    if(dbus_data.acquired < 0)
    {
        msg_error(EPIPE, LOG_EMERG, "Failed acquiring D-Bus name");
        return -1;
    }

    assert(dbus_data.playback_iface != NULL);
    assert(dbus_data.urlfifo_iface != NULL);

    dbus_data.thread = g_thread_new("D-Bus I/O", process_dbus, &dbus_data);
    if(dbus_data.thread == NULL)
    {
        msg_error(EAGAIN, LOG_EMERG, "Failed spawning D-Bus I/O thread");
        return -1;
    }

    return 0;
}

void dbus_shutdown(void)
{
    if(dbus_data.loop == NULL)
        return;

    g_bus_unown_name(dbus_data.owner_id);

    g_main_loop_quit(dbus_data.loop);
    (void)g_thread_join(dbus_data.thread);
    g_main_loop_unref(dbus_data.loop);

    g_object_unref(dbus_data.playback_iface);
    g_object_unref(dbus_data.urlfifo_iface);

    dbus_data.loop = NULL;
}
