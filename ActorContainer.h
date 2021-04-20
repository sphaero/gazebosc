#ifndef ACTORCONTAINER_H
#define ACTORCONTAINER_H

#include "libsphactor.h"
#include <string>
#include <vector>
#include "ImNodes.h"
#include "ImNodesEz.h"
#include <czmq.h>
#include <time.h>
#include "ext/ImGui-Addons/FileBrowser/ImGuiFileBrowser.h"
#include "fontawesome5.h"

// actor file browser
imgui_addons::ImGuiFileBrowser actor_file_dialog;
// text editor forward
bool OpenTextEditor(zfile_t *txtfile);

/// A structure defining a connection between two slots of two actors.
struct Connection
{
    /// `id` that was passed to BeginNode() of input node.
    void* input_node = nullptr;
    /// Descriptor of input slot.
    const char* input_slot = nullptr;
    /// `id` that was passed to BeginNode() of output node.
    void* output_node = nullptr;
    /// Descriptor of output slot.
    const char* output_slot = nullptr;

    bool operator==(const Connection& other) const
    {
        return input_node == other.input_node &&
        input_slot == other.input_slot &&
        output_node == other.output_node &&
        output_slot == other.output_slot;
    }

    bool operator!=(const Connection& other) const
    {
        return !operator ==(other);
    }
};

enum GActorSlotTypes
{
    ActorSlotAny = 1,    // ID can not be 0
    ActorSlotPosition,
    ActorSlotRotation,
    ActorSlotMatrix,
    ActorSlotInt,
    ActorSlotOSC,
    ActorSlotNatNet
};

#define MAX_STR_DEFAULT 256 // Default value UI strings will have if no maximum is set
struct ActorContainer {
    /// Title which will be displayed at the center-top of the actor.
    const char* title = nullptr;
    /// Flag indicating that actor is selected by the user.
    bool selected = false;
    /// Actor position on the canvas.
    ImVec2 pos{};
    /// List of actor connections.
    std::vector<Connection> connections{};
    /// A list of input slots current actor has.
    std::vector<ImNodes::Ez::SlotInfo> input_slots{};
    /// A list of output slots current actor has.
    std::vector<ImNodes::Ez::SlotInfo> output_slots{};

    /// Last received "lastActive" clock value
    int64_t lastActive = 0;

    sphactor_t *actor;
    zconfig_t *capabilities;

    ActorContainer(sphactor_t *actor) {
        this->actor = actor;
        this->title = sphactor_ask_actor_type(actor);
        this->capabilities = zconfig_dup(sphactor_capability(actor));
        this->pos.x = sphactor_position_x(actor);
        this->pos.y = sphactor_position_y(actor);

        // retrieve in-/output sockets
        if (this->capabilities)
        {
            zconfig_t *insocket = zconfig_locate( this->capabilities, "inputs/input");
            if ( insocket !=  nullptr )
            {
                zconfig_t *type = zconfig_locate(insocket, "type");
                assert(type);

                char* typeStr = zconfig_value(type);
                if ( streq(typeStr, "OSC")) {
                    input_slots.push_back({ "OSC", ActorSlotOSC });
                }
                else if ( streq(typeStr, "NatNet")) {
                    input_slots.push_back({ "NatNet", ActorSlotNatNet });
                }
                else {
                    zsys_error("Unsupported input type: %s", typeStr);
                }
            }

            zconfig_t *outsocket = zconfig_locate( this->capabilities, "outputs/output");

            if ( outsocket !=  nullptr )
            {
                zconfig_t *type = zconfig_locate(outsocket, "type");
                assert(type);

                char* typeStr = zconfig_value(type);
                if ( streq(typeStr, "OSC")) {
                    output_slots.push_back({ "OSC", ActorSlotOSC });
                }
                else if ( streq(typeStr, "NatNet")) {
                    output_slots.push_back({ "NatNet", ActorSlotNatNet });
                }
                else {
                    zsys_error("Unsupported output type: %s", typeStr);
                }
            }
        }
        else
        {
            input_slots.push_back({ "OSC", ActorSlotOSC });
            output_slots.push_back({ "OSC", ActorSlotOSC });
        }
        //ParseConnections();
        //InitializeCapabilities(); //already done by sph_stage?
    }

    ~ActorContainer() {
        zconfig_destroy(&this->capabilities);
    }

    void ParseConnections() {
        if ( this->capabilities == NULL ) return;

        // get actor's connections
        zlist_t *conn = sphactor_connections( this->actor );
        for ( char *connection = (char *)zlist_first(conn); connection != nullptr; connection = (char *)zlist_next(conn))
        {
            Connection new_connection;
            new_connection.input_node = this;
            new_connection.output_node = FindActorContainerByEndpoint(connection);
            assert(new_connection.input_node);
            assert(new_connection.output_node);
            ((ActorContainer*) new_connection.input_node)->connections.push_back(new_connection);
            ((ActorContainer*) new_connection.output_node)->connections.push_back(new_connection);

        }
    }

    ActorContainer *
    FindActorContainerByEndpoint(const char *endpoint)
    {
        return NULL;
    }

    void InitializeCapabilities() {
        if ( this->capabilities == NULL ) return;

        zconfig_t *root = zconfig_locate(this->capabilities, "capabilities");
        if ( root == NULL ) return;

        zconfig_t *data = zconfig_locate(root, "data");
        while( data != NULL ) {
            HandleAPICalls(data);
            data = zconfig_next(data);
        }
    }

    char *itoa(int i)
    {
        char *str = (char *)malloc(10);
        sprintf(str, "%d", i);
        return str;
    }

    char *ftoa(float f)
    {
        char *str = (char *)malloc(30);
        sprintf(str, "%f", f);
        return str;
    }

    void HandleAPICalls(zconfig_t * data) {
        zconfig_t *zapic = zconfig_locate(data, "api_call");
        if ( zapic ) {
            zconfig_t *zapiv = zconfig_locate(data, "api_value");
            zconfig_t *zvalue = zconfig_locate(data, "value");

            if (zapiv)
            {
                char * zapivStr = zconfig_value(zapiv);
                char type = zapivStr[0];
                switch( type ) {
                    case 'b': {
                        char *buf = new char[4];
                        const char *zvalueStr = zconfig_value(zvalue);
                        strcpy(buf, zvalueStr);
                        sphactor_ask_api(this->actor, zconfig_value(zapic), zapivStr, zvalueStr);
                        //SendAPI<char *>(zapic, zapiv, zvalue, &buf);
                        zstr_free(&buf);
                    } break;
                    case 'i': {
                        int ival;
                        ReadInt(&ival, zvalue);
                        sphactor_ask_api(this->actor, zconfig_value(zapic), zapivStr, itoa(ival));
                    } break;
                    case 'f': {
                        float fval;
                        ReadFloat(&fval, zvalue);
                        //SendAPI<float>(zapic, zapiv, zvalue, &fval);
                        sphactor_ask_api(this->actor, zconfig_value(zapic), zapivStr, ftoa(fval));
                    } break;
                    case 's': {
                        const char *zvalueStr = zconfig_value(zvalue);
                        //SendAPI<char *>(zapic, zapiv, zvalue, const_cast<char **>(&zvalueStr));
                        sphactor_ask_api(this->actor, zconfig_value(zapic), zapivStr, zvalueStr);
                    } break;
                }
            }
            else {
                //assume string
                //int ival;
                //ReadInt(&ival, zvalue);
                //zsock_send( sphactor_socket(this->actor), "si", zconfig_value(zapic), ival);
                sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapic), zconfig_value(zvalue));
            }
        }
    }

    template<typename T>
    void SendAPI(zconfig_t *zapic, zconfig_t *zapiv, zconfig_t *zvalue, T * value) {
        if ( !zapic ) return;

        if (zapiv)
        {
            std::string pic = "s";
            pic += zconfig_value(zapiv);
            //zsys_info("Sending: %s", pic.c_str());
            //sphactor_ask_api( this->actor, pic.c_str(), zconfig_value(zapic), *value);
            zsock_send( sphactor_socket(this->actor), pic.c_str(), zconfig_value(zapic), *value);
        }
        else
            //sphactor_ask_api( this->actor, "ss", zconfig_value(zapic), *value);
            zsock_send( sphactor_socket(this->actor), "si", zconfig_value(zapic), *value);
    }


    void Render(float deltaTime) {
        //loop through each data element in capabilities
        if ( this->capabilities == NULL ) return;

        zconfig_t *root = zconfig_locate(this->capabilities, "capabilities");
        if ( root == NULL ) return;

        zconfig_t *data = zconfig_locate(root, "data");
        while( data != NULL ) {
            zconfig_t *name = zconfig_locate(data, "name");
            zconfig_t *type = zconfig_locate(data, "type");
            assert(name);
            assert(type);

            char* nameStr = zconfig_value(name);
            char* typeStr = zconfig_value(type);
            if ( streq(typeStr, "int")) {
                RenderInt( nameStr, data );
            }
            else if ( streq(typeStr, "float")) {
                RenderFloat( nameStr, data );
            }
            else if ( streq(typeStr, "string")) {
                RenderString(nameStr, data);
            }
            else if ( streq(typeStr, "bool")) {
                RenderBool( nameStr, data );
            }
            else if ( streq(typeStr, "filename")) {
                RenderFilename( nameStr, data );
            }
            else if ( streq(typeStr, "mediacontrol")) {
                RenderMediacontrol( nameStr, data );
            }

            data = zconfig_next(data);
        }
        RenderCustomReport();
    }

    void RenderCustomReport() {
        const int LABEL_WIDTH = 25;
        const int VALUE_WIDTH = 50;

        sphactor_report_t * report = sphactor_report(actor);
        lastActive = sphactor_report_send_time(report);

        assert(report);
        zosc_t * customData = sphactor_report_custom(report);
        if ( customData ) {
            const char* address = zosc_address(customData);
            //ImGui::Text("%s", address);

            char type = '0';
            const void *data = zosc_first(customData, &type);
            bool name = true;
            char* nameStr = nullptr;
            while( data ) {
                if ( name ) {
                    //expecting a name string for each piece of data
                    assert(type == 's');

                    int rc = zosc_pop_string(customData, &nameStr);
                    if (rc == 0 )
                    {
                        ImGui::BeginGroup();
                        if (!streq(nameStr, "lastActive")) {
                            ImGui::SetNextItemWidth(LABEL_WIDTH);
                            ImGui::Text("%s:", nameStr);
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(VALUE_WIDTH);
                        }
                    }
                }
                else {
                    switch(type) {
                        case 's': {
                            char* value;
                            int rc = zosc_pop_string(customData, &value);
                            if( rc == 0)
                            {
                                ImGui::Text("%s", value);
                                zstr_free(&value);
                            }
                        } break;
                        case 'c': {
                            char value;
                            zosc_pop_char(customData, &value);
                            ImGui::Text("%c", value);
                        } break;
                        case 'i': {
                            int32_t value;
                            zosc_pop_int32(customData, &value);
                            ImGui::Text("%i", value);
                        } break;
                        case 'h': {
                            int64_t value;
                            zosc_pop_int64(customData, &value);

                            if (streq(nameStr, "lastActive")) {
                                // render something to indicate the actor is currently active
                                lastActive = (int64_t)value;
                                int64_t diff = zclock_mono() - lastActive;
                                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                                ImNodes::CanvasState* canvas = ImNodes::GetCurrentCanvas();
                                if (diff < 100) {
                                    draw_list->AddCircleFilled(canvas->offset + pos * canvas->zoom + ImVec2(5,5) * canvas->zoom, 5 * canvas->zoom , ImColor(0, 255, 0), 4);
                                }
                                else {
                                    draw_list->AddCircleFilled(canvas->offset + pos * canvas->zoom + ImVec2(5, 5) * canvas->zoom, 5 * canvas->zoom, ImColor(127, 127, 127), 4);
                                }
                            }
                            else {
                                ImGui::Text("%lli", value);
                            }
                        } break;
                        case 'f': {
                            float value;
                            zosc_pop_float(customData, &value);
                            ImGui::Text("%f", value);
                        } break;
                        case 'd': {
                            double value;
                            zosc_pop_double(customData, &value);
                            ImGui::Text("%f", value);
                        } break;
                        case 'F': {
                            ImGui::Text("FALSE");
                        } break;
                        case 'T': {
                            ImGui::Text("TRUE");
                        } break;
                    }

                    ImGui::EndGroup();

                    //free the nameStr here so we can use it up to this point
                    zstr_free(&nameStr);
                }

                //flip expecting name or value
                name = !name;
                data = zosc_next(customData, &type);
            }
        }
    }

    void SolvePadding( int* position ) {
        if ( *position % 4 != 0 ) {
            *position += 4 - *position % 4;
        }
    }

    template<typename T>
    void RenderValue(T *value, const byte * bytes, int *position) {
        memcpy(value, bytes+*position, sizeof(T));
        *position += sizeof(T);
    }

    void RenderMediacontrol(const char* name, zconfig_t *data)
    {
        if ( ImGui::Button(ICON_FA_BACKWARD) )
            //sphactor_ask_api(this->actor, "BACK", "", NULL);
            zstr_send(sphactor_socket(this->actor), "BACK");
        ImGui::SameLine();
        if ( ImGui::Button(ICON_FA_PLAY) )
            zstr_send(sphactor_socket(this->actor), "PLAY");
        ImGui::SameLine();
        if ( ImGui::Button(ICON_FA_PAUSE) )
            zstr_send(sphactor_socket(this->actor), "PAUSE");
    }

    void RenderFilename(const char* name, zconfig_t *data) {
        int max = MAX_STR_DEFAULT;

        zconfig_t * zvalue = zconfig_locate(data, "value");
        zconfig_t * ztype_hint = zconfig_locate(data, "type_hint");
        zconfig_t * zapic = zconfig_locate(data, "api_call");
        zconfig_t * zapiv = zconfig_locate(data, "api_value");
        assert(zvalue);

        zconfig_t * zmax = zconfig_locate(data, "max");

        ReadInt( &max, zmax );

        char buf[MAX_STR_DEFAULT];
        const char* zvalueStr = zconfig_value(zvalue);
        strcpy(buf, zvalueStr);
        char *p = &buf[0];
        bool file_selected = false;

        ImGui::SetNextItemWidth(180);
        if ( ImGui::InputTextWithHint("", name, buf, max, ImGuiInputTextFlags_EnterReturnsTrue ) ) {
            zconfig_set_value(zvalue, "%s", buf);
            sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), p );
            //SendAPI<char*>(zapic, zapiv, zvalue, &(p));
        }
        ImGui::SameLine();
        if ( ImGui::Button( ICON_FA_FOLDER_OPEN ) )
            file_selected = true;

        if ( file_selected )
            ImGui::OpenPopup("Actor Open File");

        if ( actor_file_dialog.showFileDialog("Actor Open File",
                                              imgui_addons::ImGuiFileBrowser::DialogMode::OPEN,
                                              ImVec2(700, 310),
                                              "*.*") ) // TODO: perhaps add type hint for extensions?
        {
            zconfig_set_value(zvalue, "%s", actor_file_dialog.selected_path.c_str() );
            strcpy(buf, actor_file_dialog.selected_path.c_str());
            //SendAPI<char*>(zapic, zapiv, zvalue, &(p) );
            sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), p );
        }
        // TODO: handle options
        if ( true )//zconfig_locate(data, "options") )
        {
            if ( ImGui::Button(ICON_FA_EDIT) )
            {
                zconfig_t * zvalue = zconfig_locate(data, "value");
                const char* zvalueStr = zconfig_value(zvalue);
                zfile_t *f = zfile_new(nullptr, zvalueStr);
                if ( strlen(zvalueStr) && f )
                {
                    OpenTextEditor(f); // text editor now owns f
                }
                else
                    zsys_error("no valid file to load: %s", zvalueStr);
            }
        }
    }

    void RenderBool(const char* name, zconfig_t *data) {
        bool value;

        zconfig_t * zvalue = zconfig_locate(data, "value");
        zconfig_t * zapic = zconfig_locate(data, "api_call");
        zconfig_t * zapiv = zconfig_locate(data, "api_value");
        assert(zvalue);

        ReadBool( &value, zvalue);

        ImGui::SetNextItemWidth(100);
        if ( ImGui::Checkbox( name, &value ) ) {
            zconfig_set_value(zvalue, "%s", value ? "True" : "False");

            char *buf = new char[6];
            const char *zvalueStr = zconfig_value(zvalue);
            strcpy(buf, zvalueStr);
            //SendAPI<char *>(zapic, zapiv, zvalue, &buf);
            sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), buf );
            zstr_free(&buf);
        }
    }

    void RenderInt(const char* name, zconfig_t *data) {
        int value;
        int min = 0, max = 0, step = 0;

        zconfig_t * zvalue = zconfig_locate(data, "value");
        zconfig_t * zapic = zconfig_locate(data, "api_call");
        zconfig_t * zapiv = zconfig_locate(data, "api_value");
        assert(zvalue);

        zconfig_t * zmin = zconfig_locate(data, "min");
        zconfig_t * zmax = zconfig_locate(data, "max");
        zconfig_t * zstep = zconfig_locate(data, "step");

        ReadInt( &value, zvalue);
        ReadInt( &min, zmin);
        ReadInt( &max, zmax);
        ReadInt( &step, zstep);

        ImGui::SetNextItemWidth(100);
        if ( ImGui::InputInt( name, &value, step, 100,ImGuiInputTextFlags_EnterReturnsTrue ) ) {
            if ( zmin ) {
                if (value < min) value = min;
            }
            if ( zmax ) {
                if ( value > max ) value = max;
            }

            zconfig_set_value(zvalue, "%i", value);
            //SendAPI<int>(zapic, zapiv, zvalue, &value);
            sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), itoa(value) );
        }
    }

    void RenderFloat(const char* name, zconfig_t *data) {
        float value;
        float min = 0, max = 0, step = 0;

        zconfig_t * zvalue = zconfig_locate(data, "value");
        zconfig_t * zapic = zconfig_locate(data, "api_call");
        zconfig_t * zapiv = zconfig_locate(data, "api_value");
        assert(zvalue);

        zconfig_t * zmin = zconfig_locate(data, "min");
        zconfig_t * zmax = zconfig_locate(data, "max");
        zconfig_t * zstep = zconfig_locate(data, "step");

        ReadFloat( &value, zvalue);
        ReadFloat( &min, zmin);
        ReadFloat( &max, zmax);
        ReadFloat( &step, zstep);

        ImGui::SetNextItemWidth(100);
        if ( min != max ) {
            if ( ImGui::SliderFloat( name, &value, min, max) ) {
                zconfig_set_value(zvalue, "%f", value);
                //SendAPI<float>(zapic, zapiv, zvalue, &value);
                sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), ftoa(value) );
            }
        }
        else {
            if ( ImGui::InputFloat( name, &value, min, max) ) {
                zconfig_set_value(zvalue, "%f", value);
                //SendAPI<float>(zapic, zapiv, zvalue, &value);
                sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), ftoa(value) );
            }
        }
    }

    void RenderString(const char* name, zconfig_t *data) {
        int max = MAX_STR_DEFAULT;

        zconfig_t * zvalue = zconfig_locate(data, "value");
        zconfig_t * zapic = zconfig_locate(data, "api_call");
        zconfig_t * zapiv = zconfig_locate(data, "api_value");
        assert(zvalue);

        zconfig_t * zmax = zconfig_locate(data, "max");

        ReadInt( &max, zmax );

        char buf[MAX_STR_DEFAULT];
        const char* zvalueStr = zconfig_value(zvalue);
        strcpy(buf, zvalueStr);
        char *p = &buf[0];

        ImGui::SetNextItemWidth(200);
        if ( ImGui::InputText(name, buf, max, ImGuiInputTextFlags_EnterReturnsTrue ) ) {
            zconfig_set_value(zvalue, "%s", buf);
            //SendAPI<char*>(zapic, zapiv, zvalue, &(p));
            sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), buf );
        }
    }

    void ReadBool( bool *value, zconfig_t * data) {
        if ( data != NULL ) {
            *value = streq( zconfig_value(data), "True");
        }
    }

    void ReadInt( int *value, zconfig_t * data) {
        if ( data != NULL ) {
            *value = atoi(zconfig_value(data));
        }
    }

    void ReadFloat( float *value, zconfig_t * data) {
        if ( data != NULL ) {
            *value = atof(zconfig_value(data));
        }
    }

    void CreateActor() {
    }

    void DestroyActor() {
        if ( actor )
            sphactor_destroy(&actor);
    }

    void DeleteConnection(const Connection& connection)
    {
        for (auto it = connections.begin(); it != connections.end(); ++it)
        {
            if (connection == *it)
            {
                connections.erase(it);
                break;
            }
        }
    }

    //TODO: Add custom report data to this?
    void SerializeActorData(zconfig_t *section) {
        zconfig_t *xpos = zconfig_new("xpos", section);
        zconfig_set_value(xpos, "%f", pos.x);

        zconfig_t *ypos = zconfig_new("ypos", section);
        zconfig_set_value(ypos, "%f", pos.y);

        // Parse current state of data capabilities
        if ( this->capabilities ) {
            zconfig_t *root = zconfig_locate(this->capabilities, "capabilities");
            if ( root ) {
                zconfig_t *data = zconfig_locate(root, "data");
                while ( data ) {
                    zconfig_t *name = zconfig_locate(data,"name");
                    zconfig_t *value = zconfig_locate(data,"value");

                    if (value) // only store if there's a value
                    {
                        char *nameStr = zconfig_value(name);
                        char *valueStr = zconfig_value(value);

                        zconfig_t *stored = zconfig_new(nameStr, section);
                        zconfig_set_value(stored, "%s", valueStr);
                    }

                    data = zconfig_next(data);
                }
            }
        }
    }

    void DeserializeActorData( ImVector<char*> *args, ImVector<char*>::iterator it) {
        char* xpos = *it;
        it++;
        char* ypos = *it;
        it++;

        if ( it != args->end()) {
            if ( this->capabilities ) {
                zconfig_t *root = zconfig_locate(this->capabilities, "capabilities");
                if ( root ) {
                    zconfig_t *data = zconfig_locate(root, "data");
                    while ( data && it != args->end() ) {
                        zconfig_t *value = zconfig_locate(data,"value");
                        if (value)
                        {
                            char* valueStr = *it;
                            zconfig_set_value(value, "%s", valueStr);

                            HandleAPICalls(data);
                            it++;
                        }
                        data = zconfig_next(data);
                    }
                }
            }
        }

        pos.x = atof(xpos);
        pos.y = atof(ypos);

        free(xpos);
        free(ypos);
    }
};

#endif
