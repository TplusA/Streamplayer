#include <errno.h>
#include <assert.h>

#include <gst/gst.h>

#include "streamer.h"
#include "urlfifo.h"
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
};

static bool try_queue_next_stream(GstElement *pipeline,
                                  enum queue_mode queue_mode)
{
    struct urlfifo_item next;

    if(urlfifo_pop_item(&next) < 0)
        return false;

    msg_info("Queuing next stream: \"%s\"", next.url);

    if(queue_mode == QUEUEMODE_FORCE_SKIP)
    {
        GstStateChangeReturn ret = gst_element_set_state(pipeline,
                                                         GST_STATE_READY);
        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
            msg_error(ENOSYS, LOG_ERR,
                      "Force skip: unhandled gst_element_set_state() "
                      "return code %d", ret);
    }

    g_object_set(G_OBJECT(pipeline), "uri", next.url, NULL);

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
    (void)try_queue_next_stream(elem, QUEUEMODE_JUST_UPDATE_URI);
}

static void handle_end_of_stream(GstBus *bus, GstMessage *message,
                                 gpointer user_data)
{
    if(message->type == GST_MESSAGE_EOS)
    {
        msg_info("Finished playing all streams");
        GstStateChangeReturn ret =
            gst_element_set_state(user_data, GST_STATE_READY);
        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
            msg_error(ENOSYS, LOG_ERR,
                      "EOS: unhandled gst_element_set_state() return code %d",
                      ret);
    }
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
       !try_queue_next_stream(streamer_data.pipeline, QUEUEMODE_START_PLAYING))
    {
        msg_info("Got playback request, but URL FIFO is empty");
    }
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

    if(!try_queue_next_stream(streamer_data.pipeline, QUEUEMODE_FORCE_SKIP))
        msg_info("Cannot play next, URL FIFO is empty");
}

