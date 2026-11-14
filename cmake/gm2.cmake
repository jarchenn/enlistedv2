# GM2: 2D minimap, explosion pack, artillery, suppression vignette (docs/minimap.md, docs/abilities.md)
target_sources(NormandyMapmaker PRIVATE src/minimap.cpp src/abilities.cpp)
target_sources(NormandyMapmaker PRIVATE src/vm_anim.cpp)   # MG1 animated viewmodels
