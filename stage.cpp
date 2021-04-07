//
// Copyright (c) 2017-2019 Rokas Kupstys.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <vector>
#include <map>
#include <string>
#include <imgui.h>
#include "ImNodesEz.h"
#include "libsphactor.h"
#include "ActorContainer.h"
#include "actors.h"
#include "ext/ImGui-Addons/FileBrowser/ImGuiFileBrowser.h"

//enum for menu actions
enum MenuAction
{
    MenuAction_Save = 0,
    MenuAction_Load,
    MenuAction_Clear,
    MenuAction_Exit,
    MenuAction_SaveAs,
    MenuAction_None
};

// List of actor types and constructors
//  Currently these are all internal dependencies, but we will need to create
//   a "generic node" class for types defined outside of this codebase.
ImVector<char*> actor_types;
std::vector<ActorContainer*> actors;
std::map<std::string, int> max_actors_by_type;

// File browser instance
imgui_addons::ImGuiFileBrowser file_dialog;
std::string editingFile = "";
std::string editingPath = "";

bool Save( const char* configFile );
bool Load( const char* configFile );
void Clear();
ActorContainer* Find( const char* endpoint );

void UpdateRegisteredActorsCache() {
    zlist_t* list = sphactor_get_registered();
    actor_types.clear();

    char* item = (char*)zlist_first(list);
    while( item ) {
        actor_types.push_back(item);
        item = (char*)zlist_next(list);
    }
}

void RegisterActors() {
    sphactor_register<Client>( "OSC Output" );
    sphactor_register<Count>( "Count" );
    sphactor_register( "Log", &LogActor, NULL, NULL );
    sphactor_register<NatNet>( "NatNet" );
    sphactor_register<NatNet2OSC>( "NatNet2OSC" );
    sphactor_register<OSCInput>( "OSC Input" );
    sphactor_register<Pulse>( "Pulse" );
    sphactor_register<ModPlayerActor>( "ModPlayer" );
#ifdef PYTHON3_FOUND
    int rc = python_init();
    assert( rc == 0);
    sphactor_register("Python", pythonactor_handler, pythonactor_new_helper, NULL); // https://stackoverflow.com/questions/65957511/typedef-for-a-registering-a-constructor-function-in-c
#endif
    //enforcable maximum actor counts
    max_actors_by_type.insert(std::make_pair("NatNet", 1));

    UpdateRegisteredActorsCache();
}

ActorContainer * CreateFromType( const char* typeStr, const char* uuidStr ) {
    zuuid_t *uuid = zuuid_new();
    if ( uuidStr != NULL )
      zuuid_set_str(uuid, uuidStr);

    sphactor_t *actor = sphactor_new_by_type(typeStr, uuidStr, uuid);
    sphactor_ask_set_actor_type(actor, typeStr);
    return new ActorContainer(actor);
}

int CountActorsOfType( const char* type ) {
    int count = 0;
    for (auto it = actors.begin(); it != actors.end(); it++)
    {
        ActorContainer* actor = *it;
        if ( streq( actor->title, type ) ) {
            count++;
        }
    }
    return count;
}

void ShowTextEditor()
{
    static char text[1024 * 16] =
                    "/*\n"
                    " The Pentium F00F bug, shorthand for F0 0F C7 C8,\n"
                    " the hexadecimal encoding of one offending instruction,\n"
                    " more formally, the invalid operand with locked CMPXCHG8B\n"
                    " instruction bug, is a design flaw in the majority of\n"
                    " Intel Pentium, Pentium MMX, and Pentium OverDrive\n"
                    " processors (all in the P5 microarchitecture).\n"
                    "*/\n\n"
                    "label:\n"
                    "\tlock cmpxchg8b eax\n";

    ImGui::Begin("Texteditor", &txteditor_open );
    //if(buf)
    //{
        //ImGui::InputTextMultiline("###source", buf, bufsize);
        ImGui::InputTextMultiline("##source", text, IM_ARRAYSIZE(text), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16), ImGuiInputTextFlags_AllowTabInput);
    //}
    ImGui::End();
}

void ShowConfigWindow(bool * showLog) {
    static char* configFile = new char[64];

    //creates window
    ImGui::Begin("Stage Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );

    ImGui::Text("Save/load stage");

    ImGui::InputText("Filename", configFile, 128);

    if (ImGui::Button("Save")) {                           // Buttons return true when clicked (most widgets return true when edited/activated)
        Save(configFile);
    }
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    if (ImGui::Button("Load")) {                           // Buttons return true when clicked (most widgets return true when edited/activated)
        Load(configFile);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {                           // Buttons return true when clicked (most widgets return true when edited/activated)
        Clear();
    }

    ImGui::Checkbox("Show Log Window", showLog);

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::End();
}

void ShowLogWindow(ImGuiTextBuffer& buffer) {
    static bool ScrollToBottom = true;

    ImGui::PushID(123);

    ImGui::SetNextWindowSizeConstraints(ImVec2(100,100), ImVec2(1000,1000));
    ImGui::Begin("Console");

    if (ImGui::Button("Clear")) buffer.clear();
    ImGui::SameLine();
    if ( ImGui::Button("To Bottom") ) {
        ScrollToBottom = true;
    }

    ImGui::BeginChild("scrolling", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if ( !ScrollToBottom && ImGui::GetScrollY() == ImGui::GetScrollMaxY() ) {
        ScrollToBottom = true;
    }

    ImGui::TextUnformatted(buffer.begin());

    if (ScrollToBottom)
        ImGui::SetScrollHereY(1.0f);
    ScrollToBottom = false;

    ImGui::EndChild();

    ImGui::End();

    ImGui::PopID();
}

int RenderMenuBar( bool * showLog ) {
    static char* configFile = new char[64] { 0x0 };
    static int keyFocus = 0;
    MenuAction action = MenuAction_None;

    if ( keyFocus > 0 ) keyFocus--;

    ImGui::BeginMainMenuBar();

    if ( ImGui::BeginMenu("File") ) {
        if ( ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Load") ) {
            //TODO: support checking if changes were made
            action = MenuAction_Load;
        }
        if ( ImGui::MenuItem(ICON_FA_SAVE " Save") ) {
            action = MenuAction_Save;
        }
        if ( ImGui::MenuItem(ICON_FA_SAVE " Save As") ) {
            action = MenuAction_SaveAs;
        }
        ImGui::Separator();
        ImGui::Separator();
        if ( ImGui::MenuItem(ICON_FA_WINDOW_CLOSE " Exit") ) {
            //TODO: support checking if changes were made
            action = MenuAction_Exit;
        }

        ImGui::EndMenu();
    }

    if ( ImGui::BeginMenu("Stage") ) {
        if ( ImGui::MenuItem(ICON_FA_TRASH_ALT " Clear") ) {
            action = MenuAction_Clear;
        }
        ImGui::EndMenu();
    }

    if ( ImGui::BeginMenu("Tools") ) {
        if ( ImGui::MenuItem(ICON_FA_TERMINAL " Toggle Console") ) {
            *showLog = !(*showLog);
        }
        ImGui::EndMenu();
    }

    //TODO: Display stage status (new, loaded, changed)
    ImGui::Separator();
    ImGui::Separator();

    if ( streq( editingFile.c_str(), "" ) ) {
        ImGui::TextColored( ImVec4(.7,.9,.7,1), ICON_FA_EDIT ": New Stage");
    }
    else {
        ImGui::TextColored( ImVec4(.7,.9,.7,1), ICON_FA_EDIT ": %s", editingFile.c_str());
    }

    ImGui::EndMainMenuBar();

    // Handle MenuActions
    if ( action == MenuAction_Load) {
        ImGui::OpenPopup("MenuAction_Load");
        keyFocus = 2;
    }
    else if ( action == MenuAction_Save ) {
        if ( streq( editingFile.c_str(), "" ) ) {
            ImGui::OpenPopup("MenuAction_Save");
        }
        else {
            Save(editingPath.c_str());
            ImGui::CloseCurrentPopup();
        }
    }
    else if ( action == MenuAction_SaveAs ) {
        ImGui::OpenPopup("MenuAction_Save");
    }
    else if ( action == MenuAction_Clear ) {
        Clear();
        editingFile = "";
        editingPath = "";
    }
    else if ( action == MenuAction_Exit ) {
        return -1;
    }

    if(file_dialog.showFileDialog("MenuAction_Load", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), "*.*"))
    {
        Load(file_dialog.selected_path.c_str());
        editingFile = file_dialog.selected_fn;
        editingPath = file_dialog.selected_path;
    }

    if(file_dialog.showFileDialog("MenuAction_Save", imgui_addons::ImGuiFileBrowser::DialogMode::SAVE, ImVec2(700, 310), "*.*"))
    {
        if ( !Save(file_dialog.selected_path.c_str()) ) {
            editingFile = "";
            editingPath = "";
        }
        else {
            editingFile = file_dialog.selected_fn;
            editingPath = file_dialog.selected_path;
        }
    }

    return 0;
}

int UpdateActors(float deltaTime, bool * showLog)
{
    int rc = 0;
    // Canvas must be created after ImGui initializes, because constructor accesses ImGui style to configure default colors.
    static ImNodes::CanvasState canvas{};

    //const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::SetNextWindowPos(ImVec2(0,0));

    if (ImGui::Begin("ImNodes", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_MenuBar ))
    {
        rc = RenderMenuBar(showLog);

        // We probably need to keep some state, like positions of nodes/slots for rendering connections.
        ImNodes::BeginCanvas(&canvas);
        for (auto it = actors.begin(); it != actors.end();)
        {
            ActorContainer* actor = *it;

            // Start rendering node
            if (ImNodes::Ez::BeginNode(actor, actor->title, &actor->pos, &actor->selected))
            {
                // Render input nodes first (order is important)
                ImNodes::Ez::InputSlots(actor->input_slots.data(), actor->input_slots.size());

                // Custom node content may go here
                actor->Render(deltaTime);

                // Render output slots second (order is important)
                ImNodes::Ez::OutputSlots(actor->output_slots.data(), actor->output_slots.size());

                // Store new connections when they are created
                Connection new_connection;
                if (ImNodes::GetNewConnection(&new_connection.input_node, &new_connection.input_slot,
                    &new_connection.output_node, &new_connection.output_slot))
                {
                    assert(new_connection.input_node);
                    assert(new_connection.output_node);
                    ((ActorContainer*) new_connection.input_node)->connections.push_back(new_connection);
                    ((ActorContainer*) new_connection.output_node)->connections.push_back(new_connection);
                    sphactor_ask_connect( ((ActorContainer*) new_connection.input_node)->actor,
                                      sphactor_ask_endpoint( ((ActorContainer*) new_connection.output_node)->actor ) );
                }

                // Render output connections of this actor
                for (const Connection& connection : actor->connections)
                {
                    // Node contains all it's connections (both from output and to input slots). This means that multiple
                    // nodes will have same connection. We render only output connections and ensure that each connection
                    // will be rendered once.
                    if (connection.output_node != actor)
                        continue;

                    if (!ImNodes::Connection(connection.input_node, connection.input_slot, connection.output_node,
                        connection.output_slot))
                    {
                        assert(connection.input_node);
                        assert(connection.output_node);
                        sphactor_ask_disconnect( ((ActorContainer*) connection.input_node)->actor,
                                          sphactor_ask_endpoint( ((ActorContainer*) connection.output_node)->actor ) );
                        // Remove deleted connections
                        ((ActorContainer*) connection.input_node)->DeleteConnection(connection);
                        ((ActorContainer*) connection.output_node)->DeleteConnection(connection);
                    }
                }
            }
            // Node rendering is done. This call will render node background based on size of content inside node.
            ImNodes::Ez::EndNode();

            bool del = ImGui::IsKeyPressedMap(ImGuiKey_Delete) || ( ImGui::IsKeyPressedMap(ImGuiKey_Backspace) && ImGui::GetActiveID() == 0 );
            if (actor->selected && del)
            {
                // Loop and delete connections of actors connected to us
                for (auto& connection : actor->connections)
                {
                    if (connection.output_node == actor) {
                        ((ActorContainer*) connection.input_node)->DeleteConnection(connection);
                    }
                    else {
                        ((ActorContainer*) connection.output_node)->DeleteConnection(connection);
                    }
                }
                // Delete all our connections separately
                actor->connections.clear();
                actor->DestroyActor();

                delete actor;
                it = actors.erase(it);
            }
            else
                ++it;
        }

        if (ImGui::IsMouseReleased(1) && ImGui::IsWindowHovered() && !ImGui::IsMouseDragging(1))
        {
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
            ImGui::OpenPopup("NodesContextMenu");
        }

        if (ImGui::BeginPopup("NodesContextMenu"))
        {
            //TODO: Fetch updated available nodes?
            for (const auto desc : actor_types)
            {
                if (ImGui::MenuItem(desc))
                {
                    if ( max_actors_by_type.find(desc) != max_actors_by_type.end() ) {
                        int max = max_actors_by_type.at(desc);
                        if ( CountActorsOfType(desc) < max ) {
                            ActorContainer *actor = CreateFromType(desc, nullptr);
                            actors.push_back(actor);
                            ImNodes::AutoPositionNode(actors.back());
                        }
                    }
                    else {
                        ActorContainer *actor = CreateFromType(desc, nullptr);
                        actors.push_back(actor);
                        ImNodes::AutoPositionNode(actors.back());
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Zoom"))
                canvas.zoom = 1;

            if (ImGui::IsAnyMouseDown() && !ImGui::IsWindowHovered())
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImNodes::EndCanvas();
    }
    ImGui::End();

    if (txteditor_open) ShowTextEditor();

    return rc;
}

bool Save( const char* configFile ) {
    if ( actors.size() == 0 ) return false;

    zconfig_t* config = sphactor_zconfig_new("root");
    for (auto it = actors.begin(); it != actors.end(); it++)
    {
        ActorContainer* actor = *it;
        zconfig_t* actorSection = sphactor_zconfig_append(actor->actor, config);

        // Add custom actor data to section
        actor->SerializeActorData(actorSection);

        zconfig_t* connections = zconfig_locate(config, "connections");
        if ( connections == nullptr ) {
            connections = zconfig_new("connections", config);
        }

        for (const Connection& connection : actor->connections)
        {
            //only store connections once
            if ( actor != connection.output_node ) continue;

            zconfig_t* item = zconfig_new( "con", connections );

            ActorContainer *out = (ActorContainer*)connection.output_node;
            ActorContainer *in = (ActorContainer*)connection.input_node;

            zconfig_set_value(item,"%s,%s,%s", sphactor_ask_endpoint(out->actor), sphactor_ask_endpoint(in->actor), connection.output_slot );
        }
    }
    zconfig_save(config, configFile);

    return true;
}

bool Load( const char* configFile ) {
    zconfig_t* root = zconfig_load(configFile);

    if ( root == nullptr ) return false;

    zconfig_t* configActors = zconfig_locate(root, "actors");

    // Clear current stage
    //TODO: Maybe ask if people want to save first?
    Clear();

    // Get and loop all actors
    zconfig_t* actor = zconfig_locate(configActors, "actor");
    while( actor != nullptr )
    {
        zconfig_t* uuid = zconfig_locate(actor, "uuid");
        zconfig_t* type = zconfig_locate(actor, "type");
        zconfig_t* endpoint = zconfig_locate(actor, "endpoint");

        char *uuidStr = zconfig_value(uuid);
        char *typeStr = zconfig_value(type);
        char *endpointStr = zconfig_value(endpoint);

        // We're assuming the endpoint is the last thing added by the sphactor actor
        //  from there we ready until we receive null and send that to the high-level actor
        ImVector<char*> *args = new ImVector<char*>();
        zconfig_t *arg = zconfig_next(endpoint);
        while ( arg != nullptr ) {
            args->push_back(zconfig_value(arg));
            arg = zconfig_next(arg);
        }

        ActorContainer *gActor = CreateFromType(typeStr, uuidStr);
        gActor->CreateActor();

        auto it = args->begin();
        gActor->DeserializeActorData(args, it);

        actors.push_back(gActor);

        actor = zconfig_next(actor);

        free(uuidStr);
        free(typeStr);
        free(endpointStr);

        args->clear();
        delete args;
    }

    // Get and loop all connections
    zconfig_t* connections = zconfig_locate(root, "connections");
    zconfig_t* con = zconfig_locate( connections, "con");
    while( con != nullptr ) {
        char* conVal = zconfig_value(con);

        // Parse comma separated connection string to get the two endpoints
        int i;
        for( i = 0; conVal[i] != '\0'; ++i ) {
            if ( conVal[i] == ',' ) break;
        }

        char* output = new char[i+1];
        char* input = new char[strlen(conVal)-i];
        char* type = new char[64];

        char * pch;
        pch = strtok (conVal,",");
        sprintf(output, "%s", pch);
        pch = strtok (NULL, ",");
        sprintf(input, "%s", pch);
        pch = strtok(NULL, ",");
        sprintf(type, "%s", pch);

        // Loop actors and find output actor
        for (auto it = actors.begin(); it != actors.end();)
        {
            ActorContainer* actor = *it;

            // We're the output side, so we recreate the connection
            if (streq(sphactor_ask_endpoint(actor->actor), output)) {
                Connection connection;
                ActorContainer* inputActor = Find(input);
                assert(inputActor);

                connection.output_node = actor;
                connection.input_node = inputActor;
                connection.input_slot = type;
                connection.output_slot = type;

                actor->connections.push_back(connection);
                inputActor->connections.push_back(connection);

                sphactor_ask_connect( inputActor->actor, sphactor_ask_endpoint(actor->actor) );
                break;
            }

            it++;
        }

        con = zconfig_next(con);
        delete[] output;
        delete[] input;
    }

    free(root);

    return true;
}

void Clear() {
    //delete all connections
    for (auto it = actors.begin(); it != actors.end();)
    {
        ActorContainer* actor = *it;

        for (auto& connection : actor->connections)
        {
            //delete them once
            if (connection.output_node == actor) {
                ((ActorContainer*) connection.input_node)->DeleteConnection(connection);
            }
        }
        actor->connections.clear();
        it++;
    }

    //delete all actors
    for (auto it = actors.begin(); it != actors.end();)
    {
        ActorContainer* actor = *it;
        actor->DestroyActor();
        delete actor;
        it = actors.erase(it);
    }
}

ActorContainer* Find( const char* endpoint ) {
    for (auto it = actors.begin(); it != actors.end();)
    {
        ActorContainer* actor = *it;
        if ( streq( sphactor_ask_endpoint(actor->actor), endpoint)) {
            return actor;
        }

        it++;
    }

    return nullptr;
}
