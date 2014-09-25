#include <errno.h>
#include <assert.h>

#include <gst/gst.h>

#include "streamer.h"
#include "messages.h"

static struct
{
    GstElement *pipeline;
}
streamer_data;

static void queue_stream_from_url_fifo(GstElement *elem, gpointer userdata)
{
    msg_error(ENOSYS, LOG_ERR,
              "Current stream is about to finish, need to queue next one");
}

static void handle_end_of_stream(GstBus *bus, GstMessage *message,
                                 gpointer user_data)
{
    if(message->type == GST_MESSAGE_EOS)
    {
        msg_info("Finished playing all streams");
        gst_element_set_state(user_data, GST_STATE_READY);
    }
}

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
                     G_CALLBACK(queue_stream_from_url_fifo), NULL);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(streamer_data.pipeline));
    assert(bus != NULL);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", G_CALLBACK(handle_end_of_stream),
                     streamer_data.pipeline);
    gst_object_unref(bus);

    g_main_loop_ref(loop);

    gst_element_set_state(streamer_data.pipeline, GST_STATE_READY);

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
    static const char test_uri[] = "http://dsl.german-gothic-radio.de:8046";

    GstState state;
    GstStateChangeReturn ret =
        gst_element_get_state(streamer_data.pipeline, &state, NULL, 0);

    if(ret == GST_STATE_CHANGE_SUCCESS)
    {
        if(state == GST_STATE_READY)
        {
            msg_info("Start playing \"%s\"", test_uri);
            g_object_set(G_OBJECT(streamer_data.pipeline), "uri", test_uri, NULL);
        }
    }
    else
        msg_error(ENOSYS, LOG_ERR,
                  "Unexpected gst_element_get_state() return code %d", ret);

    ret = gst_element_set_state(streamer_data.pipeline, GST_STATE_PLAYING);

    if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
        msg_info("Unhandled gst_element_set_state() return code %d", ret);
}

void streamer_stop(void)
{
    msg_info("Stopping as requested");
    assert(streamer_data.pipeline != NULL);
    gst_element_set_state(streamer_data.pipeline, GST_STATE_READY);
}

void streamer_pause(void)
{
    msg_info("Pausing as requested");
    assert(streamer_data.pipeline != NULL);
    gst_element_set_state(streamer_data.pipeline, GST_STATE_PAUSED);
}

void streamer_next(void)
{
    msg_error(ENOSYS, LOG_ERR, "Got play next request");
    assert(streamer_data.pipeline != NULL);
}

