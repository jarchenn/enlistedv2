#pragma once
// Breadcrumbs for crash reports from the multiplayer + AI code: the last stage reached is printed by an unhandled-
// exception filter (installed by mpaiArg), so a silent crash in a test process names the subsystem it happened in.
extern const char* volatile gCrumb;
extern bool gCrumbProf;               // NMK_FRAME_PROF: crumbs double as frame-profile marks (app.cpp)
void crumbMark(const char* s);
void crumbFrameEnd();                 // logs the frame's breakdown when it took over 150 ms
inline void crumb(const char* s) { gCrumb = s; if (gCrumbProf) crumbMark(s); }
void installCrashReporter();
// map-load steps (PF3): with NMK_FRAME_PROF set, a scope logs "[loadprof] <step> <ms>" when it ends (app.cpp)
struct LoadStep { const char* what; double t0; explicit LoadStep(const char* w); ~LoadStep(); };
