#include "libsphactor.h"
#include "PulseActor.h"

const char * pulseCapabilities =
                                "capabilities\n"
                                "    timeout\n"
                                "        type = \"int\"\n"
                                "        value = \"1000\"\n"
                                "        min = \"1\"\n"
                                "        max = \"10000\"\n"
                                "        step = \"1\"\n"
                                "        api_call = \"SET TIMEOUT\"\n"
                                "        api_value = \"i\"\n"           // optional picture format used in zsock_send
                                "    someFloat\n"
                                "        name = \"someFloat\"\n"
                                "        type = \"float\"\n"
                                "        value = \"1.0\"\n"
                                "        min = \"0\"\n"
                                "        max = \"10\"\n"
                                "        step = \"0\"\n"
                                "    someText\n"
                                "        name = \"someText\"\n"
                                "        type = \"string\"\n"
                                "        value = \"Hello world!\"\n"
                                "        max = \"64\"\n"
                                "outputs\n"
                                "    output\n"
                                "        type = \"OSC\"\n";

zmsg_t * Pulse::handleInit( sphactor_event_t *ev ) {
    sphactor_actor_set_capability((sphactor_actor_t*)ev->actor, zconfig_str_load(pulseCapabilities));
    return Sphactor::handleInit(ev);
}

zmsg_t * Pulse::handleTimer( sphactor_event_t *ev ) {
    zosc_t * osc = zosc_create("/pulse", "s", "PULSE");

    zmsg_t *msg = zmsg_new();
    zframe_t *frame = zframe_new(zosc_data(osc), zosc_size(osc));
    zmsg_append(msg, &frame);

    // clean up
    zframe_destroy(&frame);
    zmsg_destroy(&ev->msg);

    // publish new msg
    return msg;
}
