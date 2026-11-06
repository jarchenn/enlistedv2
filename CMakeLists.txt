cmake_minimum_required(VERSION 3.24)
project(NormandyMapmaker VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---- Pinned dependencies -------------------------------------------------
FetchContent_Declare(glfw  GIT_REPOSITORY https://github.com/glfw/glfw.git        GIT_TAG 7b6aead9fb88b3623e3b3725ebb42670cbe4c579 GIT_SHALLOW FALSE) # 3.4
FetchContent_Declare(glm   GIT_REPOSITORY https://github.com/g-truc/glm.git       GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863) # 1.0.1
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git    GIT_TAG f401021d5a5d56fe2304056c391e78f81c8d4b8f) # v1.91.5
FetchContent_Declare(jolt  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git GIT_TAG e77f175595e64cb44218cc9d9d56fc365ad0e36a SOURCE_SUBDIR Build) # v5.6.0
FetchContent_Declare(json  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(stb   GIT_REPOSITORY https://github.com/nothings/stb.git     GIT_TAG d14d46f6b44456eb88beac58cba833a4135d4c89)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 4a5b74bef029b3592c54b6048650ee5f972c1a48) # 0.11.21

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm jolt json)
FetchContent_Populate(imgui)  # no CMake project; sources compiled below
FetchContent_GetProperties(imgui)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
  FetchContent_Populate(miniaudio)
endif()

add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# ---- Game ---------------------------------------------------------------
add_executable(NormandyMapmaker
  src/main.cpp src/app.cpp src/assets.cpp src/render.cpp src/terrain.cpp
  src/physics.cpp src/mapdoc.cpp src/editor.cpp src/explore.cpp src/audio.cpp
  src/selftest.cpp src/thirdparty_impl.cpp src/damage.cpp src/ground.cpp src/selftest_damage.cpp src/fx.cpp src/station.cpp src/weapons.cpp src/selftest_pak.cpp src/selftest_weapons.cpp src/capture.cpp src/grass.cpp src/selftest_grass.cpp src/selftest_record.cpp src/recorder.cpp src/model.cpp src/viewmodel.cpp src/selftest_fps.cpp src/selftest_env.cpp src/ballistics.cpp src/foliage.cpp src/builder.cpp src/selftest_village.cpp src/arsenal.cpp src/selftest_arsenal.cpp src/selftest_move.cpp)
target_include_directories(NormandyMapmaker PRIVATE src ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
target_link_libraries(NormandyMapmaker PRIVATE glad glfw glm::glm imgui Jolt nlohmann_json::nlohmann_json)
target_compile_definitions(NormandyMapmaker PRIVATE GLM_ENABLE_EXPERIMENTAL GLFW_INCLUDE_NONE)
if(MINGW)
  target_link_options(NormandyMapmaker PRIVATE -static -mwindows -s)
  target_link_libraries(NormandyMapmaker PRIVATE winmm ole32)
elseif(MSVC)
  set_target_properties(NormandyMapmaker PROPERTIES WIN32_EXECUTABLE ON)
  target_link_options(NormandyMapmaker PRIVATE /ENTRY:mainCRTStartup)
endif()

# Copy content next to the executable so the build dir is runnable.
add_custom_target(copy_content ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/content $<TARGET_FILE_DIR:NormandyMapmaker>/content
  COMMENT "Copying content/")
add_dependencies(copy_content NormandyMapmaker)

install(TARGETS NormandyMapmaker RUNTIME DESTINATION .)
install(DIRECTORY content DESTINATION .)
install(FILES README.md DESTINATION .)
