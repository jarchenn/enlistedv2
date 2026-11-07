# Internet play: room codes, hole punching and relay through the rendezvous server (src/inet.h,
# tools/rendezvous/PROTOCOL.md). Kept separate so merges touch CMakeLists.txt by one line only.
target_sources(NormandyMapmaker PRIVATE src/inet.cpp)
