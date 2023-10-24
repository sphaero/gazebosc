#include "gstreameractor.h"

/* playbin flags */
typedef enum {
    GST_PLAY_FLAG_VIDEO         = (1 << 0), /* We want video output */
    GST_PLAY_FLAG_AUDIO         = (1 << 1), /* We want audio output */
    GST_PLAY_FLAG_TEXT          = (1 << 2)  /* We want subtitle output */
} GstPlayFlags;

void *
gstreameractor_new_helper(void *args)
{
    return (void *)gstreameractor_new();
}

gstreameractor_t *
gstreameractor_new()
{
    gstreameractor_t *self = (gstreameractor_t *) zmalloc (sizeof (gstreameractor_t));
    assert (self);

    /* Initialize GStreamer */
    gst_init (NULL, NULL);

    /* Create the elements */
    self->playbin = gst_element_factory_make ("playbin", "playbin");
    assert(self->playbin); // assert elements created

    return self;
}

/* Extract some metadata from the streams and print it on the screen */
static void analyze_streams (gstreameractor_t *data) {
    gint i;
    GstTagList *tags;
    gchar *str;
    guint rate;

    /* Read some properties */
    g_object_get (data->playbin, "n-video", &data->n_video, NULL);
    g_object_get (data->playbin, "n-audio", &data->n_audio, NULL);
    g_object_get (data->playbin, "n-text", &data->n_text, NULL);

    g_print ("%d video stream(s), %d audio stream(s), %d text stream(s)\n",
            data->n_video, data->n_audio, data->n_text);

    g_print ("\n");
    for (i = 0; i < data->n_video; i++) {
        tags = NULL;
        /* Retrieve the stream's video tags */
        g_signal_emit_by_name (data->playbin, "get-video-tags", i, &tags);
        if (tags) {
            g_print ("video stream %d:\n", i);
            gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &str);
            g_print ("  codec: %s\n", str ? str : "unknown");
            g_free (str);
            gst_tag_list_free (tags);
        }
    }

    g_print ("\n");
    for (i = 0; i < data->n_audio; i++) {
        tags = NULL;
        /* Retrieve the stream's audio tags */
        g_signal_emit_by_name (data->playbin, "get-audio-tags", i, &tags);
        if (tags) {
            g_print ("audio stream %d:\n", i);
            if (gst_tag_list_get_string (tags, GST_TAG_AUDIO_CODEC, &str)) {
                g_print ("  codec: %s\n", str);
                g_free (str);
            }
            if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
                g_print ("  language: %s\n", str);
                g_free (str);
            }
            if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &rate)) {
                g_print ("  bitrate: %d\n", rate);
            }
            gst_tag_list_free (tags);
        }
    }

    g_print ("\n");
    for (i = 0; i < data->n_text; i++) {
        tags = NULL;
        /* Retrieve the stream's subtitle tags */
        g_signal_emit_by_name (data->playbin, "get-text-tags", i, &tags);
        if (tags) {
            g_print ("subtitle stream %d:\n", i);
            if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
                g_print ("  language: %s\n", str);
                g_free (str);
            }
            gst_tag_list_free (tags);
        }
    }

    g_object_get (data->playbin, "current-video", &data->current_video, NULL);
    g_object_get (data->playbin, "current-audio", &data->current_audio, NULL);
    g_object_get (data->playbin, "current-text", &data->current_text, NULL);

    g_print ("\n");
    g_print ("Currently playing video stream %d, audio stream %d and text stream %d\n",
            data->current_video, data->current_audio, data->current_text);
    g_print ("Type any number and hit ENTER to select a different audio stream\n");
}

/* Process messages from GStreamer */
static gboolean handle_message (GstBus *bus, GstMessage *msg, gstreameractor_t *data) {
    GError *err;
    gchar *debug_info;

    switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
        gst_message_parse_error (msg, &err, &debug_info);
        g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
        g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
        g_clear_error (&err);
        g_free (debug_info);
        //g_main_loop_quit (data->main_loop);
        break;
    case GST_MESSAGE_EOS:
        g_print ("End-Of-Stream reached.\n");
        //g_main_loop_quit (data->main_loop);
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state, pending_state;
        gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
        if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
            if (new_state == GST_STATE_PLAYING) {
                /* Once we are in the playing state, analyze the streams */
                analyze_streams (data);
            }
        }
    } break;
    }

    /* We want to keep receiving messages */
    return TRUE;
}

zmsg_t *
gstreameractor_init(gstreameractor_t *self, sphactor_event_t *ev)
{
    assert(self);

    /* Set the URI to play */
    g_object_set (self->playbin, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_cropped_multilingual.webm", NULL);

    /* Set flags to show Audio and Video but ignore Subtitles */
    gint flags;
    g_object_get (self->playbin, "flags", &flags, NULL);
    flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO;
    flags &= ~GST_PLAY_FLAG_TEXT;
    g_object_set (self->playbin, "flags", flags, NULL);

    /* Set connection speed. This will affect some internal decisions of playbin */
    g_object_set (self->playbin, "connection-speed", 56, NULL);

    /* Add a bus watch, so we get notified when a message arrives */
    GstBus *bus = gst_element_get_bus (self->playbin);
    gst_bus_add_watch (bus, (GstBusFunc)handle_message, self);

    /* Start playing */
    GstStateChangeReturn ret = gst_element_set_state (self->playbin, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        zsys_error ("Unable to set the pipeline to the playing state.\n");
        gst_object_unref (self->playbin);
        return NULL;
    }
    // run one iteration
    g_main_context_iteration(self->main_context, false);

    return NULL;
}

zmsg_t *
gstreameractor_api(gstreameractor_t *self, sphactor_event_t *ev)
{
    char *cmd = zmsg_popstr(ev->msg);
    if ( streq(cmd, "SET FILE") )
    {
        char *filename = zmsg_popstr(ev->msg);
        // dispose the event msg we don't use it further
        zmsg_destroy(&ev->msg);

        // check the filename
        if ( strlen(filename) < 1)
        {
            zsys_error("Empty filename received", filename);
            zstr_free(&filename);
            zstr_free(&cmd);
            return NULL;
        }
        zstr_free(&filename);
        zstr_free(&cmd);
        return NULL;

    }

    if ( ev->msg ) zmsg_destroy(&ev->msg);
    zstr_free(&cmd);
    return NULL;
}

void
gstreameractor_destroy(gstreameractor_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        gstreameractor_t *self = *self_p;
        if (self->main_context )
            g_main_context_unref(self->main_context);
        if (self->playbin)
        {
            gst_element_set_state (self->playbin, GST_STATE_NULL);
            gst_object_unref (self->playbin);
        }
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

zmsg_t *
gstreameractor_timer(gstreameractor_t *self, sphactor_event_t *ev)
{
    return NULL;
}

zmsg_t *
gstreameractor_handler(sphactor_event_t *ev, void *args)
{
    assert(args);
    gstreameractor_t *self = (gstreameractor_t *)args;
    return gstreameractor_handle_msg(self, ev);
}

zmsg_t *
gstreameractor_handle_msg(gstreameractor_t *self, sphactor_event_t *ev)
{
    assert(self);
    assert(ev);
    zmsg_t *ret = NULL;
    // these calls don't require a python instance
    if (streq(ev->type, "API"))
    {
        ret = gstreameractor_api(self, ev);
    }
    else if (streq(ev->type, "DESTROY"))
    {
        gstreameractor_destroy(&self);
        return NULL;
    }
    else if (streq(ev->type, "INIT"))
    {
        ret = gstreameractor_init(self, ev);
    }
    else if ( streq(ev->type, "TIME") )
    {
        ret = gstreameractor_timer(self, ev);
    }
    /*else if ( streq(ev->type, "SOCK") )
    {
        ret = gstreameractor_socket(self, ev);
    }
    else if ( streq(ev->type, "FDSOCK") )
    {
        ret = gstreameractor_custom_socket(self, ev);
    }
    else if ( streq(ev->type, "STOP") )
    {
        return gstreameractor_stop(self, ev);
    }*/

    g_main_context_iteration(self->main_context, false);
    return ret;
}
