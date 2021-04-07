#include "libsphactor.h"
#include "NatNet2OSCActor.h"
#include "NatNetActor.h"
#include <map>
#include <algorithm>

//TODO: Add filter options
const char * natNet2OSCCapabilities = "capabilities\n"
                                "    markers\n"
                                "        name = \"markers\"\n"
                                "        type = \"bool\"\n"
                                "        value = \"False\"\n"
                                "        api_call = \"SET MARKERS\"\n"
                                "        api_value = \"s\"\n"
                                "    rigidbodies\n"
                                "        name = \"rigidbodies\"\n"
                                "        type = \"bool\"\n"
                                "        value = \"False\"\n"
                                "        api_call = \"SET RIGIDBODIES\"\n"
                                "        api_value = \"s\"\n"
                                "    skeletons\n"
                                "        name = \"skeletons\"\n"
                                "        type = \"bool\"\n"
                                "        value = \"False\"\n"
                                "        api_call = \"SET SKELETONS\"\n"
                                "        api_value = \"s\"\n"
                                "    rbVelocities\n"
                                "        name = \"rbVelocities\"\n"
                                "        type = \"bool\"\n"
                                "        value = \"False\"\n"
                                "        api_call = \"SET VELOCITIES\"\n"
                                "        api_value = \"s\"\n"
                                "    hierarchy\n"
                                "        name = \"hierarchy\"\n"
                                "        type = \"bool\"\n"
                                "        value = \"True\"\n"
                                "        api_call = \"SET HIERARCHY\"\n"
                                "        api_value = \"s\"\n"
                                "   sendSkeletonDefinitions\n"
                                "        name = \"sendSkeletonDefinitions\"\n"
                                "        type = \"bool\"\n"
                                "        value = \"False\"\n"
                                "        api_call = \"SET SKELETONDEF\"\n"
                                "        api_value = \"s\"\n"
                                "inputs\n"
                                "    input\n"
                                "        type = \"NatNet\"\n" //TODO: NatNet input?
                                "outputs\n"
                                "    output\n"
                                "        type = \"OSC\"\n";

zmsg_t *
NatNet2OSC::handleMsg( sphactor_event_t *ev ) {
    if ( streq(ev->type, "INIT")) {
        sphactor_actor_set_capability((sphactor_actor_t*)ev->actor, zconfig_str_load(natNet2OSCCapabilities));
    }
    else if ( streq( ev->type, "DESTROY")) {
        //TODO: Figure out why this delete sometimes causes SIGABRT
        //          "pointer freed was never allocated"
        //delete this;
        zmsg_destroy(&ev->msg);
        return NULL;
    }
    else if ( streq(ev->type, "SOCK")) {
        //zsys_info("SOCK");

        //TODO: unpack msg and parse intozStr OSC
        zframe_t *zframe = zmsg_pop(ev->msg);
        if (zframe) {
            byte *data = zframe_data(zframe);
            Unpack((char **) &data);
            zframe_destroy(&zframe);

            //Send Frame
            zmsg_t *oscMsg = zmsg_new();

            //markers
            if ( sendMarkers ) {
                for (int i = 0; i < markers.size(); i++) {
                    zosc_t *osc = zosc_create("/marker", "ifff", i, markers[i][0], markers[i][1], markers[i][2]);
                    zmsg_add(oscMsg, zosc_pack(osc));
                    //TODO: clean up osc* ?
                }
            }

            //rigidbodies
            if ( sendRigidbodies )
                addRigidbodies(oscMsg);

            //skeletons
            if ( sendSkeletons )
                addSkeletons(oscMsg);

            if ( zmsg_content_size(oscMsg) != 0 ){
                //zsys_info("Sending zmsg of size: %i", zmsg_content_size(oscMsg));
                zmsg_destroy(&ev->msg);
                return oscMsg;
            }

            // Nothing to send, destroy and return NULL
            zmsg_destroy(&oscMsg);
        }

        zmsg_destroy(&ev->msg);
        return NULL;
    }
    else if ( streq(ev->type, "API")) {
        //pop msg for command
        char * cmd = zmsg_popstr(ev->msg);
        if (cmd) {
            if ( streq(cmd, "SET MARKERS") ) {
                char * value = zmsg_popstr(ev->msg);
                sendMarkers = streq( value, "True");
                //zsys_info("Got: %s, set to %s", value, sendMarkers ? "True" : "False");
            }
            else if ( streq(cmd, "SET RIGIDBODIES") ) {
                char * value = zmsg_popstr(ev->msg);
                sendRigidbodies = streq( value, "True");
                //zsys_info("Got: %s, set to %s", value, sendRigidbodies ? "True" : "False");
            }
            else if ( streq(cmd, "SET SKELETONS") ) {
                char * value = zmsg_popstr(ev->msg);
                sendSkeletons = streq( value, "True");
                //zsys_info("Got: %s, set to %s", value, sendSkeletons ? "True" : "False");
            }
            else if ( streq(cmd, "SET VELOCITIES") ) {
                char * value = zmsg_popstr(ev->msg);
                sendVelocities = streq( value, "True");
                //zsys_info("Got: %s, set to %s", value, sendVelocities ? "True" : "False");
            }
            else if ( streq(cmd, "SET HIERARCHY") ) {
                char * value = zmsg_popstr(ev->msg);
                sendHierarchy = streq( value, "True");
                //zsys_info("Got: %s, set to %s", value, sendHierarchy ? "True" : "False");
            }
            else if ( streq(cmd, "SET SKELETONDEF") ) {
                char * value = zmsg_popstr(ev->msg);
                sendSkeletonDefinitions = streq( value, "True");
                //zsys_info("Got: %s, set to %s", value, sendSkeletonDefinitions ? "True" : "False");
            }

            zstr_free(&cmd);
        }

        zmsg_destroy(&ev->msg);
        return NULL;
    }

    return ev->msg;
}

void NatNet2OSC::addRigidbodies(zmsg_t *zmsg)
{
    for (int i = 0; i < rigidbodies.size(); i++)
    {
        RigidBodyDescription rbd = NatNet::rigidbody_descs[i];
        RigidBody &RB = rigidbodies[rbd.id];

        // Decompose to get the different elements
        glm::vec3 position = RB.position;
        glm::quat rotation;
        rotation.x = RB.rotation[0];
        rotation.y = RB.rotation[1];
        rotation.z = RB.rotation[2];
        rotation.w = RB.rotation[3];

        //we're going to fetch or create this
        //TODO: TEST re-implemented rigidbody histories for velocity data
        RigidBodyHistory *rb;

        //Get or create rigidbodyhistory
        bool found = false;
        for( int r = 0; r < rbHistory.size(); ++r )
        {
            if ( rbHistory[r].rigidBodyId == rbd.id )
            {
                rb = &rbHistory[r];
                found = true;
            }
        }

        if ( !found )
        {
            rb = new RigidBodyHistory( rbd.id, position, rotation );
            rbHistory.push_back(*rb);
        }

        glm::vec3 velocity;
        glm::vec3 angularVelocity;

        if ( rb->firstRun )
        {
            rb->currentDataPoint = 0;
            rb->firstRun = false;
        }
        else
        {
            //TODO: Get invFPS from timeout
            float invFPS = ( 1.0f / 60.0f );
            if ( rb->currentDataPoint < 2 * SMOOTHING + 1 )
            {
                rb->velocities[rb->currentDataPoint] = ( position - rb->previousPosition ) * invFPS;
                glm::vec3 diff = glm::eulerAngles( rb->previousOrientation * glm::inverse(rotation) );
                rb->angularVelocities[rb->currentDataPoint] = ( diff * invFPS );

                rb->currentDataPoint++;
            }
            else
            {
                int count = 0;
                int maxDist = SMOOTHING;
                glm::vec3 totalVelocity;
                glm::vec3 totalAngularVelocity;
                //calculate smoothed velocity
                for( int x = 0; x < SMOOTHING * 2 + 1; ++x )
                {
                    //calculate integer distance from "center"
                    //above - maxDist = influence of data point
                    int dist = abs( x - SMOOTHING );
                    int infl = ( maxDist - dist ) + 1;

                    //add all
                    totalVelocity += rb->velocities[x] * glm::vec3(infl);
                    totalAngularVelocity += rb->angularVelocities[x] * glm::vec3(infl);
                    //count "influences"
                    count += infl;
                }

                //divide by total data point influences
                velocity = totalVelocity / glm::vec3(count);
                angularVelocity = totalAngularVelocity / glm::vec3(count);

                for( int x = 0; x < rb->currentDataPoint - 1; ++x )
                {
                    rb->velocities[x] = rb->velocities[x+1];
                    rb->angularVelocities[x] = rb->angularVelocities[x+1];
                }
                rb->velocities[rb->currentDataPoint-1] = ( position - rb->previousPosition ) * invFPS;

                glm::vec3 diff = glm::eulerAngles(( rb->previousOrientation * glm::inverse(rotation) ));
                rb->angularVelocities[rb->currentDataPoint-1] = ( diff * invFPS );
            }

            rb->previousPosition = position;
            rb->previousOrientation = rotation;
        }

        std::string address = "/rigidBody";
        if ( sendHierarchy ) {
            address += "/"+NatNet::rigidbody_descs[i].name;
        }

        zosc_t *oscMsg = zosc_create(address.c_str(), "isfffffff",
                                     rbd.id,
                                     NatNet::rigidbody_descs[i].name.c_str(),
                                     position.x,
                                     position.y,
                                     position.z,
                                     rotation.x,
                                     rotation.y,
                                     rotation.z,
                                     rotation.w
        );

        if ( sendVelocities )
        {
            zosc_append(oscMsg, "ffffff",
                        //velocity over SMOOTHING * 2 + 1 frames
                        velocity.x * 1000, velocity.y * 1000, velocity.z * 1000,
                        //angular velocity (euler), also smoothed
                        angularVelocity.x * 1000, angularVelocity.y * 1000, angularVelocity.z * 1000 );
        }

        zosc_append(oscMsg, "i", (RB.isActive() ? 1 : 0));

        zmsg_add(zmsg, zosc_pack(oscMsg));
    }
}

void NatNet2OSC::addSkeletons(zmsg_t *zmsg)
{
    for (int j = 0; j < skeletons.size(); j++)
    {
        const Skeleton &S = skeletons[NatNet::skeleton_descs[j].id];
        std::vector<RigidBodyDescription> rbd = NatNet::skeleton_descs[j].joints;

        if ( sendHierarchy )
        {
            for (int i = 0; i < S.joints.size(); i++)
            {
                RigidBody RB = S.joints[i];
                std::string address = "/skeleton/" + NatNet::skeleton_descs[j].name + "/" + rbd[i].name;

                // Get the matirx
                //ofMatrix4x4 matrix = RB.matrix;

                // Decompose to get the different elements
                //Vec3 position;
                //Vec4 rotation;
                //Vec3 scale;
                //Vec4 so;
                //matrix.decompose(position, rotation, scale, so);

                zosc_t *oscMsg = zosc_create( address.c_str(), "sfffffff",
                                              rbd[i].name.c_str(),
                                              RB.position.x,
                                              RB.position.y,
                                              RB.position.z,
                                              RB.rotation.x,
                                              RB.rotation.y,
                                              RB.rotation.z,
                                              RB.rotation.w
                );

                //needed for skeleton retargeting
                if ( sendSkeletonDefinitions )
                {
                    zosc_append(oscMsg, "ifff",
                                rbd[i].parent_id,
                                rbd[i].offset.x,
                                rbd[i].offset.y,
                                rbd[i].offset.z
                    );
                }

                zmsg_add(zmsg, zosc_pack(oscMsg));
            }
        }
        else
        {
            std::string address = "/skeleton";

            zosc_t * oscMsg = zosc_create(address.c_str(), "si", NatNet::skeleton_descs[j].name.c_str(), S.id );

            for (int i = 0; i < S.joints.size(); i++)
            {
                RigidBody RB = S.joints[i];

                // Get the matirx
                //ofMatrix4x4 matrix = RB.matrix;

                // Decompose to get the different elements
                //ofVec3f position;
                //ofQuaternion rotation;
                //ofVec3f scale;
                //ofQuaternion so;
                //matrix.decompose(position, rotation, scale, so);

                zosc_append( oscMsg, "sfffffff",
                             rbd[i].name.c_str(),
                             RB.position.x,
                             RB.position.y,
                             RB.position.z,
                             RB.rotation.x,
                             RB.rotation.y,
                             RB.rotation.z,
                             RB.rotation.w
                );

                //needed for skeleton retargeting
                if ( sendSkeletonDefinitions )
                {
                    zosc_append(oscMsg, "ifff",
                                rbd[i].parent_id,
                                rbd[i].offset.x,
                                rbd[i].offset.y,
                                rbd[i].offset.z
                    );
                }
            }

            zmsg_add(zmsg, zosc_pack(oscMsg));
        }
    }
}

char* NatNet2OSC::unpackMarkerSet(char* ptr, std::vector<Marker>& ref_markers)
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

char* NatNet2OSC::unpackRigidBodies(char* ptr, std::vector<RigidBody>& ref_rigidbodies)
{
    //TODO: Access static natnet vars
    int major = NatNet::NatNetVersion[0];
    int minor = NatNet::NatNetVersion[1];

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

void NatNet2OSC::Unpack( char ** pData ) {
    int major = NatNet::NatNetVersion[0];
    int minor = NatNet::NatNetVersion[1];

    //TODO: Matrix, only used for scale previously
    //Vec4 rotation = transform.getRotate();

    char *ptr = *pData;

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
    // this is stored centrally on static NatNet:: lists
    else if(MessageID == 5) // Data Descriptions
    {
        //std::vector<RigidBodyDescription> tmp_rigidbody_descs;
        //std::vector<SkeletonDescription> tmp_skeleton_descs;
        //std::vector<MarkerSetDescription> tmp_markerset_descs;

        // number of datasets
        int nDatasets = 0; memcpy(&nDatasets, ptr, 4);
        ptr += 4;
        //zsys_info("Dataset Count : %d\n", nDatasets);

        for(int i=0; i < nDatasets; i++)
        {
            //zsys_info("Dataset %d\n", i);

            int type = 0; memcpy(&type, ptr, 4); ptr += 4;
            //zsys_info("Type : %d\n", i, type);

            if(type == 0)   // markerset
            {
                //MarkerSetDescription description;

                // name
                char szName[256];
                strcpy(szName, ptr);
                int nDataBytes = (int) strlen(szName) + 1;
                ptr += nDataBytes;
                //zsys_info("Markerset Name: %s\n", szName);
                //description.name = szName;

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
                    //description.marker_names.push_back(szName);
                }
                //tmp_markerset_descs.push_back(description);
            }
            else if(type ==1)   // rigid body
            {
                //RigidBodyDescription description;

                if(major >= 2)
                {
                    // name
                    char szName[MAX_NAMELENGTH];
                    strcpy(szName, ptr);
                    ptr += strlen(ptr) + 1;
                    //zsys_info("Name: %s\n", szName);
                    //description.name = szName;
                }

                int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                //zsys_info("ID : %d\n", ID);
                //description.id = ID;

                int parentID = 0; memcpy(&parentID, ptr, 4); ptr +=4;
                //zsys_info("Parent ID : %d\n", parentID);
                //description.parent_id = parentID;

                float xoffset = 0; memcpy(&xoffset, ptr, 4); ptr +=4;
                //zsys_info("X Offset : %3.2f\n", xoffset);

                float yoffset = 0; memcpy(&yoffset, ptr, 4); ptr +=4;
                //zsys_info("Y Offset : %3.2f\n", yoffset);

                float zoffset = 0; memcpy(&zoffset, ptr, 4); ptr +=4;
                //zsys_info("Z Offset : %3.2f\n", zoffset);

                //description.offset[0] = xoffset;
                //description.offset[1] = xoffset;
                //description.offset[2] = xoffset;

                //tmp_rigidbody_descs.push_back(description);
            }
            else if(type ==2)   // skeleton
            {
                //SkeletonDescription description;

                char szName[MAX_NAMELENGTH];
                strcpy(szName, ptr);
                ptr += strlen(ptr) + 1;
                //zsys_info("Name: %s\n", szName);
                //description.name = szName;

                int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                //zsys_info("ID : %d\n", ID);
                //description.id = ID;

                int nRigidBodies = 0; memcpy(&nRigidBodies, ptr, 4); ptr +=4;
                //zsys_info("RigidBody (Bone) Count : %d\n", nRigidBodies);
                //description.joints.resize(nRigidBodies);

                for(int i=0; i< nRigidBodies; i++)
                {
                    if(major >= 2)
                    {
                        // RB name
                        char szName[MAX_NAMELENGTH];
                        strcpy(szName, ptr);
                        ptr += strlen(ptr) + 1;
                        //zsys_info("Rigid Body Name: %s\n", szName);
                        //description.joints[i].name = szName;
                    }

                    int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                    //zsys_info("RigidBody ID : %d\n", ID);
                    //description.joints[i].id = ID;

                    int parentID = 0; memcpy(&parentID, ptr, 4); ptr +=4;
                    //zsys_info("Parent ID : %d\n", parentID);
                    //description.joints[i].parent_id = parentID;

                    float xoffset = 0; memcpy(&xoffset, ptr, 4); ptr +=4;
                    //zsys_info("X Offset : %3.2f\n", xoffset);

                    float yoffset = 0; memcpy(&yoffset, ptr, 4); ptr +=4;
                    //zsys_info("Y Offset : %3.2f\n", yoffset);

                    float zoffset = 0; memcpy(&zoffset, ptr, 4); ptr +=4;
                    //zsys_info("Z Offset : %3.2f\n", zoffset);

                    //description.joints[i].offset[0] = xoffset;
                    //description.joints[i].offset[0] = yoffset;
                    //description.joints[i].offset[0] = zoffset;
                }
                //tmp_skeleton_descs.push_back(description);
            }

        }   // next dataset

        //zsys_info("End Packet\n-------------\n");

        //Store descriptions, skipped here
        //this->markerset_descs = tmp_markerset_descs;
        //this->rigidbody_descs = tmp_rigidbody_descs;
        //this->skeleton_descs = tmp_skeleton_descs;
    }
    else
    {
        zsys_info("Unrecognized Packet Type: %i.\n", MessageID);
    }
}

void NatNet2OSC::fixRanges( glm::vec3 *euler )
{
    if (euler->x < -180.0f)
        euler->x += 360.0f;
    else if (euler->x > 180.0f)
        euler->x -= 360.0f;

    if (euler->y < -180.0f)
        euler->y += 360.0f;
    else if (euler->y > 180.0f)
        euler->y -= 360.0f;

    if (euler->z < -180.0f)
        euler->z += 360.0f;
    else if (euler->z > 180.0f)
        euler->z -= 360.0f;
}
