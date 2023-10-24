//
// Copyright (c) 21021 Arnaud Loonstra/Aaron Oostdijk.
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
#include <stack>
#include <filesystem>
namespace fs = std::filesystem;
#include <imgui.h>
#include "ImNodesEz.h"
#include "libsphactor.h"
#include "ActorContainer.h"
#include "actors.h"
#include "ext/ImGui-Addons/FileBrowser/ImGuiFileBrowser.h"
#include "ext/ImGuiColorTextEdit/TextEditor.h"
#include "config.h"

#include "ext/SDL/include/SDL_keyboard.h"
#include "ext/SDL/include/SDL_scancode.h"

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

//enum for undo stack
enum UndoAction
{
    UndoAction_Create = 0,
    UndoAction_Delete,
    UndoAction_Disconnect,
    UndoAction_Connect,
    UndoAction_Invalid
};

struct UndoData {
    UndoAction type = UndoAction_Invalid;
    const char * title = nullptr;
    const char * uuid = nullptr;
    const char * endpoint = nullptr;
    const char * input_slot = nullptr;
    const char * output_slot = nullptr;
    zconfig_t * sphactor_config = nullptr;
    ImVec2 position;

    UndoData( UndoData * from ) {
        type = from->type;
        title = from->title ? strdup(from->title) : nullptr;
        uuid = from->uuid ? strdup(from->uuid) : nullptr;
        endpoint = from->endpoint ? strdup(from->endpoint) : nullptr;
        input_slot = from->input_slot ? strdup(from->input_slot) : nullptr;
        output_slot = from->output_slot ? strdup(from->output_slot) : nullptr;
        if (from->sphactor_config != nullptr)
            sphactor_config = zconfig_dup(from->sphactor_config);
        position = from->position;
    }
    UndoData() {}
};

std::stack<UndoData> undoStack;
std::stack<UndoData> redoStack;

// List of actor types and constructors
//  Currently these are all internal dependencies, but we will need to create
//   a "generic node" class for types defined outside of this codebase.
ImVector<char*> actor_types;
std::vector<ActorContainer*> actors;
std::map<std::string, int> max_actors_by_type;

sph_stage_t *stage = NULL;
static ziflist_t *nics = NULL; // list of network interfaces

// File browser instance
imgui_addons::ImGuiFileBrowser file_dialog;
std::string editingFile = "";
std::string editingPath = "";

bool Save( const char* configFile );
bool Load( const char* configFile );
void Clear();
void Init();
ActorContainer* Find( const char* endpoint );

// undo stuff
void performUndo(UndoData &undo);
void RegisterCreateAction( ActorContainer * actor );
void RegisterDeleteAction( ActorContainer * actor );
void RegisterConnectAction(ActorContainer * in, ActorContainer * out, const char * input_slot, const char * output_slot);
void RegisterDisconnectAction(ActorContainer * in, ActorContainer * out, const char * input_slot, const char * output_slot);
void swapUndoType(UndoData * undo);

// TODO: move this to something includable
static char*
s_basename(char const* path)
{
    const char* s = strrchr(path, '/');
    if (!s)
        return strdup(path);
    else
        return strdup(s + 1);
}

void moveCwdIfNeeded() {
    // check if we are still in the working dir or if we should move to the new dir
    char cwd[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    // editingPath not starting with cwd means we need to move to the new wd
    if (editingPath.rfind(cwd, 0) != 0) {
        // we're not in the current working dir! move files to editingPath
        // moving if cwd was tmp dir (name contains _gzs_)
        std::string cwds = std::string(cwd);
        std::filesystem::path newcwds = std::filesystem::path(editingPath);
        fs::path tmppath(GZB_GLOBAL.TMPPATH);
        tmppath.append("_gzs_");
        if (cwds.rfind(tmppath.string(), 0) == 0)
        {
            // cwd is a tmp dir so we need to move files
            // copy and delete for now
            try
            {
                fs::copy(cwds, newcwds.parent_path(), fs::copy_options::overwrite_existing | fs::copy_options::recursive);
            }
            catch (std::exception& e)
            {
                zsys_error("Copy files to new working dir failed: %s", e.what());
            }
            try
            {
                fs::remove_all(cwds);
            }
            catch (std::exception& e)
            {
                zsys_error("Removing old tmpdir failed: %s", e.what());
            }
        }
        else
        {
            // we copy recursively
            // Recursively copies all files and folders from src to target and overwrites existing files in target.
            try
            {
                fs::copy(cwds, newcwds.parent_path(), fs::copy_options::overwrite_existing | fs::copy_options::recursive);
            }
            catch (std::exception& e)
            {
                zsys_error("Copy files to new working dir failed: %s", e.what());
            }
        }
        fs::current_path(newcwds.parent_path());
        zsys_info("Working dir set to %s", newcwds.parent_path().c_str());
    }
}

// Ability to load multiple text files
struct textfile {
    TextEditor *editor;
    zfile_t *file;
    char *basename;
    bool open;
    bool changed;
};
std::vector<textfile> open_text_files;
bool txteditor_open = false;
textfile* current_editor = nullptr;
textfile* hardswap_editor = nullptr;

#ifdef HAVE_IMGUI_DEMO
// ImGui Demo window for dev purposes
bool showDemo = false;
void ImGui::ShowDemoWindow(bool* p_open);
#endif

void SelectableText(const char *buf)
{
    ImGui::PushID(buf);
    ImGui::PushItemWidth(ImGui::GetColumnWidth());
    ImGui::GetStyleColorVec4(ImGuiCol_TableRowBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_TableRowBg)); //otherwise it is colored
    ImGui::InputText("", (char *)buf, strlen(buf), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopItemWidth();
    ImGui::PopStyleColor();
    ImGui::PopID();
}

bool showAbout = false;

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

    sphactor_register("HTTPLaunchpod", &httplaunchpodactor_handler, zconfig_str_load(httplaunchpodactorcapabilities), &httplaunchpodactor_new_helper, NULL); // https://stackoverflow.com/questions/65957511/typedef-for-a-registering-a-constructor-function-in-c
    sphactor_register<OSCOutput>( "OSC Output", OSCOutput::capabilities);
    sphactor_register<NatNet>( "NatNet", NatNet::capabilities );
    sphactor_register<NatNet2OSC>( "NatNet2OSC", NatNet2OSC::capabilities );
    sphactor_register<Midi2OSC>( "Midi2OSC", Midi2OSC::capabilities );
#ifdef HAVE_OPENVR
    sphactor_register<OpenVR>("OpenVR", OpenVR::capabilities);
#endif
    sphactor_register<OSCInput>( "OSC Input", OSCInput::capabilities );
    sphactor_register<Record>("Record", Record::capabilities );
    sphactor_register<ModPlayerActor>( "ModPlayer", ModPlayerActor::capabilities );
    sphactor_register<ProcessActor>( "Process", ProcessActor::capabilities );
#ifdef HAVE_OPENVR
    sphactor_register<DmxActor>( "DmxOut", DmxActor::capabilities );
    sphactor_register<IntSlider>( "IntSlider", IntSlider::capabilities );
    sphactor_register<FloatSlider>( "FloatSlider", FloatSlider::capabilities );
#endif
#ifdef PYTHON3_FOUND
    int rc = python_init();
    assert( rc == 0);
    /* Check newer version: We should make this async as it slows startup" */
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject *pUpdateBool = python_call_file_func("checkver", "check_github_newer_commit", "(s)", GIT_HASH);
    if (pUpdateBool && PyObject_IsTrue(pUpdateBool))
        GZB_GLOBAL.UPDATE_AVAIL = true;

    PyGILState_Release(gstate);
    /* End check newer version */
#endif
#ifdef GSTREAMER_FOUND
    sphactor_register("Gstreamer", &gstreameractor_handler, zconfig_str_load(gstreameractorcapabilities), &gstreameractor_new_helper, NULL);
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
        Init();
    }

    ImGui::Checkbox("Show Log Window", showLog);

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::End();
}


/* Ref: https://github.com/ocornut/imgui/issues/3606
 * These guys are amazing! Just copied one of the entries
 */
#define V2 ImVec2
#define F float
V2 conv(V2 v, F z, V2 sz, V2 o){return V2((v.x/z)*sz.x*5.f+sz.x*0.5f,(v.y/z)*sz.y*5.f+sz.y*0.5f)+o;}
V2 R(V2 v, F ng){ng*=0.1f;return V2(v.x*cosf(ng)-v.y*sinf(ng),v.x*sinf(ng)+v.y*cosf(ng));}
void FX(ImDrawList* d, V2 o, V2 b, V2 sz, ImVec4, F t) {
    d->AddRectFilled(o,b,0xFF000000,0);
    t*=4;
    for (int i = 0; i < 20; i++) {
        F z=21.-i-(t-floorf(t))*2.,ng=-t*2.1+z,ot0=-t+z*0.2,ot1=-t+(z+1.)*0.2,os=0.3;
        V2 s[]={V2(cosf((t+z)*0.1)*0.2+1.,sinf((t+z)*0.1)*0.2+1.),V2(cosf((t+z+1.)*0.1)*0.2+1.,sinf((t+z+1.)*0.1)*0.2+1.)};
        V2 of[]={V2(cosf(ot0)*os,sinf(ot0)*os),V2(cosf(ot1)*os,sinf(ot1)*os)};
        V2 p[]={V2(-1,-1),V2(1,-1),V2(1,1),V2(-1,1)};
        ImVec2 pts[8];int j;
        for (j=0;j<8;j++) {
            int n = (j/4);pts[j]=conv(R(p[j%4]*s[n]+of[n],ng+n),(z+n)*2.,sz,o);
        }
        for (j=0;j<4;j++){
            V2 q[4]={pts[j],pts[(j+1)%4],pts[((j+1)%4)+4],pts[j+4]};
            F it=(((i&1)?0.5:0.6)+j*0.05)*((21.-z)/21.);
            d->AddConvexPolyFilled(q,4,ImColor::HSV(0.6+sinf(t*0.03)*0.5,1,sqrtf(it)));
        }
    }
}

void ShowAboutWindow(bool *open)
{
    ImGuiIO& io = ImGui::GetIO();
    //TODO: try to center window, doesn't work somehow
    ImGui::SetNextWindowPos(ImGui::GetWindowSize()/2, ImGuiCond_Once, ImVec2(0.5,0.5));
    ImGui::Begin("About", open, ImGuiWindowFlags_AlwaysAutoResize);
    ImVec2 size(320.0f, 180.0f);
    ImGui::InvisibleButton("canvas", size);
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(p0, p1);

    ImVec4 mouse_data;
    mouse_data.x = (io.MousePos.x - p0.x) / size.x;
    mouse_data.y = (io.MousePos.y - p0.y) / size.y;
    mouse_data.z = io.MouseDownDuration[0];
    mouse_data.w = io.MouseDownDuration[1];
    float time = (float)ImGui::GetTime();
    FX(draw_list, p0, p1, size, mouse_data, time);
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize()*4.f, p0 + ImVec2(16,120), ImColor::HSV(mouse_data.x,mouse_data.x,0.9), "GazebOsc");
    //draw_list->AddText(p0 + size/2, IM_COL32(255,0,255,255), "GazebOsc");
    //draw_list->AddCircleFilled( p0 + size/2, 10.f, IM_COL32_WHITE, 8);
    draw_list->PopClipRect();
    ImGui::Text("Gazebosc: " GIT_VERSION);
    ImGui::Text("ZeroMQ: %i.%i.%i", ZMQ_VERSION_MAJOR, ZMQ_VERSION_MINOR, ZMQ_VERSION_PATCH);
    ImGui::Text("CZMQ: %i.%i.%i", CZMQ_VERSION_MAJOR, CZMQ_VERSION_MINOR, CZMQ_VERSION_PATCH);
    ImGui::Text("Sphactor: %i.%i.%i", SPHACTOR_VERSION_MAJOR, SPHACTOR_VERSION_MINOR, SPHACTOR_VERSION_PATCH);
    ImGui::Text("Dear ImGui: %s", IMGUI_VERSION);
    if (ImGui::CollapsingHeader("System Info"))
    {
        if (nics == NULL)
            nics = ziflist_new();

        static ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;

        if (ImGui::BeginTable("interfaces", 5, flags))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("MacAddress");
            ImGui::TableSetupColumn("IpAddress");
            ImGui::TableSetupColumn("Netmask");
            ImGui::TableSetupColumn("Broadcast");
            ImGui::TableHeadersRow();

            const char *name = ziflist_first (nics);
            while (name)
            {
                ImGui::TableNextRow();
                // name
                ImGui::TableSetColumnIndex(0);
                SelectableText(name);

                // mac
                char buf[32];
                ImGui::TableSetColumnIndex(1);
                sprintf(buf, "%s", ziflist_mac(nics) );
                SelectableText(buf);
                // address
                ImGui::TableSetColumnIndex(2);
                sprintf(buf, "%s", ziflist_address (nics));
                SelectableText(buf);
                // netmask
                ImGui::TableSetColumnIndex(3);
                sprintf(buf, "%s", ziflist_netmask (nics));
                SelectableText(buf);
                // broadcast
                ImGui::TableSetColumnIndex(4);
                //char buf[32];
                sprintf(buf, "%s", ziflist_broadcast (nics));
                SelectableText(buf);

                name = ziflist_next (nics);
            }
            ImGui::EndTable();
        }
        if ( ImGui::Button("refresh interfaces") )
        {
            ziflist_reload(nics);
        }

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

void OpenTextEditor(zfile_t* txtfile)
{    
    static auto lang = TextEditor::LanguageDefinition::Python();

    assert(txtfile);
    textfile * found = nullptr;
    for (auto it = open_text_files.begin(); it != open_text_files.end(); ++it)
    {
        if (streq(zfile_filename(it->file, NULL), zfile_filename(txtfile, nullptr)))
        {
            found = &(*it);
            break; // file already exists in the editor
        }
    }
    if (found == nullptr)
    {
        zsys_info("TextEditor: loading & new textfile");
        // we own the pointer now!
        int rc = zfile_input(txtfile);
        assert(rc == 0);
        zchunk_t* data = zfile_read(txtfile, zfile_cursize(txtfile), 0);
        TextEditor* editor = new TextEditor();
        editor->SetLanguageDefinition(lang);
        editor->SetText((char*)zchunk_data(data));
        char* basename = s_basename(zfile_filename(txtfile, nullptr));
        // add the new file to our list of open files
        open_text_files.push_back({ editor, txtfile, basename, true, false });
        hardswap_editor = &(open_text_files.back());
    }
    else {
        zsys_info("TextEditor: existing text file found, activating the correct tab?");
        hardswap_editor = found;
        // cleanup not used txtfile
        zfile_destroy(&txtfile);
    }
    txteditor_open = true;
}

void ShowTextEditor()
{
    if (ImGui::Begin("Texteditor", &txteditor_open, ImGuiWindowFlags_MenuBar)) {

        ImGui::BeginMenuBar();

        if (ImGui::BeginMenu("File")) {
            
            bool file_selected = false;

            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Load"))
                file_selected = true;

            if (file_selected)
                ImGui::OpenPopup("Actor Open File");

            // open the load dialog
            if (actor_file_dialog.showFileDialog("Actor Open File",
                imgui_addons::ImGuiFileBrowser::DialogMode::OPEN,
                ImVec2(700, 310),
                "*.*")) // TODO: perhaps add type hint for extensions?
            {
                zfile_t* f = zfile_new(nullptr, actor_file_dialog.selected_path.c_str());
                if (actor_file_dialog.selected_path.length() > 0 && f)
                {
                    OpenTextEditor(f);
                }
                else
                    zsys_error("no valid file to load: %s", actor_file_dialog.selected_path.c_str());
            }
            if (ImGui::MenuItem(ICON_FA_SAVE " Save")) {
                if (current_editor != nullptr) {
                    // the zfile class is missing truncating files on write if the file is smaller than the old file
                    // see https://github.com/zeromq/czmq/issues/2203
                    ssize_t oldSize = zfile_size(zfile_filename(current_editor->file, NULL));
                    int rc = zfile_output(current_editor->file);
                    if (rc == 0) {
                        std::string text = current_editor->editor->GetText();
                        zchunk_t* data = zchunk_frommem((void *)text.c_str(), strlen(text.c_str()), nullptr, nullptr);
                        int rc = zfile_write(current_editor->file, data, 0);
                        if (oldSize > text.length() )
                        {
#ifdef __WINDOWS__          // truncate the file
                            if ( _chsize_s(fileno(zfile_handle(current_editor->file)), (__int64)text.length()) != 0 )
                            {
                                zsys_error("Some error trying to truncate the file");
                            }
#else
                            ftruncate(fileno(zfile_handle(current_editor->file)), text.length());
#endif
                        }
                        zfile_close(current_editor->file);
                        if (rc != 0) {
                            zsys_info("ERROR WRITING TO FILE: %i", rc);
                        }
                        else
                            current_editor->changed = false;
                    }
                }
            }
            // TODO: support save as?
            //if (ImGui::MenuItem(ICON_FA_SAVE " Save As")) {
            //
            //}
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_WINDOW_CLOSE " Exit")) {
                //TODO: support checking if changes were made
                txteditor_open = false;
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            bool ro = current_editor->editor->IsReadOnly();
            if (ImGui::MenuItem("Read-only mode", nullptr, &ro))
                current_editor->editor->SetReadOnly(ro);
            ImGui::Separator();

            if (ImGui::MenuItem("Undo", "ALT-Backspace", nullptr, !ro && current_editor->editor->CanUndo()))
                current_editor->editor->Undo();
            if (ImGui::MenuItem("Redo", "Ctrl-Y", nullptr, !ro && current_editor->editor->CanRedo()))
                current_editor->editor->Redo();

            ImGui::Separator();

            if (ImGui::MenuItem("Copy", "Ctrl-C", nullptr, current_editor->editor->HasSelection()))
                current_editor->editor->Copy();
            if (ImGui::MenuItem("Cut", "Ctrl-X", nullptr, !ro && current_editor->editor->HasSelection()))
                current_editor->editor->Cut();
            if (ImGui::MenuItem("Delete", "Del", nullptr, !ro && current_editor->editor->HasSelection()))
                current_editor->editor->Delete();
            if (ImGui::MenuItem("Paste", "Ctrl-V", nullptr, !ro && ImGui::GetClipboardText() != nullptr))
                current_editor->editor->Paste();

            ImGui::Separator();

            if (ImGui::MenuItem("Select all", nullptr, nullptr))
                current_editor->editor->SetSelection(TextEditor::Coordinates(), TextEditor::Coordinates(current_editor->editor->GetTotalLines(), 0));

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Dark palette"))
                current_editor->editor->SetPalette(TextEditor::GetDarkPalette());
            if (ImGui::MenuItem("Light palette"))
                current_editor->editor->SetPalette(TextEditor::GetLightPalette());
            if (ImGui::MenuItem("Retro blue palette"))
                current_editor->editor->SetPalette(TextEditor::GetRetroBluePalette());
            if (ImGui::MenuItem("Mariana palette"))
                current_editor->editor->SetPalette(TextEditor::GetMarianaPalette());
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();

        ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_AutoSelectNewTabs;
        if (ImGui::BeginTabBar("TextEditorTabBar", tab_bar_flags))
        {
            for (auto it = open_text_files.begin(); it != open_text_files.end(); ++it)
            {
                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (hardswap_editor && &(*it) == hardswap_editor) {
                    flags |= ImGuiTabItemFlags_SetSelected;
                    hardswap_editor = nullptr;
                }
                // TODO: when loading the textfile this will always
                // result in text being changed.
                if ( !it->changed && it->editor->IsTextChanged() ) it->changed = true;

                char filename[FILENAME_MAX];
                if ( it->changed )
                    snprintf(filename, FILENAME_MAX, "%s*###%s", it->basename, it->basename);
                else
                    snprintf(filename, FILENAME_MAX, "%s###%s", it->basename, it->basename);

                if (ImGui::BeginTabItem(filename, &it->open, flags))
                {
                    current_editor = &(*it); // set current textfile from active tab
                    current_editor->editor->Render("TextEditor");//, false, ImVec2(), false);
                    ImGui::EndTabItem();
                }
                else if (!it->open)
                {
                    // file is closed by gui do we need to save it?
                    // this doesn't work, mTextChanged is set to false when rendered
                    /*if ( it->editor->IsTextChanged() )
                    {
                        zsys_debug("need to save file %s", it->basename );
                    }*/
                    zsys_debug("remove file %s", it->basename);
                    open_text_files.erase(it);
                    current_editor = nullptr;
                    break;
                }
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
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
#ifdef HAVE_IMGUI_DEMO
        if ( ImGui::MenuItem(ICON_FA_CARAVAN " Toggle Demo") ) {
            showDemo = !showDemo;
        }
#endif
        if ( ImGui::MenuItem(ICON_FA_INFO " Toggle About") ) {
            showAbout = !showAbout;
        }

        ImGui::EndMenu();
    }

    //TODO: Display stage status (new, loaded, changed)
    ImGui::Separator();
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);

    ImGui::Separator();
    ImGui::TextColored( ImVec4(.3,.5,.3,1), ICON_FA_FOLDER ": %s", path);

    if ( ImGui::IsItemHovered() )
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted("Current working directory, double click to open");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();

        if ( ImGui::IsMouseDoubleClicked(0) )
        {
            char cmd[PATH_MAX];
#ifdef __WINDOWS__
            snprintf(cmd, PATH_MAX, "start explorer %s", path);
            //snprintf(cmd, PATH_MAX, "explorer.exe %s", path);
            //STARTUPINFO startupInfo = {0};
            //startupInfo.cb = sizeof(startupInfo);
            //PROCESS_INFORMATION processInformation;
            //CreateProcess(NULL, cmd, NULL, NULL, false, 0, NULL, NULL, &startupInfo, &processInformation);
#elif defined __UTYPE_LINUX
            snprintf(cmd, PATH_MAX, "xdg-open %s &", path);
#else
            snprintf(cmd, PATH_MAX, "open %s", path);
#endif
            system(cmd);
        }
    }

    ImGui::Separator();


    if ( streq( editingFile.c_str(), "" ) ) {
        ImGui::TextColored( ImVec4(.7,.9,.7,1), ICON_FA_EDIT ": *Unsaved Stage*");
    }
    else {
        ImGui::TextColored( ImVec4(.7,.9,.7,1), ICON_FA_EDIT ": %s", editingFile.c_str());
    }

    if (GZB_GLOBAL.UPDATE_AVAIL)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 64);
        ImGui::TextColored( ImVec4(.99,.9,.7,1), "update " ICON_FA_INFO_CIRCLE );
        if ( ImGui::IsItemHovered() )
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted("Gazebosc update available, click to download");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();

            if ( ImGui::IsMouseClicked(0) )
            {
                char cmd[PATH_MAX];
                char url[60] = "https://pong.hku.nl/~buildbot/gazebosc/";
    #ifdef __WINDOWS__
                snprintf(cmd, PATH_MAX, "start %s", url);
    #elif defined __UTYPE_LINUX
                snprintf(cmd, PATH_MAX, "xdg-open %s &", url);
    #else
                snprintf(cmd, PATH_MAX, "open %s", url);
    #endif
                system(cmd);
            }
        }
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
        Init();
    }
    else if ( action == MenuAction_Exit ) {
        return -1;
    }

    if(file_dialog.showFileDialog("MenuAction_Load", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".gzs"))
    {
        if (Load(file_dialog.selected_path.c_str())) {
            editingFile = file_dialog.selected_fn;
            editingPath = file_dialog.selected_path;

            moveCwdIfNeeded();
        }
    }

    if(file_dialog.showFileDialog("MenuAction_Save", imgui_addons::ImGuiFileBrowser::DialogMode::SAVE, ImVec2(700, 310), ".gzs"))
    {
        if ( !Save(file_dialog.selected_path.c_str()) ) {
            editingFile = "";
            editingPath = "";
            zsys_error("Saving file failed!");
        }
        else {
            editingFile = file_dialog.selected_fn;
            editingPath = file_dialog.selected_path;

            moveCwdIfNeeded();
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
    static std::vector<ActorContainer*> selectedActors;
    static std::vector<char *> actorClipboardType;
    static std::vector<char *> actorClipboardCapabilities;
    static std::vector<ImVec2> actorClipboardPositions;
    static bool D_PRESSED = false;

    int numKeys;
    byte * keyState = (byte*)SDL_GetKeyboardState(&numKeys);

    int rc = 0;
    static ImNodes::Ez::Context* context = ImNodes::Ez::CreateContext();
    IM_UNUSED(context);

    ImGui::SetNextWindowPos(ImVec2(0,0));

    ImGuiIO &io = ImGui::GetIO();
    bool copy = ImGui::IsKeyPressedMap(ImGuiKey_C) && (io.KeySuper || io.KeyCtrl);
    bool paste = ImGui::IsKeyPressedMap(ImGuiKey_V) && (io.KeySuper || io.KeyCtrl);
    bool dup = ( keyState[SDL_SCANCODE_D] == 1 && !D_PRESSED) && (io.KeySuper || io.KeyCtrl);
    bool undo = ImGui::IsKeyPressedMap(ImGuiKey_Z) && (io.KeySuper || io.KeyCtrl);
    bool redo = ImGui::IsKeyPressedMap(ImGuiKey_Y) && (io.KeySuper || io.KeyCtrl);
    bool del = ImGui::IsKeyPressedMap(ImGuiKey_Delete) || (io.KeySuper && ImGui::IsKeyPressedMap(ImGuiKey_Backspace));

    D_PRESSED = keyState[SDL_SCANCODE_D] == 1;

    if ( undo ) {
        if ( undoStack.size() > 0 ) {
            UndoData top = undoStack.top();
            performUndo( top );
            swapUndoType(&top);
            redoStack.push(UndoData(&top));
            undoStack.pop();
        }
    }
    else if ( redo ) {
        if ( redoStack.size() > 0 ) {
            UndoData top = redoStack.top();
            performUndo( top );
            swapUndoType(&top);
            undoStack.push(UndoData(&top));
            redoStack.pop();
        }
    }
    else if (del) {
        // zsys_info("DEL");
        // Loop and delete connections of actors connected to us
        for (auto it = std::begin(selectedActors); it != std::end(selectedActors); it++)
        {
            ActorContainer* actor = *it;
            for (auto& connection : actor->connections) {
                if (connection.output_node == actor) {
                    ((ActorContainer*)connection.input_node)->DeleteConnection(connection);
                }
                else {
                    ((ActorContainer*)connection.output_node)->DeleteConnection(connection);
                }
                RegisterDisconnectAction((ActorContainer*)connection.input_node, (ActorContainer*)connection.output_node, connection.input_slot, connection.output_slot);
            }

            RegisterDeleteAction(actor);

            // Delete all our connections separately
            actor->connections.clear();
            sph_stage_remove_actor(stage, zuuid_str(sphactor_ask_uuid(actor->actor)));

            // find and remove the actor from the actors list
            for (int i = 0; i < actors.size(); ++i )
            {
                if (actors.at(i) == actor) actors.erase(actors.begin() + i--);
            }
            delete actor;
        }
    }
    else if ( selectedActors.size() > 0 && copy && !ImGui::IsAnyItemActive() )
    {
        // copy actor data to clipboard char *'s
        // zsys_info("COPY" );
        for( int i = 0; i < actorClipboardType.size(); ++i ) {
            free(actorClipboardType.at(i));
            free(actorClipboardCapabilities.at(i));
        }
        actorClipboardType.clear();
        actorClipboardCapabilities.clear();
        actorClipboardPositions.clear();

        for( int i = 0; i < selectedActors.size(); ++i ) {
            ActorContainer * selectedActor = selectedActors.at(i);

            actorClipboardCapabilities.push_back(zconfig_str_save(selectedActor->capabilities));

            int length = strlen(selectedActor->title);
            actorClipboardType.push_back(new char[length+1]);
            strncpy(actorClipboardType.back(), selectedActor->title, length);
            actorClipboardType.back()[length] = '\0';

            actorClipboardPositions.push_back(ImVec2(selectedActor->pos - io.MousePos));
        }
    }
    else if ( actorClipboardType.size() > 0 && paste && !ImGui::IsAnyItemActive() ) {
        // create actor(s) from clipboard
        // clear clipboard
        for( int i = 0; i < actorClipboardType.size(); ++i ) {
            char * type = actorClipboardType.at(i);
            char * capabilities = actorClipboardCapabilities.at(i);

            // zsys_info("PASTE: %s", type);

            int max = -1;
            if ( max_actors_by_type.find(type) != max_actors_by_type.end() )
                max = max_actors_by_type.at(type);

            if ( CountActorsOfType(type) < max || max == -1) {
                ActorContainer *actor = CreateFromType(type, nullptr);
                actor->SetCapabilities(capabilities);
                actor->pos = io.MousePos + actorClipboardPositions.at(i);
                actors.push_back(actor);

                RegisterCreateAction(actor);
            }
        }

        // TODO: figure out when we need to clear this (if ever)
        /*
        free(actorClipboardType);
        free(actorClipboardCapabilities);
        actorClipboardType = nullptr;
        actorClipboardCapabilities = nullptr;
        */
    }
    else if ( selectedActors.size() > 0 && dup && !ImGui::IsAnyItemActive() ) {
        // zsys_info("DUP");
        std::vector<char *> dupClipboardType;
        std::vector<char *> dupClipboardCapabilities;
        std::vector<ImVec2> dupClipboardPositions;

        ImVec2 center;

        for( int i = 0; i < selectedActors.size(); ++i ) {
            ActorContainer * selectedActor = selectedActors.at(i);

            dupClipboardCapabilities.push_back(zconfig_str_save(selectedActor->capabilities));

            int length = strlen(selectedActor->title);
            dupClipboardType.push_back(new char[length+1]);
            strncpy(dupClipboardType.back(), selectedActor->title, length);
            dupClipboardType.back()[length] = '\0';

            center += selectedActor->pos;
        }

        center /= selectedActors.size();
        for( int i = 0; i < selectedActors.size(); ++i ) {
            ActorContainer * selectedActor = selectedActors.at(i);
            dupClipboardPositions.push_back(ImVec2(selectedActor->pos - center));
        }

        selectedActors.clear();

        for( int i = 0; i < dupClipboardType.size(); ++i ) {
            char * type = dupClipboardType.at(i);
            char * capabilities = dupClipboardCapabilities.at(i);

            // zsys_info("PASTE: %s", type);

            int max = -1;
            if ( max_actors_by_type.find(type) != max_actors_by_type.end() )
                max = max_actors_by_type.at(type);

            if ( CountActorsOfType(type) < max || max == -1) {
                ActorContainer *actor = CreateFromType(type, nullptr);
                actor->SetCapabilities(capabilities);
                actor->pos = io.MousePos + dupClipboardPositions.at(i) + ImVec2(10,10);
                actors.push_back(actor);

                // TODO: SELECT DUPLICATED ACTORS!?
                // selectedActors.push_back(actor);

                RegisterCreateAction(actor);
            }
        }
    }

    selectedActors.clear();
    if (ImGui::Begin("ImNodes", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_MenuBar ))
    {
        rc = RenderMenuBar(showLog);

        // We probably need to keep some state, like positions of nodes/slots for rendering connections.
        ImNodes::Ez::BeginCanvas();

        for (auto it = std::begin(actors); it != std::end(actors); it++)
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

                    RegisterConnectAction((ActorContainer*)new_connection.input_node, (ActorContainer*)new_connection.output_node, new_connection.input_slot, new_connection.output_slot);
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

                        RegisterDisconnectAction((ActorContainer*)connection.input_node, (ActorContainer*)connection.output_node, connection.input_slot, connection.output_slot);
                    }
                    else
                    {
                        // Animate the bezier vertex colors for recently active connections
                        int64_t diff = zclock_mono() - ((ActorContainer*) connection.output_node)->lastActive;
                        ImNodes::CanvasState *canvas = ImNodes::GetCurrentCanvas();
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
                                                          canvas->Colors[ImNodes::ColConnection], index,
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

            if ( actor->selected) {
                selectedActors.push_back(actor); 
            }
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
                            RegisterCreateAction(actor);
                        }
                    }
                    else {
                        ActorContainer *actor = CreateFromType(desc, nullptr);
                        actors.push_back(actor);
                        ImNodes::AutoPositionNode(actors.back());
                        RegisterCreateAction(actor);
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Zoom"))
                ImNodes::GetCurrentCanvas()->Zoom = 1;

            if (ImGui::IsAnyMouseDown() && !ImGui::IsWindowHovered())
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImNodes::Ez::EndCanvas();
    }
    ImGui::End();

    if (showAbout) ShowAboutWindow(&showAbout);

    if (txteditor_open) ShowTextEditor();
#ifdef HAVE_IMGUI_DEMO
    // ImGui Demo window for dev purposes
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
#endif
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

bool Load( const char* configFile )
{
    undoStack = std::stack<UndoData>();
    redoStack = std::stack<UndoData>();

    // Clear current stage
    // TODO: Maybe ask if people want to save first?
    if (stage)
        Clear();

    stage = sph_stage_load(configFile);
    if ( stage == NULL )
        return false;

    // clear active file as it needs saving to become a file first
    editingFile = std::string(configFile);
    editingPath = std::filesystem::path( std::filesystem::current_path() /= std::string(configFile)).string();

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
                new_connection.input_slot = gActor->input_slots[0].title;
                new_connection.output_node = peer_actor_container;
                new_connection.output_slot = peer_actor_container->output_slots[0].title;
                ((ActorContainer*) new_connection.input_node)->connections.push_back(new_connection);
                ((ActorContainer*) new_connection.output_node)->connections.push_back(new_connection);
            }
        }
        ++it;
    }

    return stage != NULL;
}

void Init() {
    // initialise an empty stage with tmp working dir
    assert(stage == NULL);

    // set a temporary random working dir for our stage
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char tmpdir[12] = "_gzs_xxxxxx";
    for (int i=5;i<12;i++)
    {
        int key = rand() % (int)(sizeof(charset)-1);
        tmpdir[i] = charset[key];
    }
    tmpdir[11] = 0; // null termination
    fs::path tmppath(GZB_GLOBAL.TMPPATH);
    tmppath.append(tmpdir);
    std::error_code ec;
    if ( ! fs::create_directory(tmppath, ec) )
    {
        // TODO: what to do if creating the dir fails?
        zsys_error("Creating tmp dir %s failed, this might mean trouble!!!", tmppath.string().c_str() );
    }
    else
    {
        fs::current_path(tmppath);
        zsys_info("Temporary stage dir is now at %s", tmppath.string().c_str());
    }
    // clear active file as it needs saving to become a file first
    editingFile = "";
    editingPath = "";
}

void Clear() {
    undoStack = std::stack<UndoData>();
    redoStack = std::stack<UndoData>();

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

    sph_stage_destroy(&stage);
    // remove working dir if it's a temporary path
    std::error_code ec; // no exception
    fs::path cwd = fs::current_path(ec);
    // temporary change the working dir otherwise we cannot delete it on windows, Load or Init will reset it
    fs::current_path(GZB_GLOBAL.TMPPATH);

    fs::path tmppath(GZB_GLOBAL.TMPPATH);
    tmppath.append("_gzs_");
    if ( cwd.string().rfind(tmppath.string(), 0) == 0 )
    {
        try
        {
            fs::remove_all(cwd);
        }
        catch (std::exception& e)
        {
            zsys_error("failed to remove working dir: %s", e.what());
        }
    }
    assert(stage == NULL);

    //delete all actors
    for (auto it = actors.begin(); it != actors.end();)
    {
        ActorContainer* actor = *it;
        // sphactor is already destroyed by stage
        delete actor;
        it = actors.erase(it);
    }
    // clear active file and set new path
    editingFile = "";
    editingPath = "";
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

void performUndo(UndoData &undo) {
    switch( undo.type) {
        case UndoAction_Delete: {
            zsys_info("UNDO DELETE");
            sphactor_t * sphactor_actor = sphactor_load(undo.sphactor_config);
            sph_stage_add_actor(stage, sphactor_actor);

            ActorContainer *actor = new ActorContainer( sphactor_actor );
            //zsys_info("UUID: %s", zuuid_str(sphactor_ask_uuid(sphactor_actor)));
            actor->pos = undo.position;
            actors.push_back(actor);
            break;
        }
        case UndoAction_Create: {
            zsys_info("UNDO CREATE");
            for( auto it = actors.begin(); it != actors.end(); it++) {
                ActorContainer* actor = *it;
                if ( streq( zuuid_str(sphactor_ask_uuid(actor->actor)), undo.uuid)) {
                    // Delete all our connections separately
                    actor->connections.clear();
                    sph_stage_remove_actor(stage, zuuid_str(sphactor_ask_uuid(actor->actor)));

                    delete actor;
                    actors.erase(it);
                    break;
                }
            }
            break;
        }
        case UndoAction_Connect: {
            zsys_info("UNDO CONNECT");
            ActorContainer *out = nullptr, *in = nullptr;
            for( auto it = actors.begin(); it != actors.end(); it++) {
                ActorContainer* actor = *it;
                if ( streq( zuuid_str(sphactor_ask_uuid(actor->actor)), undo.uuid)) {
                    in = *it;
                }
                else if ( streq( sphactor_ask_endpoint(actor->actor), undo.endpoint)) {
                    out = *it;
                }
                if (out && in) break;
            }

            if ( !out || !in ) {
                zsys_info("COULD NOT FIND ONE OF THE NODES");
                break;
            }

            // find and delete connections
            for( auto it = in->connections.begin(); it != in->connections.end(); it++ ) {
                ActorContainer *actor = (ActorContainer*)(*it).output_node;
                if ( (*it).output_node == out) {
                    in->connections.erase(it);
                    break;
                }
            }
            for( auto it = out->connections.begin(); it != out->connections.end(); it++ ) {
                ActorContainer *actor = (ActorContainer*)(*it).input_node;
                if ( (*it).input_node == in) {
                    out->connections.erase(it);
                    break;
                }
            }

            // disconnect
            sphactor_ask_disconnect(in->actor, undo.endpoint);

            break;
        }
        case UndoAction_Disconnect: {
            zsys_info("UNDO DISCONNECT");
            ActorContainer *out = nullptr, *in = nullptr;
            for( auto it = actors.begin(); it != actors.end(); it++) {
                ActorContainer* actor = *it;
                if ( streq( zuuid_str(sphactor_ask_uuid(actor->actor)), undo.uuid)) {
                    in = *it;
                }
                else if ( streq( sphactor_ask_endpoint(actor->actor), undo.endpoint)) {
                    out = *it;
                }
                if (out && in) break;
            }

            if ( !out || !in ) {
                zsys_info("COULD NOT FIND ONE OF THE NODES");
                break;
            }

            //rebuild connection
            Connection new_connection;
            new_connection.input_slot = undo.input_slot;
            new_connection.output_slot = undo.output_slot;
            new_connection.input_node = in;
            new_connection.output_node = out;

            in->connections.push_back(new_connection);
            out->connections.push_back(new_connection);

            //connect
            sphactor_ask_connect(in->actor, undo.endpoint);

            break;
        }
        default: {
            break;
        }
    }
}

void swapUndoType(UndoData * undo) {
    switch(undo->type) {
        case UndoAction_Connect: {
            undo->type = UndoAction_Disconnect;
            break;
        }
        case UndoAction_Disconnect: {
            undo->type = UndoAction_Connect;
            break;
        }
        case UndoAction_Create: {
            undo->type = UndoAction_Delete;
            break;
        }
        case UndoAction_Delete: {
            undo->type = UndoAction_Create;
            break;
        }
    }
}

void RegisterCreateAction( ActorContainer * actor ) {
    if ( redoStack.size() > 0 ) {
        redoStack = std::stack<UndoData>();
    }

    UndoData undo;
    undo.type = UndoAction_Create;
    undo.title = strdup(actor->title);
    undo.sphactor_config = sphactor_save(actor->actor, nullptr);
    undo.uuid = strdup(zuuid_str(sphactor_ask_uuid(actor->actor)));
    zsys_info("UUID: %s", undo.uuid);
    ImGuiIO& io = ImGui::GetIO();
    if (actor->pos.x == 0 && actor->pos.y == 0) {
        undo.position = ImVec2(io.MousePos.x, io.MousePos.y);
    }
    else {
        undo.position = actor->pos;
    }
    
    undoStack.push(undo);
}

void RegisterDeleteAction( ActorContainer * actor ) {
    if ( redoStack.size() > 0 ) {
        redoStack = std::stack<UndoData>();
    }

    UndoData undo;
    undo.type = UndoAction_Delete;
    undo.title = strdup(actor->title);
    undo.sphactor_config = sphactor_save(actor->actor, nullptr);
    undo.uuid = strdup(zuuid_str(sphactor_ask_uuid(actor->actor)));
    zsys_info("UUID: %s", undo.uuid);
    undo.position = actor->pos;
    undoStack.push(undo);
}

void RegisterConnectAction(ActorContainer * in, ActorContainer * out, const char * input_slot, const char * output_slot) {
    if ( redoStack.size() > 0 ) {
        redoStack = std::stack<UndoData>();
    }

    UndoData undo;
    undo.type = UndoAction_Connect;
    undo.uuid = strdup(zuuid_str(sphactor_ask_uuid(in->actor)));
    undo.endpoint = strdup(sphactor_ask_endpoint(out->actor));
    undo.input_slot = strdup(input_slot);
    undo.output_slot = strdup(output_slot);
    undoStack.push(undo);
}

void RegisterDisconnectAction(ActorContainer * in, ActorContainer * out, const char * input_slot, const char * output_slot) {
    if ( redoStack.size() > 0 ) {
        redoStack = std::stack<UndoData>();
    }

    UndoData undo;
    undo.type = UndoAction_Disconnect;
    undo.uuid = strdup(zuuid_str(sphactor_ask_uuid(in->actor)));
    undo.endpoint = strdup(sphactor_ask_endpoint(out->actor));
    undo.input_slot = strdup(input_slot);
    undo.output_slot = strdup(output_slot);
    undoStack.push(undo);
}
