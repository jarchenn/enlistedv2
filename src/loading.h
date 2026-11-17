// UI2 loading screen (loading.cpp): shown while a map loads, a map generates or a match starts. The work runs on the
// main thread, so the screen is presented from inside it: every LoadStep (crumb.h) and the navigation bake's progress
// draw a frame on a second ImGui context that shares the font atlas. When the job ends it fades out over the first
// frames of the game.
#pragma once
#include <string>
struct App;
void loadingBegin(App& app, const std::string& title, const std::string& sub, int expectedStages, unsigned thumbTex = 0);   // nested calls join the outer job
void loadingStage(const char* what);      // a LoadStep began: a new stage label (internal names map to player-facing ones)
void loadingStageDone();                  // a LoadStep ended: the bar moves on
void loadingProgress(const char* label, float f);   // a long stage with its own fraction (the AI navigation bake)
void loadingEnd();
void loadingOverlay(App& app);            // every frame after NewFrame: the fade-out once a job ended
bool loadingActive();
struct LoadingJob { LoadingJob(App& a, const std::string& t, const std::string& s, int n, unsigned th = 0) { loadingBegin(a, t, s, n, th); } ~LoadingJob() { loadingEnd(); } };
