#ifndef GSTREAMERACTOR_H
#define GSTREAMERACTOR_H

#include "libsphactor.h"
#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _gstreameractor_t
{
    GstElement *playbin;  /* Our one and only element */

    gint n_video;          /* Number of embedded video streams */
    gint n_audio;          /* Number of embedded audio streams */
    gint n_text;           /* Number of embedded subtitle streams */

    gint current_video;    /* Currently playing video stream */
    gint current_audio;    /* Currently playing audio stream */
    gint current_text;     /* Currently playing subtitle stream */

    //GMainLoop *main_loop;  /* GLib's Main Loop */
    GMainContext *main_context; /* Glib's Main context */
};

typedef struct _gstreameractor_t gstreameractor_t;

static const char *gstreameractorcapabilities =
    "capabilities\n"
    "    data\n"
    "        name = \"URL\"\n"
    "        type = \"URL\"\n"
    "        options = \"\"\n"            // c = create file,  t = textfile, e = editable
    "        help = \"Load a video from url\"\n"
    "        value = \"\"\n"
    "        api_call = \"SET FILE\"\n"
    "        api_value = \"s\"\n"           // optional picture format used in zsock_send
    "inputs\n"
    "    input\n"
    "        type = \"OSC\"\n"
    "outputs\n"
    "    output\n"
    //TODO: Perhaps add NatNet output type so we can filter the data multiple times...
    "        type = \"OSC\"\n";


void * gstreameractor_new_helper(void *args);
gstreameractor_t * gstreameractor_new();
void gstreameractor_destroy(gstreameractor_t **self_p);

zmsg_t *gstreameractor_init(gstreameractor_t *self, sphactor_event_t *ev);
zmsg_t *gstreameractor_timer(gstreameractor_t *self, sphactor_event_t *ev);
zmsg_t *gstreameractor_api(gstreameractor_t *self, sphactor_event_t *ev);
zmsg_t *gstreameractor_socket(gstreameractor_t *self, sphactor_event_t *ev);
zmsg_t *gstreameractor_custom_socket(gstreameractor_t *self, sphactor_event_t *ev);
zmsg_t *gstreameractor_stop(gstreameractor_t *self, sphactor_event_t *ev);

zmsg_t * gstreameractor_handler(sphactor_event_t *ev, void *args);
zmsg_t * gstreameractor_handle_msg(gstreameractor_t *self, sphactor_event_t *ev);

#ifdef __cplusplus
} // end extern
#endif

#endif // GSTREAMERACTOR_H
