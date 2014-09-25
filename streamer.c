#include <errno.h>
#include <assert.h>

#include <gst/gst.h>

#include "streamer.h"
#include "urlfifo.h"
#include "dbus_iface_deep.h"
#include "messages.h"

enum queue_mode
{
    QUEUEMODE_JUST_UPDATE_URI,
    QUEUEMODE_START_PLAYING,
    QUEUEMODE_FORCE_SKIP,
};

struct streamer_data
{
    GstElement *pipeline;
    struct urlfifo_item current_stream;
};

static inline void invalidate_stream_url(struct urlfifo_item *item)
{
    item->url[0] = '\0';
}

static bool try_queue_next_stream(GstElement *pipeline,
                                  struct urlfifo_item *next_dest,
                                  enum queue_mode queue_mode)
{
    if(urlfifo_pop_item(next_dest) < 0)
        return false;

    msg_info("Queuing next stream: \"%s\"", next_dest->url);

    if(queue_mode == QUEUEMODE_FORCE_SKIP)
    {
        GstStateChangeReturn ret = gst_element_set_state(pipeline,
                                                         GST_STATE_READY);
        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
            msg_error(ENOSYS, LOG_ERR,
                      "Force skip: unhandled gst_element_set_state() "
                      "return code %d", ret);
    }

    g_object_set(G_OBJECT(pipeline), "uri", next_dest->url, NULL);

    if(queue_mode != QUEUEMODE_JUST_UPDATE_URI)
    {
        GstStateChangeReturn ret = gst_element_set_state(pipeline,
                                                         GST_STATE_PLAYING);
        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
            msg_error(ENOSYS, LOG_ERR,
                      "Play queued: unhandled gst_element_set_state() "
                      "return code %d", ret);
    }

    return true;
}

static void queue_stream_from_url_fifo(GstElement *elem, gpointer user_data)
{
    (void)try_queue_next_stream(elem, user_data, QUEUEMODE_JUST_UPDATE_URI);
}

static void handle_end_of_stream(GstBus *bus, GstMessage *message,
                                 gpointer user_data)
{
    msg_info("Finished playing all streams");

    struct streamer_data *data = user_data;
    GstStateChangeReturn ret = gst_element_set_state(data->pipeline,
                                                     GST_STATE_READY);

    if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
        msg_error(ENOSYS, LOG_ERR,
                  "EOS: unhandled gst_element_set_state() return code %d",
                  ret);

    invalidate_current_stream(data);
}

/*!
 * \todo Check embedded comment. How should we go about the GVariant format
 *     string(s)?
 */
static void start_of_new_stream(GstElement *elem, gpointer user_data)
{
    struct urlfifo_item *data = user_data;

    /*
     * The proper way to get at the GVariant format string would be to call
     * #tdbus_splay_playback_interface_info() and inspect the
     * \c GDBusInterfaceInfo introspection data to find the string deeply
     * buried inside the signal description.
     *
     * I think this is too much work just for retrieving a known value. Maybe a
     * unit test should be written to ensure that the hard-coded string here is
     * indeed correct. Maybe the retrieval should really be implemented in
     * production code, but only be done in the startup code.
     */
    GVariant *meta_data = g_variant_new("a(ss)", NULL);

    tdbus_splay_playback_emit_now_playing(dbus_get_playback_iface(),
                                          data->id, data->url,
                                          urlfifo_is_full(), meta_data);
}

static struct streamer_data streamer_data;

int streamer_setup(GMainLoop *loop)
{
#if GST_VERSION_MAJOR < 1
    streamer_data.pipeline = gst_element_factory_make("playbin2", "play");
#else
    streamer_data.pipeline = gst_element_factory_make("playbin", "play");
#endif

    if(streamer_data.pipeline == NULL)
        return -1;

    g_signal_connect(streamer_data.pipeline, "about-to-finish",
                     G_CALLBACK(queue_stream_from_url_fifo),
                     &streamer_data.current_stream);

    g_signal_connect(streamer_data.pipeline, "audio-changed",
                     G_CALLBACK(start_of_new_stream),
                     &streamer_data.current_stream);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(streamer_data.pipeline));
    assert(bus != NULL);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message::eos",
                     G_CALLBACK(handle_end_of_stream), &streamer_data);
    gst_object_unref(bus);

    g_main_loop_ref(loop);

    gst_element_set_state(streamer_data.pipeline, GST_STATE_READY);
    invalidate_stream_url(&streamer_data.current_stream);

    return 0;
}

void streamer_shutdown(GMainLoop *loop)
{
    if(loop == NULL)
        return;

    g_main_loop_unref(loop);

    gst_element_set_state(streamer_data.pipeline, GST_STATE_NULL);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(streamer_data.pipeline));
    assert(bus != NULL);
    gst_bus_remove_signal_watch(bus);
    gst_object_unref(bus);

    gst_object_unref(GST_OBJECT(streamer_data.pipeline));
    streamer_data.pipeline = NULL;
}

void streamer_start(void)
{
    GstState state;
    GstStateChangeReturn ret =
        gst_element_get_state(streamer_data.pipeline, &state, NULL, 0);

    if(ret != GST_STATE_CHANGE_SUCCESS)
    {
        msg_error(ENOSYS, LOG_ERR,
                  "Start: Unexpected gst_element_get_state() return code %d", ret);
        return;
    }

    if(state == GST_STATE_PLAYING)
        return;

    if(state == GST_STATE_READY &&
       !try_queue_next_stream(streamer_data.pipeline,
                              &streamer_data.current_stream,
                              QUEUEMODE_START_PLAYING))
    {
        msg_info("Got playback request, but URL FIFO is empty");
    }
}

void streamer_stop(void)
{
    msg_info("Stopping as requested");
    assert(streamer_data.pipeline != NULL);
    gst_element_set_state(streamer_data.pipeline, GST_STATE_READY);
    invalidate_stream_url(&streamer_data.current_stream);
}

void streamer_pause(void)
{
    msg_info("Pausing as requested");
    assert(streamer_data.pipeline != NULL);
    gst_element_set_state(streamer_data.pipeline, GST_STATE_PAUSED);
}

void streamer_next(void)
{
    msg_info("Next requested");
    assert(streamer_data.pipeline != NULL);

    GstState state;
    GstStateChangeReturn ret =
        gst_element_get_state(streamer_data.pipeline, &state, NULL, 0);

    if(ret != GST_STATE_CHANGE_SUCCESS)
    {
        msg_error(ENOSYS, LOG_ERR,
                  "Next: Unexpected gst_element_get_state() return code %d", ret);
        return;
    }

    if(state != GST_STATE_PLAYING)
    {
        msg_info("Not playing, not skipping");
        return;
    }

    if(!try_queue_next_stream(streamer_data.pipeline,
                              &streamer_data.current_stream,
                              QUEUEMODE_FORCE_SKIP))
        msg_info("Cannot play next, URL FIFO is empty");
}

