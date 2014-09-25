#include <gst/gst.h>

#include "streamer.h"

static struct
{
    GstElement *pipeline;
}
streamer_data;

int streamer_setup(GMainLoop *loop)
{
#if GST_VERSION_MAJOR < 1
    streamer_data.pipeline = gst_element_factory_make("playbin2", "play");
#else
    streamer_data.pipeline = gst_element_factory_make("playbin", "play");
#endif

    if(streamer_data.pipeline == NULL)
        return -1;

    g_main_loop_ref(loop);

    return 0;
}

void streamer_shutdown(GMainLoop *loop)
{
    if(loop == NULL)
        return;

    g_main_loop_unref(loop);

    gst_object_unref(GST_OBJECT(streamer_data.pipeline));
    streamer_data.pipeline = NULL;
}
