#pragma once
// Skeletal animation clips from glTF (G2: the zombie GLB carries its own clips; the soldiers stay procedural, pose.cpp).
// Channels are T / R / S per node (LINEAR or STEP; CUBICSPLINE uses the value keys). Sampling writes node locals, which
// become the PoseResult the soldier pipeline already uses: skin matrices for the compute skinning, model-space deltas
// (gore) and the semantic joint heads (hit capsules, damage zones).
#include "common.h"

struct CharModel;
struct PoseResult;

struct AnimClip {
  std::string name; float dur = 0;
  struct Channel { int node = -1, path = 0; bool step = false; std::vector<float> t, v; };   // path 0 T, 1 R (xyzw), 2 S
  std::vector<Channel> ch;
  float swing = -1;          // time of the fastest hand: where an attack's blow lands
  float groundSpeed = 0;     // m/s the planted feet slide back (locomotion: playback rate = speed / groundSpeed)
};

struct NodeLocals { std::vector<vec3> T, S; std::vector<quat> R; std::vector<char> set; };   // set: animated (else nodeLocal)

void clipsFromGltf(const json& J, const std::function<std::vector<float>(int)>& acc, std::vector<AnimClip>& out);
void analyzeClips(CharModel& m);                         // swing / groundSpeed (after the joints are mapped)
int findClip(const CharModel& m, const std::string& name);   // -1 = none
void restLocals(const CharModel& m, NodeLocals& L);
void sampleClip(const CharModel& m, int clip, float t, bool loop, NodeLocals& L);   // overwrites the nodes it animates
void mixLocals(NodeLocals& a, const NodeLocals& b, float w);   // a -> b by w (lerp / slerp)
void poseFromLocals(const CharModel& m, const NodeLocals& L, PoseResult& out);

// One character's clip state: a single clip, or two clips blended at a shared phase (walk -> run by speed), with a
// cross-fade from the last shown pose whenever the key changes.
struct AnimPlayer {
  int key = -1;              // what is playing (caller-defined)
  int a = -1, b = -1;        // clip; b >= 0: blended toward b by w, time is the shared phase 0..1
  float w = 0, rate = 1, time = 0;
  bool loop = true;
  float fade = 1, fadeLen = 0.2f;
  NodeLocals last, from;
  float lastHp = 100; int strike = 0, life = -1;   // zombie.cpp: hit and attack triggers, the life they belong to
  // same key: only the blend / rate change; a new key (or restart) starts at 0 and cross-fades
  void play(int key, int clipA, int clipB, float w, float rate, bool loop, bool restart = false);
  void update(const CharModel& m, float dt, PoseResult& out);
};
