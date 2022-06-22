#ifndef ACTORCONTAINER_H
#define ACTORCONTAINER_H

#include "libsphactor.h"
#include <string>
#include <vector>
#include "ImNodes.h"
#include "ImNodesEz.h"
#include <czmq.h>
#include <time.h>
#include "imgui_internal.h"
#include "ext/ImGui-Addons/FileBrowser/ImGuiFileBrowser.h"
#include "fontawesome5.h"
#include "config.h"

// actor file browser
imgui_addons::ImGuiFileBrowser actor_file_dialog;

static char *
convert_to_relative_to_wd(const char *path)
{
    char wdpath[PATH_MAX];
    getcwd(wdpath, PATH_MAX);

#if WIN32
    std::string pathStr = wdpath;
    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
    strcpy(wdpath, pathStr.c_str());
#endif

    const char *ret = strstr(path, wdpath);
    if (ret == NULL)
        return strdup(path);
    // working dir is found in path so only return the relative path
    assert(ret == path);
    ssize_t wdpathlen = strlen( wdpath ) + 1;// working dir path + first dir delimiter
    return strdup( &path[ wdpathlen ]);
}

// OpenTextEditor forward declare
void OpenTextEditor(zfile_t* txtfile);

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
                else if ( streq(typeStr, "Any")) {
                    input_slots.push_back({ "Any", ActorSlotAny });
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
                else if ( streq(typeStr, "Any")) {
                    output_slots.push_back({ "Any", ActorSlotAny });
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

    void SetCapabilities(const char* capabilities ) {
        this->capabilities = zconfig_str_load(capabilities);
        InitializeCapabilities();
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

#define GZB_TOOLTIP_THRESHOLD 1.0f
    void RenderTooltip(const char *help)
    {
        if ( strlen(help) )
        {
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > GZB_TOOLTIP_THRESHOLD )
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                ImGui::TextUnformatted(help);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
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
            else if (zvalue) {
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
            else if ( streq(typeStr, "slider")) {
                RenderSlider( nameStr, data );
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
            else if ( streq(typeStr, "trigger")) {
                RenderTrigger( nameStr, data );
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
                                    draw_list->AddCircleFilled(canvas->Offset + pos * canvas->Zoom + ImVec2(5,5) * canvas->Zoom, 5 * canvas->Zoom , ImColor(0, 255, 0), 4);
                                }
                                else {
                                    draw_list->AddCircleFilled(canvas->Offset + pos * canvas->Zoom + ImVec2(5, 5) * canvas->Zoom, 5 * canvas->Zoom, ImColor(127, 127, 127), 4);
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
        zconfig_t * zoptions = zconfig_locate(data, "options");
        zconfig_t * zvalidfiles = zconfig_locate(data, "valid_files");
        assert(zvalue);

        zconfig_t * zmax = zconfig_locate(data, "max");

        ReadInt( &max, zmax );

        char buf[MAX_STR_DEFAULT];
        const char* zvalueStr = zconfig_value(zvalue);
        strcpy(buf, zvalueStr);
        char *p = &buf[0];
        bool file_selected = false;

        const char* zoptStr = "r";
        if ( zoptions != nullptr ) {
            zoptStr = zconfig_value(zoptions);
        }
        const char *valid_files = zvalidfiles == nullptr ? "*.*" : zconfig_value(zvalidfiles);

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
                                              streq(zoptStr, "rw" ) ? imgui_addons::ImGuiFileBrowser::DialogMode::SAVE : imgui_addons::ImGuiFileBrowser::DialogMode::OPEN,
                                              ImVec2(700, 310),
                                              valid_files) ) // TODO: perhaps add type hint for extensions?
        {
            char *path = convert_to_relative_to_wd(actor_file_dialog.selected_path.c_str());
            zconfig_set_value(zvalue, "%s", path );
            strcpy(buf, path);
            //SendAPI<char*>(zapic, zapiv, zvalue, &(p) );
            zstr_free(&path);
            sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), p );
        }

        zconfig_t *help = zconfig_locate(data, "help");
        const char *helpv = "Load a file";
        if (help)
        {
            helpv = zconfig_value(help);
        }
        RenderTooltip(helpv);

        // handle options
        zconfig_t *opts = zconfig_locate(data, "options");
        // check if there are options
        if (opts)
        {
            char *optsv = zconfig_value(opts);
            // check if is an editable textfile
            if (optsv && strchr(optsv, 't') != NULL && strchr(optsv, 'e') != NULL)
            {
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_EDIT))
                {
                    zconfig_t* zvalue = zconfig_locate(data, "value");
                    const char* zvalueStr = zconfig_value(zvalue);
                    if (strlen(zvalueStr) && zsys_file_exists (zvalueStr) )
                    {
                        zfile_t* f = zfile_new(nullptr, zvalueStr);
                        OpenTextEditor(f); // could own the f pointer
                    }
                    else
                        zsys_error("no valid file to load: %s", zvalueStr);
                }
                RenderTooltip("Edit file in texteditor");
            }
            if (optsv && strchr(optsv, 'c') != NULL && strchr(optsv, 't') != NULL) // we can create a new text file
            {
                // create new text files
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FILE))
                    ImGui::OpenPopup("Create File?");

                RenderTooltip("Create new file");
                // Always center this window when appearing
                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

                if (ImGui::BeginPopupModal("Create File?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("Enter a filename:");
                    ImGui::Separator();
                    static char fnamebuf[PATH_MAX] = "";
                    bool createFile = false;
                    if ( ImGui::InputText("filename", fnamebuf, PATH_MAX, ImGuiInputTextFlags_EnterReturnsTrue ) )
                    {
                        if ( strlen(fnamebuf) )
                        {
                            createFile = true;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    char feedback[256] = "Enter a valid filename!";
                    if (strlen(fnamebuf))
                    {
                        // check if this filename conflicts
                        char path[PATH_MAX];
                        getcwd(path, PATH_MAX);
                        char fullpath[PATH_MAX];
                        sprintf (fullpath, "%s/%s", path, fnamebuf);
                        //zsys_info("filemode %i", zsys_file_mode(fullpath));
                        int mode = zsys_file_mode(fullpath);
                        if ( mode != -1 )
                        {
                            if ( S_ISDIR(mode) )
                                sprintf( feedback, "Filename \'%s\' conflicts with an existing directory!", fnamebuf);
                            else
                                sprintf( feedback, "File %s exists and will be overwritten if continued", fnamebuf);
                        }
                    }
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                    ImGui::Text(feedback);
                    ImGui::PopTextWrapPos();

                    if (ImGui::Button("OK", ImVec2(120, 0)))
                    {
                        if ( strlen(fnamebuf) )
                        {
                            createFile = true;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    {
                        createFile = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();

                    if (createFile && strlen(fnamebuf))
                    {
                        zchunk_t *file_data = NULL;
                        zconfig_t *tmpl = zconfig_locate(data, "file_template");
                        if (tmpl)
                        {
                            char *tmplpath = zconfig_value(tmpl);
                            char fullpath[PATH_MAX];
                            snprintf(fullpath, PATH_MAX-1, "%s/%s", GZB_GLOBAL.RESOURCESPATH, tmplpath);
                            if ( zsys_file_exists(fullpath) )
                            {
                                zfile_t *tmplf = zfile_new(NULL, fullpath);
                                assert(tmplf);
                                int rc = zfile_input(tmplf);
                                assert(rc == 0);
                                file_data = zfile_read(tmplf, zsys_file_size (tmplpath), 0);
                                assert(file_data);
                                zfile_destroy(&tmplf);
                            }
                            else
                            {
                                file_data = zchunk_new ("\n", 1);
                            }
                        }
                        else
                        {
                            file_data = zchunk_new ("\n", 1);
                        }
                        // create the new textfile
                        char path[PATH_MAX];
                        getcwd(path, PATH_MAX);
                        zfile_t *txtfile = zfile_new(path, fnamebuf);
                        assert(txtfile);
                        int rc = zfile_output(txtfile);
                        assert(rc == 0);
                        rc = zfile_write (txtfile, file_data, 0);
                        assert (rc == 0);
                        zchunk_destroy (&file_data);
                        zfile_close(txtfile);
                        //zfile_destroy(&txtfile);
                        zconfig_set_value(zvalue, "%s", fnamebuf);
                        sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), p );

                        OpenTextEditor(txtfile); // could own the f pointer
                    }
                }
            }
        }
    }

    void RenderBool(const char* name, zconfig_t *data) {
        bool value;

        zconfig_t * zvalue = zconfig_locate(data, "value");
        zconfig_t * zapic = zconfig_locate(data, "api_call");
        zconfig_t * zapiv = zconfig_locate(data, "api_value");
        assert(zvalue);
        char *apiv;
        // set default call value
        if (zapiv == nullptr)
            apiv = "s";
        else
            apiv = zconfig_value(zapiv);

        ReadBool( &value, zvalue);

        ImGui::SetNextItemWidth(100);
        if ( ImGui::Checkbox( name, &value ) ) {
            zconfig_set_value(zvalue, "%s", value ? "True" : "False");

            char *buf = new char[6];
            const char *zvalueStr = zconfig_value(zvalue);
            strcpy(buf, zvalueStr);
            //SendAPI<char *>(zapic, zapiv, zvalue, &buf);
            sphactor_ask_api(this->actor, zconfig_value(zapic), apiv, buf );
            zstr_free(&buf);
        }
        zconfig_t *help = zconfig_locate(data, "help");
        if (help)
        {
            char *helpv = zconfig_value(help);
            RenderTooltip(helpv);
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

        if ( ImGui::InputInt( name, &value, step, 100) ) {
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
        zconfig_t *help = zconfig_locate(data, "help");
        if (help)
        {
            char *helpv = zconfig_value(help);
            RenderTooltip(helpv);
        }
    }

    void RenderSlider(const char* name, zconfig_t *data) {

        zconfig_t * zvalue = zconfig_locate(data, "value");
        zconfig_t * zapic = zconfig_locate(data, "api_call");
        zconfig_t * zapiv = zconfig_locate(data, "api_value");
        assert(zvalue);

        zconfig_t * zmin = zconfig_locate(data, "min");
        zconfig_t * zmax = zconfig_locate(data, "max");
        zconfig_t * zstep = zconfig_locate(data, "step");

        ImGui::SetNextItemWidth(250);
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(200);

        if ( streq(zconfig_value(zapiv), "f") )
        {
            float value;
            float min = 0., max = 0., step = 0.;
            ReadFloat( &value, zvalue);
            ReadFloat( &min, zmin);
            ReadFloat( &max, zmax);
            ReadFloat( &step, zstep);

            if ( ImGui::SliderFloat("", &value, min, max) ) {
                zconfig_set_value(zvalue, "%f", value);
                sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), ftoa(value) );
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(35);
            if ( ImGui::InputFloat("##min", &min, step, 0.f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue ) )
            {
                zconfig_set_value(zmin, "%f", min);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(35);
            if ( ImGui::InputFloat( "##max", &max, step, 0.f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue ) )
            {
                zconfig_set_value(zmax, "%f", max);
            }
        }
        else
        {
            int value;
            int min = 0, max = 0, step = 0;
            ReadInt( &value, zvalue);
            ReadInt( &min, zmin);
            ReadInt( &max, zmax);
            ReadInt( &step, zstep);

            if ( ImGui::SliderInt( "", &value, min, max) ) {
                zconfig_set_value(zvalue, "%i", value);
                sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), itoa(value) );
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(30);
            if ( ImGui::InputInt( "##min", &min, 0, 0,ImGuiInputTextFlags_EnterReturnsTrue ) )
            {
                zconfig_set_value(zmin, "%i", min);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(30);
            if ( ImGui::InputInt( "##max", &max, 0, 0,ImGuiInputTextFlags_EnterReturnsTrue ) )
            {
                zconfig_set_value(zmax, "%i", max);
            }
        }

        ImGui::EndGroup();
        zconfig_t *help = zconfig_locate(data, "help");
        if (help)
        {
            char *helpv = zconfig_value(help);
            RenderTooltip(helpv);
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
        zconfig_t *help = zconfig_locate(data, "help");
        if (help)
        {
            char *helpv = zconfig_value(help);
            RenderTooltip(helpv);
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
        if ( ImGui::InputText(name, buf, max) ) {
            zconfig_set_value(zvalue, "%s", buf);
            sphactor_ask_api(this->actor, zconfig_value(zapic), zconfig_value(zapiv), buf);
        }

        zconfig_t *help = zconfig_locate(data, "help");
        if (help)
        {
            char *helpv = zconfig_value(help);
            RenderTooltip(helpv);
        }
    }

    void RenderTrigger(const char* name, zconfig_t *data) {
        zconfig_t * zapic = zconfig_locate(data, "api_call");

        ImGui::SetNextItemWidth(200);
        if ( ImGui::Button(name) ) {
            sphactor_ask_api(this->actor, zconfig_value(zapic), "", "" );
        }
        zconfig_t *help = zconfig_locate(data, "help");
        if (help)
        {
            char *helpv = zconfig_value(help);
            RenderTooltip(helpv);
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
