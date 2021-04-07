#include "ModPlayerActor.h"
static SDL_AudioSpec fmt;

// callback audio
static void mixaudio(void *userdata, Uint8 *stream, int len)
{
    static_cast<ModPlayerActor *>(userdata)->mixAudio(stream, len);
}

const char * modplayercapabilities =
        "capabilities\n"
        "    modfile\n"
        "        type = \"filename\"\n"
        "        value = \"\"\n"
        "        api_call = \"SET MOD\"\n"
        "        api_value = \"s\"\n"           // optional picture format used in zsock_send
        "    playback\n"
        "        type = \"mediacontrol\"\n"
        "outputs\n"
        "    output\n"
        //TODO: Perhaps add NatNet output type so we can filter the data multiple times...
        "        type = \"OSC\"\n";

void
ModPlayerActor::mixAudio(Uint8 *stream, int len)
{
    trackbuf_state1.nb_of_state = 0;
    hxcmod_fillbuffer(&modctx, (msample*)stream, len / 4, &trackbuf_state1);
}

int
ModPlayerActor::queueAudio()
{
    if ( this->modctx.mod_loaded == 1 )
    {
        trackbuf_state1.nb_of_state = 0;
        /* TODO: This doesn't work correctly yet. The idea is we calculate the time
         * it takes to process one row, this time is used for the actor timeout so
         * we have events on every row.
         * The formula is not easy as it's not very clear how tempo and speed is related
         * to the row time. In openMPT it's documented somewhat:
         * https://wiki.openmpt.org/Manual:_Song_Properties#Tempo_Mode
         * I've used this but it doens't work as expected. The queued buffer slowly
         * decreases so we're not submitting data fast enough?
         * Perhaps our calculation of the buffer is incorrect or the calculation
         * of the time on each row is incorrect.
         */

        // calculate the required buffer size
        // fileSize = (bitsPerSample * samplesPerSecond * channels * duration) / 8;
        // we double the buffer size to prevent underrun
        // see MPT documentation about timing, that's where the 2500ms comes from
        int duration = (2500/this->modctx.bpm)*this->modctx.song.speed;
        unsigned int buffersize = (16*48000*2*duration)/1000/8;
        unsigned int maxbuffer = buffersize * 4;
        // always try to fill the buffer but not more
        unsigned int qs = SDL_GetQueuedAudioSize(audiodev);
        if (qs <= 8192)
        {
            if (qs <= 0)
            {
                zsys_warning("Buffer underrun (or just starting)!");
                qs = 0;
            }
            // fill the buffer to the max
            buffersize = maxbuffer - qs;
        }
        //zsys_info("bs %i, qs %i, mb %i",buffersize,qs, maxbuffer);
        else if ( qs > maxbuffer )
        {
            zsys_warning("Buffer overrun!");
            buffersize = 4;
        }
        else if ( qs <= maxbuffer )
        {
            unsigned int fillsize = maxbuffer - qs;
            if ( buffersize > fillsize )
                buffersize = fillsize;
        }
#ifdef _MSC_VER
        msample *stream = (msample *)_malloca( buffersize * sizeof(msample) );
        hxcmod_fillbuffer(&modctx, stream, buffersize / 4, &trackbuf_state1);
        int rc = SDL_QueueAudio(audiodev, stream, buffersize);
        if (rc < 0 )
            zsys_error("SDL_QueueAudio failed: %s\n", SDL_GetError());
        _freea(stream);
#else
        msample stream[buffersize];
        hxcmod_fillbuffer(&modctx, (msample*)stream, buffersize / 4, &trackbuf_state1);
        int rc = SDL_QueueAudio(audiodev, stream, buffersize);
        if (rc < 0 )
            zsys_error("SDL_QueueAudio failed: %s\n", SDL_GetError());
#endif
        return SDL_GetQueuedAudioSize(audiodev);
    }
    return -1;
}

zmsg_t *
ModPlayerActor::getPatternEventMsg()
{
    tracker_state *state = this->trackbuf_state1.track_state_buf;
    zosc_t *oscm = zosc_create("/patternevent", "iiiiii",
                                      state->cur_pattern_pos,
                                      state->cur_pattern_table_pos,
                                      state->tracks[0].cur_period,   // note
                                      state->tracks[0].instrument_number,
                                      state->tracks[0].cur_effect,
                                      state->tracks[0].cur_parameffect
                                     );
    assert(oscm);
    //append rest of the tracks
    for (int i=1;i<state->number_of_tracks;i++)
    {
        zosc_append(oscm, "iiii",
                          state->tracks[i].cur_period,   // note
                          state->tracks[i].instrument_number,
                          state->tracks[i].cur_effect,
                          state->tracks[i].cur_parameffect
                        );
    }
    zmsg_t *ret = zmsg_new();
    zframe_t *data = zosc_packx(&oscm);
    zmsg_append(ret, &data);
    return ret;
}


zmsg_t *
ModPlayerActor::handleInit( sphactor_event_t *event )
{
    //init capabilities
    sphactor_actor_set_capability((sphactor_actor_t*)event->actor, zconfig_str_load(modplayercapabilities));
    sphactor_actor_set_timeout( (sphactor_actor_t*)event->actor, (2500/144)*31);
    // init hxcmod player
    hxcmod_init(&this->modctx);

    if ( event->msg ) zmsg_destroy(&event->msg);
    return nullptr;
}

zmsg_t *
ModPlayerActor::handleAPI(sphactor_event_t *event)
{
    char *cmd = zmsg_popstr(event->msg);
    if ( streq(cmd, "SET MOD") )
    {
        char *file = zmsg_popstr(event->msg);
        if (strlen(file) )
        {
            if ( this->modctx.mod_loaded == 1 )
            {
                hxcmod_unload( &this->modctx );
                // free modfile
                free( this->modfile );
            }

            FILE *f;
            int filesize = 0;
            f = fopen(file,"rb");
            if ( f == NULL)
                return NULL;
            fseek(f,0,SEEK_END);
            filesize = ftell(f);
            fseek(f,0,SEEK_SET);
            this->modfile = (unsigned char *)malloc(filesize);
            fread(this->modfile,filesize,1,f);
            hxcmod_setcfg(&this->modctx, SAMPLERATE, 0, 0);
            hxcmod_load(&this->modctx,(void*)this->modfile,filesize);
            fclose(f);

            if ( this->modctx.mod_loaded == 1 )
            {
                zosc_t *msg = zosc_create("/loaded", "sssisisi",
                                          "title", this->modctx.song.title,
                                          "length", int(this->modctx.song.length),
                                          "speed", int(this->modctx.song.speed),
                                          "bpm", int(this->modctx.bpm));
                sphactor_actor_set_custom_report_data((sphactor_actor_t*)event->actor, msg);
                sphactor_actor_set_timeout( (sphactor_actor_t*)event->actor, (2500/this->modctx.bpm)*this->modctx.song.speed);

                if (audiodev == -1 ) // init audio
                {

                    fmt.freq = 48000;
                    fmt.format = AUDIO_S16;
                    fmt.channels = 2;
                    fmt.samples = NBSTEREO16BITSAMPLES/4;
                    fmt.callback = NULL;
                    fmt.userdata = NULL;

                    memset(&this->trackbuf_state1,0,sizeof(tracker_buffer_state));
                    trackbuf_state1.nb_max_of_state = 100;
                    trackbuf_state1.track_state_buf = (tracker_state *)malloc(sizeof(tracker_state) * trackbuf_state1.nb_max_of_state);
                    memset(trackbuf_state1.track_state_buf,0,sizeof(tracker_state) * trackbuf_state1.nb_max_of_state);
                    trackbuf_state1.sample_step = ( NBSTEREO16BITSAMPLES ) / trackbuf_state1.nb_max_of_state;
                    audiodev = SDL_OpenAudioDevice(NULL, 0, &fmt, NULL, 0);
                    if ( audiodev < 1 )
                    {
                        zsys_error("Cannot open Audio: %s\n", SDL_GetError());
                    }
                }
            }
            else
            {
                hxcmod_unload(&this->modctx);
                free(this->modfile);
                zosc_t *msg = zosc_create("/report", "ss", "error loading file:", file);
                sphactor_actor_set_custom_report_data((sphactor_actor_t*)event->actor, msg);
            }
        }
        zstr_free(&file);
    }
    else if ( streq(cmd, "PLAY") )
    {
        if ( this->modctx.mod_loaded == 1)
            this->playing = true;
    }
    else if ( streq(cmd, "PAUSE") )
        this->playing = false;
    else if ( streq(cmd, "BACK") )
    {
        this->modctx.tablepos = 0;
    }

    if ( cmd )
        zstr_free(&cmd);

    zmsg_destroy(&event->msg);
    return NULL;
}

zmsg_t *
ModPlayerActor::handleTimer(sphactor_event_t *event)
{
    if ( this->modctx.mod_loaded == 1 && this->playing )
    {
        queueAudio();
        SDL_PauseAudioDevice(audiodev, 0);
        zosc_t *msg = zosc_create("/loaded", "sssisisisisi",
                                  "title", this->modctx.song.title,
                                  "length", int(this->modctx.song.length),
                                  "speed", int(this->modctx.song.speed),
                                  "bpm", int(this->modctx.bpm),
                                  "position", this->modctx.tablepos,
                                  "pattern", this->modctx.song.patterntable[this->modctx.tablepos] );
        sphactor_actor_set_custom_report_data((sphactor_actor_t*)event->actor, msg);
        // determine next timeout based on speed and bpm of the song! 2500ms/bpm*speed
        sphactor_actor_set_timeout( (sphactor_actor_t*)event->actor, (2500/this->modctx.bpm)*this->modctx.song.speed);
        return getPatternEventMsg();
    }
    if ( event->msg ) zmsg_destroy(&event->msg);
    return nullptr;
}

zmsg_t *
ModPlayerActor::handleStop(sphactor_event_t *event)
{
    if ( this->modctx.mod_loaded == 1 )
        free( this->modfile ); // free modfile

    hxcmod_unload(&this->modctx);
    if (audiodev != -1 )
        free( trackbuf_state1.track_state_buf ); // free tracker state buf

    SDL_CloseAudio();
    if ( event->msg ) zmsg_destroy(&event->msg);
    return NULL;
}
