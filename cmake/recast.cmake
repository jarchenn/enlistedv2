# W6 AI core (AI_SPEC 3-11): Recast/Detour navigation mesh (recastnavigation, zlib licence, docs/credits.md) and the
# bot AI sources. Included from CMakeLists.txt with one line, after mpai.cmake (which keeps nav.cpp / ai.cpp).
# Local builds point FETCHCONTENT_SOURCE_DIR_RECAST at %TEMP%/nmk/fc/recast-src (a checkout of the same tag).
FetchContent_Declare(recast GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
                            GIT_TAG 6dc1667f580357e8a2154c28b7867bea7e8ad3a7)   # v1.6.0
FetchContent_GetProperties(recast)
if(NOT recast_POPULATED)
  FetchContent_Populate(recast)   # no add_subdirectory: the demo / tests / install rules are not needed
endif()
file(GLOB RECAST_SOURCES ${recast_SOURCE_DIR}/Recast/Source/*.cpp ${recast_SOURCE_DIR}/Detour/Source/*.cpp
                         ${recast_SOURCE_DIR}/DetourCrowd/Source/*.cpp)
add_library(recastnav STATIC ${RECAST_SOURCES})
target_include_directories(recastnav PUBLIC ${recast_SOURCE_DIR}/Recast/Include ${recast_SOURCE_DIR}/Detour/Include
                                            ${recast_SOURCE_DIR}/DetourCrowd/Include)
# 64-bit polygon references: 4000 m maps in 32 m tiles need 14 tile bits, which leaves too few polygon bits in 32-bit
# refs. Virtual query filters: every bot plans with its own costs (threat, crowding, lane, blocked edges).
target_compile_definitions(recastnav PUBLIC DT_POLYREF64 DT_VIRTUAL_QUERYFILTER)
set_target_properties(recastnav PROPERTIES CXX_STANDARD 17)

target_sources(NormandyMapmaker PRIVATE
  src/nav_geom.cpp src/nav_links.cpp src/ai_core.cpp src/ai_path.cpp src/ai_cover.cpp src/ai_layers.cpp
  src/ai_combat.cpp src/ai_debug.cpp src/selftest_ai_course.cpp)
target_link_libraries(NormandyMapmaker PRIVATE recastnav)
