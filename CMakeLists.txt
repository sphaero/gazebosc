#
# Copyright (c) 2019 Arnaud Loonstra.
#
# License MPLv2
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

cmake_minimum_required(VERSION 3.13)
project(gazebosc)

if(NOT ARCH)
  if(CMAKE_SIZEOF_VOID_P MATCHES 8)
    set(ARCH 64)
  else()
    set(ARCH 32)
  endif()
endif()
message(STATUS "Compilation set for ${ARCH}bits architectures.")

set(CMAKE_CXX_STANDARD 14)
set(DEP_IMGUI_VERSION "1.85" CACHE STRING "ImGui version to use")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/dist/cmake")

##  Test availability of git and fetch submodules if needed
find_package(Git QUIET)
#if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
#    # Update submodules as needed
#    option(GIT_SUBMODULE "Check submodules during build" ON)
#    if(GIT_SUBMODULE)
#        message(STATUS "Submodule update")
#        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
#            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#            RESULT_VARIABLE GIT_SUBMOD_RESULT)
#        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
#            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
#        endif()
#    endif()
#endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ext/czmq/CMakeLists.txt")
    message(FATAL_ERROR "The submodules in the ext directory are not available! GIT_SUBMODULE was turned off or failed. Please update submodules using \"git submodule update --init --recursive\" and try again.")
endif()
## End getting the submodules

### SET OPTIONS
option (WITH_DEV "Enable running from an IDE" ON)
option (WITH_UNWIND "Enable libunwind support" ON)
option (WITH_IMGUI_DEMO "Enable ImGUI demo console" ON)
option (WITH_EMBED_PYTHON "Embed the python interpreter, otherwise use system installed" OFF)
option (WITH_OPENVR "Enable OpenVR support" ON)
option (WITH_DMX "Enable DMX support" ON)
### END SET OPTIONS

### EXTERNAL LIBS
# Definitions for draft API's of submodules
set(ENABLE_DRAFTS ON CACHE BOOL "enable zmq projects draft apis" FORCE)
add_definitions(-DZMQ_BUILD_DRAFT_API=1)
add_definitions(-DCZMQ_BUILD_DRAFT_API=1)

# External libraries in ext/
add_subdirectory("ext/czmq" EXCLUDE_FROM_ALL)
# overrides to prevent the Findczmq.cmake script to fail
set(czmq_LIBRARIES czmq-static)
set(czmq_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/ext/czmq/include/)
add_subdirectory("ext/libsphactor" EXCLUDE_FROM_ALL)
add_subdirectory("ext/SDL-mirror" EXCLUDE_FROM_ALL)
add_subdirectory("ext/glm" EXCLUDE_FROM_ALL)
add_subdirectory("ext/pybind11" EXCLUDE_FROM_ALL)
set( RTMIDI_TARGETNAME_UNINSTALL "rtmidi_uninstall" CACHE STRING "Name of 'uninstall' build target")
set( RTMIDI_BUILD_SHARED_LIBS "")
set( RTMIDI_BUILD_STATIC_LIBS "#" )
add_subdirectory("ext/rtmidi" EXCLUDE_FROM_ALL)

# Test for RPI
# We can check if the bcm_host.h exists
# include(CheckIncludeFile)
# CHECK_INCLUDE_FILE(/opt/vc/include/bcm_host.h RPI) # doesn't work :S
# Just check if the file is there
if (EXISTS "/opt/vc/include/bcm_host.h")
    message(STATUS "Enabling building for the Raspberry Pi")
    set(RPI "ON")
    add_definitions(-DIMGUI_IMPL_OPENGL_ES2)
endif(EXISTS "/opt/vc/include/bcm_host.h")

# External system libraries through find_package
find_package(libzmq REQUIRED)
if (NOT libzmq_FOUND)
    message (FATAL_ERROR "Cannot find libzmq, make sure it is available on the system")
endif()

# Find Python3 ref: https://gitlab.kitware.com/cmake/cmake/-/issues/20674
set(Python3_USE_STATIC_LIBS FALSE)
if(Python3_ROOT_DIR)                        # Usually specified on command line to find python explicitly
    set(CMAKE_FIND_USE_CMAKE_PATH FALSE)
    set(Python3_FIND_STRATEGY "LOCATION")
    set(Python3_FIND_REGISTRY NEVER)
endif(Python3_ROOT_DIR)
find_package (Python3 COMPONENTS Development REQUIRED)
if(Python3_FOUND)
    message ("python3 found libs: ${Python3_LIBRARIES} incl: ${Python3_INCLUDE_DIRS}")
    add_definitions(-DPYTHON3_FOUND)
else()
    message (FATAL_ERROR "Cannot find Python3, make sure the development packages of Python are available on the system")
endif()

if (WITH_UNWIND)
    find_package (Unwind)
endif (WITH_UNWIND)
if (Unwind_FOUND)
    add_definitions(-DHAVE_LIBUNWIND)
    set (HAVE_LIBUNWIND 1)
    set (HAVE_UNWIND_H 1)
endif (Unwind_FOUND)

if (WITH_IMGUI_DEMO)
    add_definitions(-DHAVE_IMGUI_DEMO)
endif (WITH_IMGUI_DEMO)


if (WITH_OPENVR)
    if (WIN32)
       set(WINDOWS_PATH_SUFFIXES win${ARCH} Win${ARCH} x${ARCH})
    endif(WIN32)
    ## OpenVR API path
    find_library(OPENVR_LIBRARIES
      NAMES
        openvr_api
      PATHS
        ${CMAKE_SOURCE_DIR}/ext/openvr/bin
        ${CMAKE_SOURCE_DIR}/ext/openvr/lib
      PATH_SUFFIXES
        osx32
        linux64
        ${WINDOWS_PATH_SUFFIXES}
      NO_DEFAULT_PATH
      NO_CMAKE_FIND_ROOT_PATH
    )
    set(OPENVR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/openvr/headers)
    add_definitions(-DHAVE_OPENVR)
    message("Found OpenVR at ${OPENVR_LIBRARIES}")
endif(WITH_OPENVR)

if(WITH_DMX)
    add_definitions(-DHAVE_DMX)
endif(WITH_DMX)

# include Dear ImGui
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    if (NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}.zip")
        file(DOWNLOAD "https://github.com/ocornut/imgui/archive/v${DEP_IMGUI_VERSION}.zip" ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}.zip)
    endif ()
    if (NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}")        
        if(${CMAKE_VERSION} VERSION_LESS "3.18.0")
            message(STATUS "cmake version below 3.18")
            execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}.zip)
        else()
            file(ARCHIVE_EXTRACT INPUT ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}.zip
                DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
            )
        endif()
    endif ()
endif ()
add_definitions("-DImTextureID=int")

### PROJECT SOURCES
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/Actors ACTOR_SOURCES)

# get git version
include(GitVersion)
message(STATUS "GIT_REPO_VERSION=${GIT_REPO_VERSION} VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_PATCH=${VERSION_PATCH}")
# create config.h
# configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dist/cmake/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h) # this is not run every build
add_custom_command(
    OUTPUT config.h
    COMMAND ${CMAKE_COMMAND}
            -D GIT_REPO_VERSION=${GIT_REPO_VERSION}
            -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/dist/cmake/config.h.in
            -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/config.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/dist/cmake/Configure.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dist/cmake/config.h.in
    VERBATIM
    )

list(APPEND GZB_SOURCES
    main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/backends/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/backends/imgui_impl_sdl.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/imconfig.h
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/imgui.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/imgui_draw.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/imgui_widgets.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/imgui_tables.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/imgui_demo.cpp
    ImNodes.cpp
    ImNodesEz.cpp
    ext/ImGui-Addons/FileBrowser/ImGuiFileBrowser.cpp
    stage.cpp
    deargui.cpp
)

list(APPEND GZB_INCLUDEDIRS
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Unwind_INCLUDE_DIRS}
    ${OPENVR_INCLUDE_DIR}
    ${libzmq_INCLUDE_DIRS}
    ${Python3_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}
    ${CMAKE_CURRENT_BINARY_DIR}/imgui-${DEP_IMGUI_VERSION}/backends
    ${pybind11_INCLUDE_DIR}
)

if(WIN32)
    list(APPEND GZB_SOURCES
        ext/StackWalker/Main/StackWalker/StackWalker.h
        ext/StackWalker/Main/StackWalker/StackWalker.cpp
    )
    list(APPEND GZB_INCLUDEDIRS
        ext/StackWalker/Main/StackWalker/
        ext/ImGui-Addons/FileBrowser/
    )
endif(WIN32)

if (UNIX)
    link_directories(
        /usr/local/lib/
    )
endif()

add_executable(gazebosc
    config.h
    ${GZB_SOURCES}
    ${ACTOR_SOURCES}
)

target_include_directories(gazebosc PUBLIC
    ${GZB_INCLUDEDIRS}
)

### BUILD OPTIONS
if (Unwind_FOUND)
  target_link_libraries (gazebosc PUBLIC unwind::unwind)
endif (Unwind_FOUND)

target_link_options(gazebosc PUBLIC ${Python3_LINK_OPTIONS})
target_compile_options(gazebosc PUBLIC ${SDL2_CFLAGS_OTHER} )
target_compile_definitions(gazebosc PUBLIC -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)

set(RESOURCE_DEST "./")
set(RUNTIME_DEST "bin")
set(APPS "bin/gazebosc")
set(LIBS )
set(DIRS "${CMAKE_BINARY_DIR}")
### PLATFORM SPECIFICS
if (APPLE)
    set(APPS "gazebosc.app")
    list(APPEND DIRS "${CMAKE_CURRENT_SOURCE_DIR}/ext/openvr/bin/osx32")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-stack-check")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-check")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/dist/osx/Info.plist.in" "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    set_target_properties(gazebosc PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        )
    set(MACOSX_BUNDLE_BUNDLE_NAME gazebosc)
    set(MACOSX_BUNDLE_COPYRIGHT "Gazebosc is licensed under the MPLv2 License")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.sphactor.Gazebosc")
    set(MACOSX_BUNDLE_ICON_FILE "AppIcon.icns")
    set(RESOURCE_DEST "gazebosc.app/Contents/Resources")
    add_definitions(-D__MACOSX_CORE__)

    target_link_libraries(gazebosc PUBLIC
        SDL2-static
        sphactor-static
        rtmidi
        ${OPENVR_LIBRARIES}
        ${Python3_LIBRARIES}
        -ldl "-framework OpenGL" "-framework CoreFoundation" "-framework CoreMidi" "-framework CoreAudio")

    # This is handled by fixup_bundle as well but it doesn't copy all required files
    if (WITH_EMBED_PYTHON )
        # copy embedded python to the bundle but exclude exe_with_* files as it kills fixup_bundle
        install(DIRECTORY ${CMAKE_BINARY_DIR}/python/Python.framework DESTINATION ${RESOURCE_DEST}/../Frameworks
            USE_SOURCE_PERMISSIONS
            PATTERN exe_with_* EXCLUDE
        )
        # Todo: fix paths in the framework, perhaps run install_name_tool manually?
        # problem is that the python binaries end up with the wrong rpaths, see otool -L python3.8
        # see: https://pong.hku.nl/redmine/issues/303#change-1041
        #install(CODE
        #        "Include(BundleUtilities)\n
        #       message(STATUS \"Fixing bundled python rpaths\")\n
        #       fixup_bundle_item(\"$\{CMAKE_INSTALL_PREFIX\}/gazebosc.app/Contents/Frameworks/Python.framework/Python\" \"$\{CMAKE_INSTALL_PREFIX\}/gazebosc.app/Contents/Frameworks/Python.framework\" \"\")"
        #)
    endif(WITH_EMBED_PYTHON)

elseif (WIN32)
    set(RUNTIME_DEST "./")
    set(APPS "gazebosc.exe")
    if (MSVC)
        string(REGEX REPLACE "/" "\\\\" VS_DEBUGGER_BACKSLASH "${CMAKE_BINARY_DIR}/bin/Debug/")
        set_property(TARGET gazebosc PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${VS_DEBUGGER_BACKSLASH}")
    endif()
    list(APPEND DIRS "${CMAKE_CURRENT_SOURCE_DIR}/ext/openvr/bin/win64")
    target_link_libraries(gazebosc PUBLIC
        SDL2-static
        czmq-static
        sphactor-static
        rtmidi
        ${OPENVR_LIBRARIES}
        ${libzmq_LIBRARIES}
        ${Python3_LIBRARIES}
        opengl32.lib DbgHelp.lib)
    if (WITH_EMBED_PYTHON)
        # copy embedded python, we force binary_dir/python just because :S
        install(DIRECTORY ${CMAKE_BINARY_DIR}/python DESTINATION ${RESOURCE_DEST}
            USE_SOURCE_PERMISSIONS
            PATTERN exe_with_* EXCLUDE
        )
    endif(WITH_EMBED_PYTHON)
elseif (RPI)
    list(APPEND DIRS "${CMAKE_CURRENT_SOURCE_DIR}/ext/openvr/bin/linuxarm64")
    target_link_libraries(gazebosc PUBLIC
        SDL2-static
        sphactor-static
        rtmidi
        ${OPENVR_LIBRARIES}
        ${Python3_LIBRARIES}
        -lasound -ldl -L/opt/vc/lib -lbrcmGLESv2)
    target_compile_options(gazebosc PUBLIC -D__LINUX_ALSA__ )
else()
    list(APPEND DIRS "${CMAKE_CURRENT_SOURCE_DIR}/ext/openvr/bin/linux64")
    target_link_libraries(gazebosc PUBLIC
        SDL2-static
        sphactor-static
        rtmidi
        ${OPENVR_LIBRARIES}
        ${Python3_LIBRARIES}
        -lasound -ldl -lGL -lGLEW)
    target_compile_options(gazebosc PUBLIC -D__LINUX_ALSA__ )
endif()

# This is just for running the binary from an ide
if (WITH_DEV)
    add_custom_command(
        TARGET gazebosc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/misc
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/misc)
endif(WITH_DEV)

install(TARGETS gazebosc
        BUNDLE DESTINATION . COMPONENT Runtime
        RUNTIME DESTINATION ${RUNTIME_DEST} COMPONENT Runtime
        )
install(DIRECTORY ${CMAKE_SOURCE_DIR}/misc
        DESTINATION ${RESOURCE_DEST})

### POST BUILD/INSTALL
# Path used for searching by FIND_XXX(), with appropriate suffixes added
if(CMAKE_PREFIX_PATH)
    foreach(dir ${CMAKE_PREFIX_PATH})
        list(APPEND DIRS "${dir}" "${dir}/bin" "${dir}/lib")
    endforeach()
endif()

install(CODE "
        include(InstallRequiredSystemLibraries)
        include(BundleUtilities)
        fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/${APPS}\" \"${LIBS}\" \"${DIRS}\" IGNORE_ITEM \"exe_with_z64;exe_with_zip;Python;python3;python3.8\")"
       )

string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} CMAKE_HOST_SYSTEM_NAME)
set(CPACK_GENERATOR "ZIP")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif(APPLE)
set(CPACK_VERBATIM_VARIABLES true)
set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
set(CPACK_PACKAGE_NAME "Gazebosc")
set(CPACK_PACKAGE_VENDOR "HKU")
set(CPACK_PACKAGE_FILE_NAME  "gazebosc_${CMAKE_HOST_SYSTEM_NAME}_${GIT_REPO_VERSION}")
#set(CPACK_PACKAGE_ICON "${ICON_TARGET}")
include(CPack)
