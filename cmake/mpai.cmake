# Multiplayer + AI milestone sources (branch multiplayer-ai). Kept out of the main source list so merges with the
# other workstreams touch CMakeLists.txt by one line only.
target_sources(NormandyMapmaker PRIVATE
  src/scenario.cpp src/match.cpp src/selftest_match.cpp src/mpai.cpp src/royale.cpp src/selftest_royale_live.cpp
  src/character.cpp src/pose.cpp src/nav.cpp src/grenade.cpp src/game.cpp src/ai.cpp src/mpui.cpp src/mpui_modes.cpp src/selftest_modes.cpp src/net.cpp src/selftest_ai.cpp src/selftest_net.cpp
  src/navmeta.cpp)
if(WIN32)
  target_link_libraries(NormandyMapmaker PRIVATE ws2_32)
endif()
target_sources(NormandyMapmaker PRIVATE src/zombie.cpp src/gunbody.cpp src/capture_gm1.cpp src/capture_df1.cpp src/anim_clip.cpp src/selftest_zombies.cpp src/selftest_perf.cpp)   # G2 zombies
target_sources(NormandyMapmaker PRIVATE src/loot.cpp src/inventory_ui.cpp src/selftest_royale.cpp)   # BR2 Battle Royale loot
target_sources(NormandyMapmaker PRIVATE src/sd_mode.cpp src/sd_game.cpp src/selftest_sd.cpp src/selftest_playerdmg.cpp src/selftest_prone.cpp src/selftest_sd3.cpp src/selftest_spectate.cpp src/selftest_sd_timings.cpp src/selftest_sdplant.cpp src/selftest_th1.cpp src/check_th2.cpp src/selftest_sdfreeze.cpp src/selftest_sdtime.cpp src/selftest_sd9.cpp)   # SD1 Search and Destroy
