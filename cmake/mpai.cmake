# Multiplayer + AI milestone sources (branch multiplayer-ai). Kept out of the main source list so merges with the
# other workstreams touch CMakeLists.txt by one line only.
target_sources(NormandyMapmaker PRIVATE
  src/scenario.cpp src/match.cpp src/selftest_match.cpp src/mpai.cpp
  src/character.cpp src/pose.cpp src/nav.cpp src/grenade.cpp src/game.cpp src/ai.cpp src/mpui.cpp src/net.cpp src/selftest_ai.cpp src/selftest_net.cpp
  src/navmeta.cpp src/gore_stub.cpp)
if(WIN32)
  target_link_libraries(NormandyMapmaker PRIVATE ws2_32)
endif()
