// UI2 loading screen (loading.cpp): shown while a map loads, a map generates or a match starts. The work runs on the
// main thread, so the screen is presented from inside it: every LoadStep (crumb.h) and the navigation bake's progress
// draw a frame on a second ImGui context that shares the font atlas. When the job ends it fades out over the first
// frames of the game.
#pragma once
#include <string>
struct App;
void loadingBegin(App& app, const std::string& title, const std::string& sub, int expectedStages, unsigned thumbTex = 0, const std::string& mapId = "");   // nested calls join the outer job; mapId: content/templates/<id>.loading.jpg as the backdrop (UI4)
void loadingStage(const char* what);      // a LoadStep began: a new stage label (internal names map to player-facing ones)
void loadingStageDone();                  // a LoadStep ended: the bar moves on
void loadingProgress(const char* label, float f);   // a long stage with its own fraction (the AI navigation bake)
void loadingEnd();
void loadingOverlay(App& app);            // every frame after NewFrame: the fade-out once a job ended
bool loadingActive();
void loadingBootMode();                   // UI5: the open job is the launch: a plain dark backdrop (no menu scene yet); its end logs the startup times
struct LoadingJob { LoadingJob(App& a, const std::string& t, const std::string& s, int n, unsigned th = 0, const std::string& id = "") { loadingBegin(a, t, s, n, th, id); } ~LoadingJob() { loadingEnd(); } };
