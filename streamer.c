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
    bool tags_are_for_queued_stream;
    GstTagList *current_stream_tags;
    GstTagList *queued_stream_tags;
};

static void invalidate_tag_list(GstTagList **list)
{
    if(*list == NULL)
        return;

    gst_tag_list_free(*list);
    *list = NULL;
}

static void invalidate_current_stream(struct streamer_data *data)
{
    data->current_stream.url[0] = '\0';
    invalidate_tag_list(&data->current_stream_tags);
    invalidate_tag_list(&data->queued_stream_tags);
}

static bool try_queue_next_stream(GstElement *pipeline,
                                  struct streamer_data *data,
                                  enum queue_mode queue_mode,
                                  GstState next_state)
{
    if(urlfifo_pop_item(&data->current_stream) < 0)
        return false;

    msg_info("Queuing next stream: \"%s\"", data->current_stream.url);

    data->tags_are_for_queued_stream =
        (queue_mode == QUEUEMODE_JUST_UPDATE_URI || QUEUEMODE_START_PLAYING);

    if(queue_mode == QUEUEMODE_FORCE_SKIP)
    {
        GstStateChangeReturn ret = gst_element_set_state(pipeline,
                                                         GST_STATE_READY);
        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
            msg_error(ENOSYS, LOG_ERR,
                      "Force skip: unhandled gst_element_set_state() "
                      "return code %d", ret);
    }

    g_object_set(G_OBJECT(pipeline), "uri", data->current_stream.url, NULL);

    if(queue_mode == QUEUEMODE_START_PLAYING ||
       queue_mode == QUEUEMODE_FORCE_SKIP)
    {
        GstStateChangeReturn ret = gst_element_set_state(pipeline, next_state);
        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
            msg_error(ENOSYS, LOG_ERR,
                      "Play queued: unhandled gst_element_set_state() "
                      "return code %d", ret);
    }

    return true;
}

static void queue_stream_from_url_fifo(GstElement *elem, gpointer user_data)
{
    (void)try_queue_next_stream(elem, user_data,
                                QUEUEMODE_JUST_UPDATE_URI, GST_STATE_NULL);
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

static void add_tuple_to_tags_variant_builder(const GstTagList *list,
                                              const gchar *tag,
                                              gpointer user_data)
{
    GVariantBuilder *builder = user_data;
    const GValue *value = gst_tag_list_get_value_index(list, tag, 0);

    if(value == NULL)
        return;

    if(G_VALUE_HOLDS_STRING(value))
        g_variant_builder_add(builder, "(ss)", tag, g_value_get_string(value));
    else if(G_VALUE_HOLDS_BOOLEAN(value))
        g_variant_builder_add(builder, "(ss)", tag,
                              g_value_get_boolean(value) ? "true" : "false");
    else if(G_VALUE_HOLDS_UINT(value))
    {
        char buffer[256];

        snprintf(buffer, sizeof(buffer), "%u", g_value_get_uint(value));
        g_variant_builder_add(builder, "(ss)", tag, buffer);
    }
    else
        msg_error(ENOSYS, LOG_ERR, "stream tag \"%s\" is not a string", tag);
}

/*!
 * \todo Check embedded comment. How should we go about the GVariant format
 *     string(s)?
 */
static GVariant *tag_list_to_g_variant(const GstTagList *list)
{
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
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ss)"));
    gst_tag_list_foreach(list, add_tuple_to_tags_variant_builder, &builder);

    return g_variant_builder_end(&builder);
}

static void handle_tag(GstBus *bus, GstMessage *message, gpointer user_data)
{
    GstTagList *tags = NULL;
    gst_message_parse_tag(message, &tags);

    struct streamer_data *data = user_data;
    GstTagList **list = (data->tags_are_for_queued_stream
                         ? &data->queued_stream_tags
                         : &data->current_stream_tags);

    if(*list != NULL)
    {
        GstTagList *merged =
            gst_tag_list_merge(*list, tags, GST_TAG_MERGE_PREPEND);
        gst_tag_list_free(tags);
        gst_tag_list_free(*list);
        *list = merged;
    }
    else
        *list = tags;

    if(*list != NULL && !data->tags_are_for_queued_stream)
    {
        GVariant *meta_data = tag_list_to_g_variant(*list);

        tdbus_splay_playback_emit_meta_data_changed(dbus_get_playback_iface(),
                                                    meta_data);
    }
}

static void start_of_new_stream(GstElement *elem, gpointer user_data)
{
    struct streamer_data *data = user_data;

    invalidate_tag_list(&data->current_stream_tags);
    data->current_stream_tags = data->queued_stream_tags;
    data->queued_stream_tags = NULL;
    data->tags_are_for_queued_stream = false;

    GVariant *meta_data = tag_list_to_g_variant(data->current_stream_tags);

    tdbus_splay_playback_emit_now_playing(dbus_get_playback_iface(),
                                          data->current_stream.id,
                                          data->current_stream.url,
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
                     G_CALLBACK(queue_stream_from_url_fifo), &streamer_data);

    g_signal_connect(streamer_data.pipeline, "audio-changed",
                     G_CALLBACK(start_of_new_stream), &streamer_data);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(streamer_data.pipeline));
    assert(bus != NULL);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message::eos",
                     G_CALLBACK(handle_end_of_stream), &streamer_data);
    g_signal_connect(bus, "message::tag",
                     G_CALLBACK(handle_tag), &streamer_data);
    gst_object_unref(bus);

    g_main_loop_ref(loop);

    gst_element_set_state(streamer_data.pipeline, GST_STATE_READY);
    invalidate_current_stream(&streamer_data);

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

    invalidate_current_stream(&streamer_data);
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

    switch(state)
    {
      case GST_STATE_PLAYING:
        break;

      case GST_STATE_READY:
        if(!try_queue_next_stream(streamer_data.pipeline, &streamer_data,
                                  QUEUEMODE_START_PLAYING, GST_STATE_PLAYING))
        {
            msg_info("Got playback request, but URL FIFO is empty");
        }

        break;

      case GST_STATE_PAUSED:
        ret = gst_element_set_state(streamer_data.pipeline, GST_STATE_PLAYING);

        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
            msg_error(ENOSYS, LOG_ERR,
                      "Start: unhandled gst_element_set_state() return code %d",
                      ret);

        break;

      case GST_STATE_NULL:
      case GST_STATE_VOID_PENDING:
        msg_error(ENOSYS, LOG_ERR,
                  "Start: pipeline is in unhandled state %d", state);
        break;
    }
}

void streamer_stop(void)
{
    msg_info("Stopping as requested");
    assert(streamer_data.pipeline != NULL);
    gst_element_set_state(streamer_data.pipeline, GST_STATE_READY);
    invalidate_current_stream(&streamer_data);
}

void streamer_pause(void)
{
    msg_info("Pausing as requested");
    assert(streamer_data.pipeline != NULL);

    GstState state;
    GstStateChangeReturn ret =
        gst_element_get_state(streamer_data.pipeline, &state, NULL, 0);

    if(ret != GST_STATE_CHANGE_SUCCESS)
    {
        msg_error(ENOSYS, LOG_ERR,
                  "Start: Unexpected gst_element_get_state() return code %d", ret);
        return;
    }

    switch(state)
    {
      case GST_STATE_PAUSED:
        return;

      case GST_STATE_READY:
        if(!try_queue_next_stream(streamer_data.pipeline, &streamer_data,
                                  QUEUEMODE_START_PLAYING, GST_STATE_PAUSED))
        {
            msg_info("Got pause request, but URL FIFO is empty");
        }

        break;

      case GST_STATE_PLAYING:
        ret = gst_element_set_state(streamer_data.pipeline, GST_STATE_PAUSED);

        if(ret != GST_STATE_CHANGE_SUCCESS && ret != GST_STATE_CHANGE_ASYNC)
            msg_error(ENOSYS, LOG_ERR,
                      "Pause: unhandled gst_element_set_state() return code %d",
                      ret);

        break;

      case GST_STATE_NULL:
      case GST_STATE_VOID_PENDING:
        msg_error(ENOSYS, LOG_ERR,
                  "Pause: pipeline is in unhandled state %d", state);
        break;
    }
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

    if(!try_queue_next_stream(streamer_data.pipeline, &streamer_data,
                              QUEUEMODE_FORCE_SKIP, state))
        msg_info("Cannot play next, URL FIFO is empty");
}
