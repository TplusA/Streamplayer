#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <errno.h>
#include <assert.h>

#include "dbus_iface.h"
#include "streamplayer_dbus.h"
#include "messages.h"

static gboolean playback_start(tdbussplayPlayback *object,
                               GDBusMethodInvocation *invocation)
{
    msg_info("Got Playback.Start message");
    tdbus_splay_playback_complete_start(object, invocation);
    return TRUE;
}

static gboolean playback_stop(tdbussplayPlayback *object,
                              GDBusMethodInvocation *invocation)
{
    msg_info("Got Playback.Stop message");
    tdbus_splay_playback_complete_stop(object, invocation);
    return TRUE;
}

static gboolean playback_pause(tdbussplayPlayback *object,
                               GDBusMethodInvocation *invocation)
{
    msg_info("Got Playback.Pause message");
    tdbus_splay_playback_complete_pause(object, invocation);
    return TRUE;
}

struct dbus_data
{
    guint owner_id;
    int acquired;
    tdbussplayPlayback *playback_iface;
    tdbussplayURLFIFO *urlfifo_iface;
};

static struct dbus_data dbus_data;

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

    g_signal_connect(data->playback_iface, "handle-start",
                     G_CALLBACK(playback_start), NULL);
    g_signal_connect(data->playback_iface, "handle-stop",
                     G_CALLBACK(playback_stop), NULL);
    g_signal_connect(data->playback_iface, "handle-pause",
                     G_CALLBACK(playback_pause), NULL);

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

int dbus_setup(GMainLoop *loop, bool connect_to_session_bus)
{
    memset(&dbus_data, 0, sizeof(dbus_data));

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

    g_main_loop_ref(loop);

    return 0;
}

void dbus_shutdown(GMainLoop *loop)
{
    if(loop == NULL)
        return;

    g_bus_unown_name(dbus_data.owner_id);

    g_main_loop_unref(loop);
    g_object_unref(dbus_data.playback_iface);
    g_object_unref(dbus_data.urlfifo_iface);
}
