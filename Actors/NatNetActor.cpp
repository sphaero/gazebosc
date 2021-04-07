#include "NatNetActor.h"
#include <string>
#include <algorithm>
#include <time.h>

//static variable definitions
int* NatNet::NatNetVersion = new int[4]{0,0,0,0};
int* NatNet::ServerVersion = new int[4]{0,0,0,0};
std::vector<RigidBodyDescription> NatNet::rigidbody_descs;
std::vector<SkeletonDescription> NatNet::skeleton_descs;
std::vector<MarkerSetDescription> NatNet::markerset_descs;

const char * natnetCapabilities =
                                "capabilities\n"
                                "    motive_ip\n"
                                "        name = \"motive_ip\"\n"
                                "        type = \"string\"\n"
                                "        value = \"192.168.10.30\"\n"
                                "        api_call = \"SET HOST\"\n"
                                "        api_value = \"s\"\n"           // optional picture format used in zsock_send
                                "    sendTimeout\n"
                                "        name = \"sendTimeout\"\n"
                                "        type = \"int\"\n"
                                "        value = \"60\"\n"
                                "        api_call = \"SET TIMEOUT\"\n"
                                "        api_value = \"i\"\n"           // optional picture format used in zsock_send
                                "outputs\n"
                                "    output\n"
                                //TODO: Perhaps add NatNet output type so we can filter the data multiple times...
                                "        type = \"NatNet\"\n";

zmsg_t * NatNet::handleMsg( sphactor_event_t * ev ) {
    if ( streq(ev->type, "INIT") ) {
        //init capabilities
        sphactor_actor_set_capability((sphactor_actor_t*)ev->actor, zconfig_str_load(natnetCapabilities));

        //receive socket on port DATA_PORT
        //Test multicast group to receive data
        //TODO: Discover network interfaces, and have user select which to bind (drop-down)
        //         This might multicast receive on the wrong interface (OS primary)
        //          So if you want to try another one, manually add "ip;" after "udp://"
        //              example: "udp://192.168.10.124;..."
        std::string url = "udp://" + MULTICAST_ADDRESS + ":" + PORT_DATA_STR;
        DataSocket = zsock_new(ZMQ_DGRAM);
        //zsock_connect(DataSocket, "%s", url.c_str());
        zsock_bind(DataSocket, "%s", url.c_str());
        assert( DataSocket );
        dataFD = zsock_fd(DataSocket);
        int rc = sphactor_actor_poller_add((sphactor_actor_t*)ev->actor, DataSocket );
        assert(rc == 0);

        // receive socket on CMD_PORT
        url = "udp://*:"+ PORT_COMMAND_STR;
        CommandSocket = zsock_new_dgram(url.c_str());
        assert( CommandSocket );
        cmdFD = zsock_fd(CommandSocket);
        rc = sphactor_actor_poller_add((sphactor_actor_t*)ev->actor, CommandSocket );
        assert(rc == 0);

        // Initialize report timestamp
        zosc_t* msg = zosc_create("/report", "sh",
            "lastActive", (int64_t)0);

        sphactor_actor_set_custom_report_data((sphactor_actor_t*)ev->actor, msg);
    }
    else if ( streq(ev->type, "DESTROY") ) {
        if ( CommandSocket != NULL ) {
            sphactor_actor_poller_remove((sphactor_actor_t*)ev->actor, DataSocket);
            zsock_destroy(&CommandSocket);
            CommandSocket = NULL;
            cmdFD = -1;
        }
        if ( DataSocket != NULL ) {
            sphactor_actor_poller_remove((sphactor_actor_t*)ev->actor, DataSocket);
            zsock_destroy(&DataSocket);
            DataSocket = NULL;
            dataFD = -1;
        }

        //TODO: Figure out why this delete sometimes causes SIGABRT
        //          "pointer freed was never allocated"
        //delete this; ?? SIGABRT ??
        zmsg_destroy(&ev->msg);
        return NULL;
    }
    else if ( streq(ev->type, "API")) {
        //pop msg for command
        char * cmd = zmsg_popstr(ev->msg);
        if (cmd) {
            if ( streq(cmd, "SET HOST") ) {
                char *host_addr = zmsg_popstr(ev->msg);
                host = host_addr;
                zsys_info("SET HOST: %s", host_addr);
                zstr_free(&host_addr);

                // Say hello to our new host
                // send initial ping command
                sPacket PacketOut;
                PacketOut.iMessage = NAT_PING;
                PacketOut.nDataBytes = 0;
                int nTries = 3;
                while (nTries--) {
                    //int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
                    std::string url = host + ":" + PORT_COMMAND_STR;
                    zstr_sendm(CommandSocket, url.c_str());
                    int rc = zsock_send(CommandSocket, "b", (char *) &PacketOut, 4 + PacketOut.nDataBytes);
                    if (rc != 0) {
                        zsys_info("Sent ping to %s", url.c_str());
                    }
                }
            }

            zstr_free(&cmd);
        }

        zmsg_destroy(&ev->msg);

        return NULL;
    }
    else if ( streq(ev->type, "FDSOCK" ) )
    {
        //build report
        zosc_t * msg = zosc_create("/report", "sisisi",
                                   "Markers", markers.size(),
                                   "Rigidbodies", rigidbodies.size(),
                                   "Skeletons", skeletons.size());

        sphactor_actor_set_custom_report_data( (sphactor_actor_t*)ev->actor, msg );

        //zsys_info("GOT FDSOCK");

        // Get the socket...
        assert(ev->msg);
        zframe_t *frame = zmsg_pop(ev->msg);
        if (zframe_size(frame) == sizeof( void *) )
        {
            void *p = *(void **)zframe_data(frame);
            zsock_t* sock = (zsock_t*) zsock_resolve( p );
            if ( sock )
            {
                SOCKET sockFD = zsock_fd(sock);

                if ( sockFD == cmdFD ) {
                    // zsys_info("CMD SOCKET");
                    // Parse command
                    int addr_len;
                    int nDataBytesReceived;
                    char str[256];
                    sPacket PacketIn;
                    addr_len = sizeof(struct sockaddr);

                    //int rc = zmq_recv(CommandSocket, (char *) &PacketIn, sizeof(sPacket), 0);
                    zmsg_t* zmsg = zmsg_recv(CommandSocket);
                    if ( zmsg ) {
                        // pop the source address off
                        char* zStr = zmsg_popstr(zmsg);
                        if ( zStr ) {
                            //zsys_info("zStr: %s", zStr );
                            zstr_free(&zStr);
                        }

                        zframe_t *zframe = zmsg_pop(zmsg);
                        if ( zframe ) {
                            sPacket *packet = (sPacket*) zframe_data(zframe);
                            if ( packet->iMessage == 7 ) {
                                // zsys_info("Got frame of data");
                                size_t len = zframe_size(zframe);
                                Unpack((char **) &packet);

                                //zsys_info("rigidbodies after handled frame: %i, %i", rigidbodies.size(), rigidbody_descs.size());

                                // if there is a difference do natnet.questDescription(); to get up to date rigidbody descriptions and thus names
                                if (rigidbody_descs.size() != rigidbodies.size())
                                {
                                    if (sentRequest <= 0) {
                                        sendRequestDescription();
                                        sentRequest = 60; //1 second
                                    }
                                    rigidbodiesReady = false;
                                }

                                //get & check skeletons size
                                if (skeleton_descs.size() != skeletons.size())
                                {
                                    if (sentRequest <= 0) {
                                        sendRequestDescription();
                                        sentRequest = 60; //1 second
                                    }
                                    skeletonsReady = false;
                                }

                                if (sentRequest > 0) sentRequest--;

                                // re-append the zframe that was popped
                                // only send if definitions are updated
                                if ( skeletonsReady && rigidbodiesReady ) {
                                    // set timestamp of last sent packet in report
                                    zosc_t* msg = zosc_create("/report", "sh",
                                        "lastActive", (int64_t)clock());

                                    sphactor_actor_set_custom_report_data((sphactor_actor_t*)ev->actor, msg);

                                    zmsg_addmem(zmsg, (byte*)packet, len);
                                    zframe_destroy(&zframe);
                                    return zmsg;
                                }
                            }
                            else {
                                HandleCommand(packet);
                            }
                            zframe_destroy(&zframe);
                        }
                        zmsg_destroy(&zmsg);
                    }
                }
                else if ( sockFD == dataFD ) {
                    //zsys_info("DATA SOCKET");
                    zmsg_destroy(&ev->msg);

                    //TODO: Send data packet to connected natnet2osc clients
                    //          this will allow for multiple filters
                    //Parse packet
                    zmsg_t* zmsg = zmsg_recv(DataSocket);

                    // We're still unpacking the data here because it might contain definitions we need to store
                    // TODO: Find a way to optimize getting definitions
                    if ( zmsg ) {
                        // pop the source address off
                        char* zStr = zmsg_popstr(zmsg);
                        if ( zStr ) {
                            //zsys_info("zStr: %s", zStr );
                            zstr_free(&zStr);
                        }

                        // parse the remaining bytes as mocap data message
                        zframe_t *zframe = zmsg_pop(zmsg);
                        if (zframe) {
                            byte *data = zframe_data(zframe);
                            size_t len = zframe_size(zframe);
                            Unpack((char **) &data);

                            //zsys_info("rigidbodies after handled frame: %i, %i", rigidbodies.size(), rigidbody_descs.size());

                            // if there is a difference do natnet.questDescription(); to get up to date rigidbody descriptions and thus names
                            if (rigidbody_descs.size() != rigidbodies.size())
                            {
                                if (sentRequest <= 0) {
                                    sendRequestDescription();
                                    sentRequest = 60; //1 second
                                }
                                rigidbodiesReady = false;
                            }

                            //get & check skeletons size
                            if (skeleton_descs.size() != skeletons.size())
                            {
                                if (sentRequest <= 0) {
                                    sendRequestDescription();
                                    sentRequest = 60; //1 second
                                }
                                skeletonsReady = false;
                            }

                            if (sentRequest > 0) sentRequest--;

                            // re-append the zframe that was popped
                            // only send if definitions are updated
                            if ( skeletonsReady && rigidbodiesReady ) {
                                // set timestamp of last received packet in report
                                zosc_t* msg = zosc_create("/report", "sh",
                                    "lastActive", (int64_t)clock());

                                sphactor_actor_set_custom_report_data((sphactor_actor_t*)ev->actor, msg);

                                zmsg_addmem(zmsg, data, len);
                                return zmsg;
                            }
                        }
                        // nothing valid or not sent on, so clean up
                        zmsg_destroy(&zmsg);
                        return NULL;
                    }

                    return NULL;
                }
            }
        }
        else
            zsys_error("args is not a zsock instance");

        zmsg_destroy(&ev->msg);
        zframe_destroy(&frame);

        return NULL;
    }

    zmsg_destroy(&ev->msg);
    return NULL;
}

bool DecodeTimecode(unsigned int inTimecode, unsigned int inTimecodeSubframe, int* hour, int* minute, int* second, int* frame, int* subframe)
{
    bool bValid = true;

    *hour = (inTimecode>>24)&255;
    *minute = (inTimecode>>16)&255;
    *second = (inTimecode>>8)&255;
    *frame = inTimecode&255;
    *subframe = inTimecodeSubframe;

    return bValid;
}

bool TimecodeStringify(unsigned int inTimecode, unsigned int inTimecodeSubframe, char *Buffer, int BufferSize)
{
    bool bValid;
    int hour, minute, second, frame, subframe;
    bValid = DecodeTimecode(inTimecode, inTimecodeSubframe, &hour, &minute, &second, &frame, &subframe);

    zsys_sprintf(Buffer,BufferSize,"%2d:%2d:%2d:%2d.%d",hour, minute, second, frame, subframe);
    for(unsigned int i=0; i<strlen(Buffer); i++)
        if(Buffer[i]==' ')
            Buffer[i]='0';

    return bValid;
}

// NatNet implementation
bool NatNet::IPAddress_StringToAddr(char *szNameOrAddress, struct in_addr *Address) {
    return false;
}

char* NatNet::unpackMarkerSet(char* ptr, std::vector<Marker>& ref_markers)
{
    int nMarkers = 0;
    memcpy(&nMarkers, ptr, 4);
    ptr += 4;

    ref_markers.resize(nMarkers);

    for (int j = 0; j < nMarkers; j++)
    {
        Marker p;
        memcpy(&p[0], ptr, 4);
        ptr += 4;
        memcpy(&p[1], ptr, 4);
        ptr += 4;
        memcpy(&p[2], ptr, 4);
        ptr += 4;

        //TODO: Matrix, only used for scale previously
        //p = transform.preMult(p);

        ref_markers[j] = p;
    }

    return ptr;
}

char* NatNet::unpackRigidBodies(char* ptr, std::vector<RigidBody>& ref_rigidbodies)
{
    int major = NatNetVersion[0];
    int minor = NatNetVersion[1];

    //TODO: Matrix, only used for scale previously
    //ofQuaternion rot = transform.getRotate();

    int nRigidBodies = 0;
    memcpy(&nRigidBodies, ptr, 4);
    ptr += 4;

    ref_rigidbodies.resize(nRigidBodies);

    for (int j = 0; j < nRigidBodies; j++)
    {
        RigidBody& RB = ref_rigidbodies[j];

        glm::vec3 pp;
        glm::vec4 q;

        int ID = 0;
        memcpy(&ID, ptr, 4);
        ptr += 4;

        memcpy(&pp[0], ptr, 4);
        ptr += 4;

        memcpy(&pp[1], ptr, 4);
        ptr += 4;

        memcpy(&pp[2], ptr, 4);
        ptr += 4;

        memcpy(&q[0], ptr, 4);
        ptr += 4;

        memcpy(&q[1], ptr, 4);
        ptr += 4;

        memcpy(&q[2], ptr, 4);
        ptr += 4;

        memcpy(&q[3], ptr, 4);
        ptr += 4;

        RB.id = ID;
        RB.position = pp;
        RB.rotation = q;

        //TODO: Matrix, only used for scale previously
        //pp = transform.preMult(pp);
        //
        //ofMatrix4x4 mat;
        //mat.setTranslation(pp);
        //mat.setRotate(q * rot);
        //RB.matrix = mat;

        // associated marker positions
        int nRigidMarkers = 0;
        memcpy(&nRigidMarkers, ptr, 4);
        ptr += 4;

        int nBytes = nRigidMarkers * 3 * sizeof(float);
        float* markerData = (float*)malloc(nBytes);
        memcpy(markerData, ptr, nBytes);
        ptr += nBytes;

        if (major >= 2)
        {
            // associated marker IDs
            nBytes = nRigidMarkers * sizeof(int);
            ptr += nBytes;

            // associated marker sizes
            nBytes = nRigidMarkers * sizeof(float);
            ptr += nBytes;
        }

        RB.markers.resize(nRigidMarkers);

        for (int k = 0; k < nRigidMarkers; k++)
        {
            float x = markerData[k * 3];
            float y = markerData[k * 3 + 1];
            float z = markerData[k * 3 + 2];

            glm::vec3 pp(x, y, z);
            //TODO: Matrix, only used for scale previously
            //pp = transform.preMult(pp);
            RB.markers[k] = pp;
        }

        if (markerData) free(markerData);

        if (major >= 2)
        {
            // Mean marker error
            float fError = 0.0f;
            memcpy(&fError, ptr, 4);
            ptr += 4;

            RB.mean_marker_error = fError;
            RB._active = RB.mean_marker_error > 0;
        } else {
            RB.mean_marker_error = 0;
        }

        // 2.6 and later
        if (((major == 2) && (minor >= 6)) || (major > 2) || (major == 0))
        {
            // params
            short params = 0; memcpy(&params, ptr, 2); ptr += 2;
            bool bTrackingValid = params & 0x01; // 0x01 : rigid body was successfully tracked in this frame
        }

    }  // next rigid body

    return ptr;
}

void NatNet::Unpack( char ** pData ) {
    int major = NatNetVersion[0];
    int minor = NatNetVersion[1];

    //TODO: Matrix, only used for scale previously
    //Vec4 rotation = transform.getRotate();

    char *ptr = *pData;

    sPacket* packet = (sPacket*)*pData;

    //zsys_info("Begin Packet\n-------\n");

    // message ID
    int MessageID = 0;
    memcpy(&MessageID, ptr, 2); ptr += 2;
    //zsys_info("Message ID : %d\n", MessageID);

    // size
    int nBytes = 0;
    memcpy(&nBytes, ptr, 2); ptr += 2;
    //zsys_info("Byte count : %d\n", nBytes);

    if(MessageID == 7)      // FRAME OF MOCAP DATA packet
    {
        // temporary data containers
        std::vector<std::vector<Marker> > tmp_markers_set;
        std::vector<Skeleton> tmp_skeletons;
        std::vector<Marker> tmp_markers;
        std::vector<Marker> tmp_filtered_markers;
        std::vector<RigidBody> tmp_rigidbodies;

        // frame number
        int frameNumber = 0; memcpy(&frameNumber, ptr, 4); ptr += 4;
        //zsys_info("Frame # : %d\n", frameNumber);

        // number of data sets (markersets, rigidbodies, etc)
        int nMarkerSets = 0; memcpy(&nMarkerSets, ptr, 4); ptr += 4;
        //zsys_info("Marker Set Count : %d\n", nMarkerSets);

        tmp_markers_set.resize(nMarkerSets);

        for (int i=0; i < nMarkerSets; i++)
        {
            // Markerset name
            char szName[256];
            strcpy(szName, ptr);
            int nDataBytes = (int) strlen(szName) + 1;
            ptr += nDataBytes;
            //zsys_info("Model Name: %s\n", szName);

            ptr = unpackMarkerSet(ptr, tmp_markers_set[i]);
        }

        // unidentified markers
        ptr = unpackMarkerSet(ptr, tmp_markers);

        // rigid bodies
        ptr = unpackRigidBodies(ptr, tmp_rigidbodies);

        // skeletons (version 2.1 and later)
        if( ((major == 2)&&(minor>0)) || (major>2))
        {
            int nSkeletons = 0;
            memcpy(&nSkeletons, ptr, 4); ptr += 4;
            //zsys_info("Skeleton Count : %d\n", nSkeletons);

            tmp_skeletons.resize(nSkeletons);

            for (int j=0; j < nSkeletons; j++)
            {
                // skeleton id
                int skeletonID = 0;
                memcpy(&skeletonID, ptr, 4); ptr += 4;

                tmp_skeletons[j].id = skeletonID;

                ptr = unpackRigidBodies(ptr, tmp_skeletons[j].joints);
            } // next skeleton
        }

        // labeled markers (version 2.3 and later)
        if( ((major == 2)&&(minor>=3)) || (major>2))
        {
            int nLabeledMarkers = 0;
            memcpy(&nLabeledMarkers, ptr, 4); ptr += 4;
            //zsys_info("Labeled Marker Count : %d\n", nLabeledMarkers);
            for (int j=0; j < nLabeledMarkers; j++)
            {
                // id
                int ID = 0; memcpy(&ID, ptr, 4); ptr += 4;
                // x
                float x = 0.0f; memcpy(&x, ptr, 4); ptr += 4;
                // y
                float y = 0.0f; memcpy(&y, ptr, 4); ptr += 4;
                // z
                float z = 0.0f; memcpy(&z, ptr, 4); ptr += 4;
                // size
                float size = 0.0f; memcpy(&size, ptr, 4); ptr += 4;

                // 2.6 and later
                if( ((major == 2)&&(minor >= 6)) || (major > 2) || (major == 0) )
                {
                    // marker params
                    short params = 0; memcpy(&params, ptr, 2); ptr += 2;
                    bool bOccluded = params & 0x01;     // marker was not visible (occluded) in this frame
                    bool bPCSolved = params & 0x02;     // position provided by point cloud solve
                    bool bModelSolved = params & 0x04;  // position provided by model solve
                }

                //zsys_info("ID  : %d\n", ID);
                //zsys_info("pos : [%3.2f,%3.2f,%3.2f]\n", x,y,z);
                //zsys_info("size: [%3.2f]\n", size);

                glm::vec3 pp(x, y, z);
                //TODO: Matrix, only used for scale previously
                //pp = transform.preMult(pp);
                tmp_markers.push_back(pp);
            }
        }

        // Force Plate data (version 2.9 and later)
        if (((major == 2) && (minor >= 9)) || (major > 2))
        {
            int nForcePlates;
            memcpy(&nForcePlates, ptr, 4); ptr += 4;
            for (int iForcePlate = 0; iForcePlate < nForcePlates; iForcePlate++)
            {
                // ID
                int ID = 0; memcpy(&ID, ptr, 4); ptr += 4;
                //zsys_info("Force Plate : %d\n", ID);

                // Channel Count
                int nChannels = 0; memcpy(&nChannels, ptr, 4); ptr += 4;

                // Channel Data
                for (int i = 0; i < nChannels; i++)
                {
                    //zsys_info(" Channel %d : ", i);
                    int nFrames = 0; memcpy(&nFrames, ptr, 4); ptr += 4;
                    for (int j = 0; j < nFrames; j++)
                    {
                        float val = 0.0f;  memcpy(&val, ptr, 4); ptr += 4;
                        //zsys_info("%3.2f   ", val);
                    }
                    //zsys_info("\n");
                }
            }
        }

        // latency
        float latency = 0.0f; memcpy(&latency, ptr, 4);	ptr += 4;
        //zsys_info("latency : %3.3f\n", latency);
        this->latency = latency;

        // timecode
        unsigned int timecode = 0; 	memcpy(&timecode, ptr, 4);	ptr += 4;
        unsigned int timecodeSub = 0; memcpy(&timecodeSub, ptr, 4); ptr += 4;
        char szTimecode[128] = "";
        TimecodeStringify(timecode, timecodeSub, szTimecode, 128);

        // timestamp
        double timestamp = 0.0f;
        // 2.7 and later - increased from single to double precision
        if( ((major == 2)&&(minor>=7)) || (major>2))
        {
            memcpy(&timestamp, ptr, 8); ptr += 8;
        }
        else
        {
            float fTemp = 0.0f;
            memcpy(&fTemp, ptr, 4); ptr += 4;
            timestamp = (double)fTemp;
        }

        // frame params
        short params = 0;  memcpy(&params, ptr, 2); ptr += 2;
        bool bIsRecording = params & 0x01;                  // 0x01 Motive is recording
        bool bTrackedModelsChanged = params & 0x02;         // 0x02 Actively tracked model list has changed


        // end of data tag
        int eod = 0; memcpy(&eod, ptr, 4); ptr += 4;
        //zsys_info("End Packet\n-------------\n");

        // filter markers
        // TODO: Test re-implemented marker filtering (replaced remove_dups)
        if (duplicated_point_removal_distance > 0)
        {
            std::map<int, RigidBody>::iterator it =
                    this->rigidbodies.begin();
            while (it != this->rigidbodies.end())
            {
                RigidBody& RB = it->second;

                for (int i = 0; i < RB.markers.size(); i++)
                {
                    glm::vec3& v = RB.markers[i];
                    std::vector<Marker>::iterator it = std::remove_if(
                            tmp_filtered_markers.begin(), tmp_filtered_markers.end(),
                            remove_dups(v, duplicated_point_removal_distance));
                    tmp_filtered_markers.erase(it, tmp_filtered_markers.end());
                }

                it++;
            }
        }

        //Copy data to instance...
        this->latency = latency;
        this->frame_number = frameNumber;
        this->markers_set = tmp_markers_set;
        this->markers = tmp_markers;
        this->filtered_markers = tmp_filtered_markers;

        // fill the rigidbodies map
        {
            for (int i = 0; i < tmp_rigidbodies.size(); i++) {
                RigidBody &RB = tmp_rigidbodies[i];
                RigidBody &tRB = this->rigidbodies[RB.id];
                tRB = RB;
            }
        }
        {
            for (int i = 0; i < tmp_skeletons.size(); i++) {
                Skeleton &S = tmp_skeletons[i];
                Skeleton &tS = this->skeletons[S.id];
                tS = S;
            }
        }
    }
    else if(MessageID == 5) // Data Descriptions
    {
        std::vector<RigidBodyDescription> tmp_rigidbody_descs;
        std::vector<SkeletonDescription> tmp_skeleton_descs;
        std::vector<MarkerSetDescription> tmp_markerset_descs;

        // number of datasets
        int nDatasets = 0; memcpy(&nDatasets, ptr, 4); ptr += 4;
        //zsys_info("Dataset Count : %d\n", nDatasets);

        for(int i=0; i < nDatasets; i++)
        {
            //zsys_info("Dataset %d\n", i);

            int type = 0; memcpy(&type, ptr, 4); ptr += 4;
            //zsys_info("Type : %d\n", i, type);

            if(type == 0)   // markerset
            {
                MarkerSetDescription description;

                // name
                char szName[256];
                strcpy(szName, ptr);
                int nDataBytes = (int) strlen(szName) + 1;
                ptr += nDataBytes;
                //zsys_info("Markerset Name: %s\n", szName);
                description.name = szName;

                // marker data
                int nMarkers = 0; memcpy(&nMarkers, ptr, 4); ptr += 4;
                //zsys_info("Marker Count : %d\n", nMarkers);

                for(int j=0; j < nMarkers; j++)
                {
                    char szName[256];
                    strcpy(szName, ptr);
                    int nDataBytes = (int) strlen(szName) + 1;
                    ptr += nDataBytes;
                    //zsys_info("Marker Name: %s\n", szName);
                    description.marker_names.push_back(szName);
                }
                tmp_markerset_descs.push_back(description);
            }
            else if(type ==1)   // rigid body
            {
                RigidBodyDescription description;

                if(major >= 2)
                {
                    // name
                    char szName[MAX_NAMELENGTH];
                    strcpy(szName, ptr);
                    ptr += strlen(ptr) + 1;
                    //zsys_info("Name: %s\n", szName);
                    description.name = szName;
                }

                int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                //zsys_info("ID : %d\n", ID);
                description.id = ID;

                int parentID = 0; memcpy(&parentID, ptr, 4); ptr +=4;
                //zsys_info("Parent ID : %d\n", parentID);
                description.parent_id = parentID;

                float xoffset = 0; memcpy(&xoffset, ptr, 4); ptr +=4;
                //zsys_info("X Offset : %3.2f\n", xoffset);

                float yoffset = 0; memcpy(&yoffset, ptr, 4); ptr +=4;
                //zsys_info("Y Offset : %3.2f\n", yoffset);

                float zoffset = 0; memcpy(&zoffset, ptr, 4); ptr +=4;
                //zsys_info("Z Offset : %3.2f\n", zoffset);

                description.offset[0] = xoffset;
                description.offset[1] = xoffset;
                description.offset[2] = xoffset;

                tmp_rigidbody_descs.push_back(description);
            }
            else if(type ==2)   // skeleton
            {
                SkeletonDescription description;

                char szName[MAX_NAMELENGTH];
                strcpy(szName, ptr);
                ptr += strlen(ptr) + 1;
                //zsys_info("Name: %s\n", szName);
                description.name = szName;

                int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                //zsys_info("ID : %d\n", ID);
                description.id = ID;

                int nRigidBodies = 0; memcpy(&nRigidBodies, ptr, 4); ptr +=4;
                //zsys_info("RigidBody (Bone) Count : %d\n", nRigidBodies);
                description.joints.resize(nRigidBodies);

                for(int i=0; i< nRigidBodies; i++)
                {
                    if(major >= 2)
                    {
                        // RB name
                        char szName[MAX_NAMELENGTH];
                        strcpy(szName, ptr);
                        ptr += strlen(ptr) + 1;
                        //zsys_info("Rigid Body Name: %s\n", szName);
                        description.joints[i].name = szName;
                    }

                    int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                    //zsys_info("RigidBody ID : %d\n", ID);
                    description.joints[i].id = ID;

                    int parentID = 0; memcpy(&parentID, ptr, 4); ptr +=4;
                    //zsys_info("Parent ID : %d\n", parentID);
                    description.joints[i].parent_id = parentID;

                    float xoffset = 0; memcpy(&xoffset, ptr, 4); ptr +=4;
                    //zsys_info("X Offset : %3.2f\n", xoffset);

                    float yoffset = 0; memcpy(&yoffset, ptr, 4); ptr +=4;
                    //zsys_info("Y Offset : %3.2f\n", yoffset);

                    float zoffset = 0; memcpy(&zoffset, ptr, 4); ptr +=4;
                    //zsys_info("Z Offset : %3.2f\n", zoffset);

                    description.joints[i].offset[0] = xoffset;
                    description.joints[i].offset[0] = yoffset;
                    description.joints[i].offset[0] = zoffset;
                }
                tmp_skeleton_descs.push_back(description);
            }

        }   // next dataset

        //zsys_info("End Packet\n-------------\n");

        //Store data
        this->markerset_descs = tmp_markerset_descs;
        this->rigidbody_descs = tmp_rigidbody_descs;
        this->skeleton_descs = tmp_skeleton_descs;
    }
    else
    {
        zsys_info("Unrecognized Packet Type: %i\n", MessageID);
    }
}

int NatNet::GetLocalIPAddresses(unsigned long Addresses[], int nMax) {
    return 0;
}

void NatNet::sendRequestDescription() {
    zsys_info("requesting description");
    sPacket packet;
    packet.iMessage = NAT_REQUEST_MODELDEF;
    packet.nDataBytes = 0;

    std::string url = host+":"+PORT_COMMAND_STR;
    zstr_sendm(CommandSocket, url.c_str());
    int rc = zsock_send(CommandSocket,  "b", (char*)&packet, 4 + packet.nDataBytes);
    if(rc != 0)
    {
        zsys_info("Socket error sending command: NAT_REQUEST_MODELDEF");
    }
}

int NatNet::SendCommand(char* szCommand) {
    // reset global result
    gCommandResponse = -1;

    // format command packet
    sPacket commandPacket;
    strcpy(commandPacket.Data.szData, szCommand);
    commandPacket.iMessage = NAT_REQUEST;
    commandPacket.nDataBytes = (int)strlen(commandPacket.Data.szData) + 1;

    //TODO: Check if replacement works...
    //int iRet = sendto(CommandSocket, (char *)&commandPacket, 4 + commandPacket.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
    std::string url = host+":"+PORT_COMMAND_STR;
    zstr_sendm(CommandSocket, url.c_str());
    int rc = zsock_send(CommandSocket,  "b", (char*)&commandPacket, 4 + commandPacket.nDataBytes);
    if(rc != 0)
    {
        zsys_info("Socket error sending command: %s", szCommand);
    }

    return gCommandResponse;
}

void NatNet::HandleCommand( sPacket *PacketIn ) {
    zsys_info("Message ID: %i", PacketIn->iMessage);
    // handle command
    switch (PacketIn->iMessage)
    {
        case NAT_MODELDEF:
            Unpack((char**)&PacketIn);
            rigidbodiesReady = true;
            skeletonsReady = true;
            break;
        case NAT_FRAMEOFDATA:
            Unpack((char**)&PacketIn);
            break;
        case NAT_PINGRESPONSE:
            for(int i=0; i<4; i++)
            {
                NatNetVersion[i] = (int)PacketIn->Data.Sender.NatNetVersion[i];
                ServerVersion[i] = (int)PacketIn->Data.Sender.Version[i];
            }
            break;
        case NAT_RESPONSE:
            gCommandResponseSize = PacketIn->nDataBytes;
            if(gCommandResponseSize==4)
                memcpy(&gCommandResponse, &PacketIn->Data.lData[0], gCommandResponseSize);
            else
            {
                memcpy(&gCommandResponseString[0], &PacketIn->Data.cData[0], gCommandResponseSize);
                zsys_info("Response : %s", gCommandResponseString);
                gCommandResponse = 0;   // ok
            }
            break;
        case NAT_UNRECOGNIZED_REQUEST:
            zsys_info("[Client] received 'unrecognized request'\n");
            gCommandResponseSize = 0;
            gCommandResponse = 1;       // err
            break;
        case NAT_MESSAGESTRING:
            zsys_info("[Client] Received message: %s\n", PacketIn->Data.szData);
            break;
    }
}
