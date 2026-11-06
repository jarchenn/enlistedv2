// No-op gore system: the contract in gore_api.h before workstream W7 replaces this file with src/gore.cpp.
// Counts the hits it is given so tests can check the damage path reports them.
#include "gore_api.h"

int gGoreHitsReported = 0;
void goreFleshHit(App&, const FleshHit&) { ++gGoreHitsReported; }
void goreStep(App&, float) {}
void goreReset(App&) {}
