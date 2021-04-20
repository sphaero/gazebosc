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

sph_stage_t *stage = NULL;

// File browser instance
imgui_addons::ImGuiFileBrowser file_dialog;
std::string editingFile = "";
std::string editingPath = "";

// Text editor instance
bool showEditor = true;
struct textfile {
    zfile_t *file;
    zchunk_t *data;
};
std::vector<textfile> open_text_files;

bool Save( const char* configFile );
bool Load( const char* configFile );
void Clear();
ActorContainer* Find( const char* endpoint );

// ImGui Demo window for dev purposes
bool showDemo = false;
void ImGui::ShowDemoWindow(bool* p_open);

void UpdateRegisteredActorsCache() {
    zhash_t *hash = sphactor_get_registered();
    zlist_t *list = zhash_keys(hash);
    actor_types.clear();

    char* item = (char*)zlist_first(list);
    while( item ) {
        actor_types.push_back(item);
        item = (char*)zlist_next(list);
    }
}

void RegisterActors() {
    // register stock actors
    sph_stock_register_all();

    sphactor_register<Client>( "OSC Output", Client::capabilities);
    sphactor_register<NatNet>( "NatNet", NatNet::capabilities );
    sphactor_register<NatNet2OSC>( "NatNet2OSC", NatNet2OSC::capabilities );
#ifdef HAVE_OPENVR
    sphactor_register<OpenVR>("OpenVR", OpenVR::capabilities);
#endif
    sphactor_register<OSCInput>( "OSC Input", OSCInput::capabilities );
    sphactor_register<ModPlayerActor>( "ModPlayer", ModPlayerActor::capabilities );
    sphactor_register<ProcessActor>( "Process", ProcessActor::capabilities );
#ifdef PYTHON3_FOUND
    int rc = python_init();
    assert( rc == 0);
#endif
    //enforcable maximum actor counts
    max_actors_by_type.insert(std::make_pair("NatNet", 1));
    max_actors_by_type.insert(std::make_pair("OpenVR", 1));

    UpdateRegisteredActorsCache();
}

ActorContainer * CreateFromType( const char* typeStr, const char* uuidStr ) {
    zuuid_t *uuid = zuuid_new();
    if ( uuidStr != NULL )
      zuuid_set_str(uuid, uuidStr);

    sphactor_t *actor = sphactor_new_by_type(typeStr, uuidStr, uuid);
    sphactor_ask_set_actor_type(actor, typeStr);
    if ( stage == nullptr )
        stage = sph_stage_new("New Stage");
    sph_stage_add_actor(stage, actor);
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

#include "TextEditor.h"

bool OpenTextEditor(zfile_t *txtfile)
{
    assert(txtfile);
    bool found = false;
    for(auto it = open_text_files.begin(); it != open_text_files.end(); ++it)
    {
        if ( streq( zfile_filename((*it).file, NULL), zfile_filename(txtfile, NULL) ) )
        {
            found = true;
            break; // file already exists in the editor
        }
    }
    if ( ! found )
    {
        // we own the pointer now!
        int rc = zfile_input(txtfile);
        assert(rc == 0);
        zchunk_t *data = zfile_read( txtfile, zfile_cursize(txtfile), 0 );
        open_text_files.push_back( { txtfile, data } );
    }
    //else
        // todo?
    showEditor = true;
}

void ShowColorTextEditor()
{
    static TextEditor editor;
    static auto lang = TextEditor::LanguageDefinition::CPlusPlus();
    static const char* fileToEdit = "ImGuiColorTextEdit/TextEditor.cpp";
    editor.SetLanguageDefinition(lang);

    auto cpos = editor.GetCursorPosition();
    ImGui::Begin(fileToEdit, nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar);
    ImGui::SetWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save"))
            {
                auto textToSave = editor.GetText();
                /// save text....
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            bool ro = editor.IsReadOnly();
            if (ImGui::MenuItem("Read-only mode", nullptr, &ro))
                editor.SetReadOnly(ro);
                ImGui::Separator();

            if (ImGui::MenuItem("Undo", "ALT-Backspace", nullptr, !ro && editor.CanUndo()))
                editor.Undo();
            if (ImGui::MenuItem("Redo", "Ctrl-Y", nullptr, !ro && editor.CanRedo()))
                editor.Redo();

            ImGui::Separator();

            if (ImGui::MenuItem("Copy", "Ctrl-C", nullptr, editor.HasSelection()))
                editor.Copy();
            if (ImGui::MenuItem("Cut", "Ctrl-X", nullptr, !ro && editor.HasSelection()))
                editor.Cut();
            if (ImGui::MenuItem("Delete", "Del", nullptr, !ro && editor.HasSelection()))
                editor.Delete();
            if (ImGui::MenuItem("Paste", "Ctrl-V", nullptr, !ro && ImGui::GetClipboardText() != nullptr))
                editor.Paste();

            ImGui::Separator();

            if (ImGui::MenuItem("Select all", nullptr, nullptr))
                editor.SetSelection(TextEditor::Coordinates(), TextEditor::Coordinates(editor.GetTotalLines(), 0));

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Dark palette"))
                editor.SetPalette(TextEditor::GetDarkPalette());
            if (ImGui::MenuItem("Light palette"))
                editor.SetPalette(TextEditor::GetLightPalette());
            if (ImGui::MenuItem("Retro blue palette"))
                editor.SetPalette(TextEditor::GetRetroBluePalette());
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("%6d/%-6d %6d lines  | %s | %s | %s | %s", cpos.mLine + 1, cpos.mColumn + 1, editor.GetTotalLines(),
            editor.IsOverwrite() ? "Ovr" : "Ins",
            editor.CanUndo() ? "*" : " ",
            editor.GetLanguageDefinition().mName.c_str(), fileToEdit);

        ImGui::EndMenuBar();
    }
    ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;

    if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags))
    {
        for(auto it = open_text_files.begin(); it != open_text_files.end(); ++it)
        {
            if (ImGui::BeginTabItem( zfile_filename((*it).file, nullptr) ) )
            {

                editor.SetText(std::string((char *)zchunk_data((*it).data)));

                editor.Render("TextEditor");
                ImGui::EndTabItem();
            }
        }

        /*if (ImGui::BeginTabItem("*new*"))
        {
            ImGui::Text("Add new file");
            ImGui::EndTabItem();
        }*/
        ImGui::EndTabBar();
    }
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
        if ( ImGui::MenuItem(ICON_FA_EDIT " Toggle Text Editor") ) {
            showEditor = !(showEditor);
        }
        if ( ImGui::MenuItem(ICON_FA_EDIT " Toggle Demo") ) {
            showDemo = !(showDemo);
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

inline ImU32 LerpImU32( ImU32 c1, ImU32 c2, int index, int total, float offset, bool doubleSided )
{
    float t = (float)index / total;
    t = glm::max(glm::min(t + offset, 1.0f), 0.0f);

    if ( doubleSided && t < .5f ) {
        ImU32 col;
        col = c1;
        c1 = c2;
        c2 = col;
    }

    unsigned char   a1 = (c1 >> 24) & 0xff;
    unsigned char   a2 = (c2 >> 24) & 0xff;
    unsigned char   r1 = (c1 >> 16) & 0xff;
    unsigned char   r2 = (c2 >> 16) & 0xff;
    unsigned char   g1 = (c1 >> 8) & 0xff;
    unsigned char   g2 = (c2 >> 8) & 0xff;
    unsigned char   b1 = c1 & 0xff;
    unsigned char   b2 = c2 & 0xff;

    return  (int) ((a2 - a1) * t + a1) << 24 |
            (int) ((r2 - r1) * t + r1) << 16 |
            (int) ((g2 - g1) * t + g1) << 8 |
            (int) ((b2 - b1) * t + b1);
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

            if ( abs(actor->pos.x) > 100000000 || abs(actor->pos.y) > 100000000 ) {
                actor->pos = ImGui::GetMousePos();
            }

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

                    ImDrawList * draw_list = ImGui::GetWindowDrawList();
                    int oldId = draw_list->_VtxCurrentIdx;

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
                    else
                    {
                        // Animate the bezier vertex colors for recently active connections
                        int64_t diff = zclock_mono() - ((ActorContainer*) connection.output_node)->lastActive;
                        if ( diff < 500 ) {
                            float offset = ( diff % 500 - 250 ) * .004f;
                            int newId = draw_list->_VtxCurrentIdx;
                            int totalCount = (newId - oldId);

                            // We're assuming it's always a 2-vertex-per-point setup (thick/textured)
                            if (totalCount % 2 == 0) {
                                int left = totalCount;
                                ImDrawVert *p = draw_list->_VtxWritePtr - totalCount;
                                while (left > 0) {
                                    int index = (totalCount - left) * .5f;
                                    // TODO: Determine what colors we want to render...
                                    ImU32 col = LerpImU32(ImColor(0, 255, 0),
                                                          canvas.colors[ImNodes::ColConnection], index,
                                                          totalCount / 2, offset, false);

                                    p->col = col;
                                    (p + 1)->col = col;

                                    left -= 2;
                                    p += 2;
                                }
                            }
                        }
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
                sph_stage_remove_actor(stage, zuuid_str(sphactor_ask_uuid(actor->actor)));

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

    if (showEditor) ShowColorTextEditor();
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);

    return rc;
}

bool Save( const char* configFile ) {
    if ( actors.size() == 0 ) return false;

    // sync positions
    auto it = actors.begin();
    while( it != actors.end() )
    {
        ActorContainer *gActor = *it;
        sphactor_set_position(gActor->actor, gActor->pos.x, gActor->pos.y);
        ++it;
    }

    int rc = sph_stage_save_as( stage, configFile);
    return rc == 0;
}

bool Load( const char* configFile ) {


    // Clear current stage
    Clear();
    if (stage == NULL )
        stage = sph_stage_new("Untitled");
    //TODO: Maybe ask if people want to save first?

    int rc = sph_stage_load(stage, configFile);

    // Create a container for every actor
    const zhash_t *stage_actors = sph_stage_actors(stage);
    sphactor_t *actor = (sphactor_t *)zhash_first( (zhash_t *)stage_actors ); //zconfig_locate(configActors, "actor");
    while( actor != NULL )
    {
        const char *uuidStr = zuuid_str(sphactor_ask_uuid(actor));
        const char *typeStr = sphactor_ask_actor_type(actor);
        const char *endpointStr = sphactor_ask_endpoint(actor);

        ActorContainer *gActor = new ActorContainer( actor ); //CreateFromType(typeStr, uuidStr);

        actors.push_back(gActor);
        actor = (sphactor_t *)zhash_next((zhash_t *)stage_actors);
    }
    // loop actor containers to handle all connections
    auto it = actors.begin();//(sphactor_t *)zhash_first( (zhash_t *)stage_actors ); //zconfig_locate(configActors, "actor");
    while( it != actors.end() )
    {
        ActorContainer *gActor = *it;
        // get actor's connections
        zlist_t *conn = sphactor_connections( gActor->actor );
        for ( char *connection = (char *)zlist_first(conn); connection != nullptr; connection = (char *)zlist_next(conn))
        {
            // find connect actor container
            ActorContainer *peer_actor_container = Find(connection);
            if (peer_actor_container != nullptr)
            {
                Connection new_connection;
                new_connection.input_node = gActor;
                new_connection.input_slot = "OSC";
                new_connection.output_node = peer_actor_container;
                new_connection.output_slot = "OSC";
                ((ActorContainer*) new_connection.input_node)->connections.push_back(new_connection);
                ((ActorContainer*) new_connection.output_node)->connections.push_back(new_connection);
            }

        }
        ++it;
    }

    return rc != -1;
}

void Clear() {

    //delete all connections
    for (auto it = actors.begin(); it != actors.end();)
    {
        ActorContainer* gActor = *it;

        for (auto& connection : gActor->connections)
        {
            //delete them once
            if (connection.output_node == gActor) {
                ((ActorContainer*) connection.input_node)->DeleteConnection(connection);
            }
        }
        gActor->connections.clear();
        it++;
    }

    if ( stage ) sph_stage_clear( stage );

    //delete all actors
    for (auto it = actors.begin(); it != actors.end();)
    {
        ActorContainer* actor = *it;
        // sphactor is already destroyed by stage
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
