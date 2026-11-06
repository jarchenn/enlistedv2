#pragma once
#include "model.h"
#include <array>
#include <vector>

// Reduce one indexed triangle mesh to about targetTris triangles (quadric error metric; src/simplify.cpp).
void simplifyMesh(std::vector<TexVertex>& verts, std::vector<uint32_t>& idx, size_t targetTris);
