# L3 light assets (docs/lights.md): the "light" block, interior lights with the room templates, fire particles,
# lamp posts along routes and the editor's Light section. lightsGather() feeds the lighting engine (L2, lighting.cpp).
target_sources(NormandyMapmaker PRIVATE src/lights.cpp)
