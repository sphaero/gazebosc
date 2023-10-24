#ifndef ACTORS_H
#define ACTORS_H

#include "Actors/HTTPLaunchpod.h"
#include "Actors/OSCOutputActor.h"
#include "Actors/Midi2OSCActor.h"
#include "Actors/NatNetActor.h"
#include "Actors/NatNet2OSCActor.h"
#include "Actors/OpenVRActor.h"
#include "Actors/OSCInputActor.h"
#include "Actors/RecordActor.h"
#include "Actors/ModPlayerActor.h"
#include "Actors/ProcessActor.h"
#include "Actors/DmxActor.h"
#include "Actors/Sliders.h"
#ifdef PYTHON3_FOUND
#include "Actors/pythonactor.h"
#endif
#ifdef GSTREAMER_FOUND
#include "Actors/pythonactor.h"
#include "Actors/gstreameractor.h"
#endif

#endif
