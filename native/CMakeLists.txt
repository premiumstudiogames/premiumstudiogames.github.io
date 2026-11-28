cmake_minimum_required(VERSION 3.24)
project(PremiumEngine VERSION 0.16.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)   # dependencies follow CMAKE_MSVC_RUNTIME_LIBRARY too
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # one .exe, no VC++ redistributable needed
  add_compile_options(/utf-8 /MP)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---- SDL2 (window, input, OpenGL context) — zlib license
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_FORCE_STATIC_VCRT ON CACHE BOOL "" FORCE)
FetchContent_Declare(sdl2 GIT_REPOSITORY https://github.com/libsdl-org/SDL GIT_TAG release-2.30.9 GIT_SHALLOW TRUE)
# ---- Dear ImGui (docking branch) — MIT
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui GIT_TAG v1.91.5-docking GIT_SHALLOW TRUE)
# ---- Jolt Physics — MIT
set(USE_AVX2 OFF CACHE BOOL "" FORCE)   # runs on any x64 PC
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
FetchContent_Declare(jolt GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics GIT_TAG v5.2.0 GIT_SHALLOW TRUE SOURCE_SUBDIR Build)
# ---- QuickJS-ng (JavaScript for game scripts) — MIT
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(quickjs GIT_REPOSITORY https://github.com/quickjs-ng/quickjs GIT_TAG v0.8.0 GIT_SHALLOW TRUE)
# ---- nlohmann/json — MIT, glm — MIT
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
# ---- Boost.Regex, header-only standalone (for the script editor's syntax colours) — Boost Software License
FetchContent_Declare(boostregex GIT_REPOSITORY https://github.com/boostorg/regex GIT_TAG boost-1.86.0 GIT_SHALLOW TRUE)
# ---- stb_image (PNG texture atlases for the block world) — public domain / MIT
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb GIT_TAG 9600522de53973336fa033a289858a4354cbb04c)
# ---- cgltf (glTF 2.0 models: .glb / .gltf) — MIT
FetchContent_Declare(cgltf GIT_REPOSITORY https://github.com/jkuhlmann/cgltf GIT_TAG v1.14 GIT_SHALLOW TRUE)
# ---- miniaudio (game sound: wav / ogg / mp3 / flac, 3D) — public domain / MIT-0
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio GIT_TAG 0.11.21 GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(sdl2 jolt quickjs json glm)
foreach(dep imgui boostregex stb miniaudio cgltf)   # sources only, no CMake project
  FetchContent_GetProperties(${dep})
  if(NOT ${dep}_POPULATED)
    FetchContent_Populate(${dep})
  endif()
endforeach()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  third_party/imguizmo/ImGuizmo.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp third_party/imguizmo)
target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

# ---- the engine (shared by the editor and the game player)
add_library(pgcore STATIC
  src/gl.cpp src/project.cpp src/mesh.cpp src/renderer.cpp src/physics.cpp src/script.cpp src/game.cpp src/log.cpp src/ui_style.cpp src/app.cpp src/voxel.cpp src/arabic.cpp src/ui.cpp src/audio.cpp src/miniaudio_impl.c src/models.cpp src/net.cpp src/blueprint.cpp src/assets.cpp src/navigation.cpp src/gameplay.cpp src/animation.cpp src/netsync.cpp src/foliage.cpp src/fracture.cpp src/cloth.cpp src/zipwrite.cpp src/landscape.cpp)
target_include_directories(pgcore PUBLIC src third_party/gl ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR} ${cgltf_SOURCE_DIR})
target_link_libraries(pgcore PUBLIC imgui Jolt qjs nlohmann_json::nlohmann_json glm::glm)
if(UNIX)
  target_link_libraries(pgcore PUBLIC ${CMAKE_DL_LIBS} pthread m)
endif()
if(WIN32)
  target_link_libraries(pgcore PUBLIC opengl32 shell32 ole32 ws2_32)
  target_compile_definitions(pgcore PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
# shown as 00.02.00 (two-digit groups, like the Premium Games apps)
foreach(part MAJOR MINOR PATCH)
  string(LENGTH "${PROJECT_VERSION_${part}}" _len)
  if(_len LESS 2)
    set(V_${part} "0${PROJECT_VERSION_${part}}")
  else()
    set(V_${part} "${PROJECT_VERSION_${part}}")
  endif()
endforeach()
set(PGE_DISPLAY_VERSION "${V_MAJOR}.${V_MINOR}.${V_PATCH}")
target_compile_definitions(pgcore PUBLIC PGE_VERSION="${PGE_DISPLAY_VERSION}")
file(WRITE ${CMAKE_BINARY_DIR}/version.txt "${PGE_DISPLAY_VERSION}")

# script editor widget (ImGuiColorTextEdit, MIT, vendored with a JavaScript language added)
add_library(textedit STATIC third_party/textedit/TextEditor.cpp third_party/textedit/LanguageDefinitions.cpp)
target_include_directories(textedit PUBLIC third_party/textedit ${boostregex_SOURCE_DIR}/include)
target_compile_definitions(textedit PUBLIC BOOST_REGEX_STANDALONE)
target_link_libraries(textedit PUBLIC imgui)

# editor
add_executable(PremiumEngine WIN32 src/editor/main.cpp src/editor/editor.cpp src/editor/panels.cpp src/editor/dialogs.cpp src/editor/platform.cpp src/editor/console.cpp src/editor/bp_editor.cpp src/editor/assets_ui.cpp)
target_link_libraries(PremiumEngine PRIVATE pgcore textedit SDL2::SDL2main)
# game player (packaged games are this program + the project file)
add_executable(PremiumEnginePlayer WIN32 src/player/main.cpp)
target_link_libraries(PremiumEnginePlayer PRIVATE pgcore SDL2::SDL2main)
if(WIN32)
  target_sources(PremiumEngine PRIVATE src/res/editor.rc)
  target_link_libraries(PremiumEngine PRIVATE comdlg32 dbghelp)
  if(MSVC)   # symbols in a .pdb next to the program (not shipped): the self-tests can say where the editor was if it stops
    target_compile_options(pgcore PRIVATE /Zi)
    target_compile_options(PremiumEngine PRIVATE /Zi)
    target_link_options(PremiumEngine PRIVATE /DEBUG /OPT:REF /OPT:ICF)
  endif()
  target_sources(PremiumEnginePlayer PRIVATE src/res/player.rc)
endif()

foreach(t PremiumEngine PremiumEnginePlayer)
  add_custom_command(TARGET ${t} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${t}>/assets)
endforeach()

# engine tests (no window needed)
add_executable(PremiumEngineTests tests/tests.cpp)
target_link_libraries(PremiumEngineTests PRIVATE pgcore SDL2::SDL2main)   # tests.cpp includes SDL.h, which renames main
target_compile_definitions(PremiumEngineTests PRIVATE PGE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
