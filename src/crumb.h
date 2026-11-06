#pragma once
// Breadcrumbs for crash reports from the multiplayer + AI code: the last stage reached is printed by an unhandled-
// exception filter (installed by mpaiArg), so a silent crash in a test process names the subsystem it happened in.
extern const char* volatile gCrumb;
inline void crumb(const char* s) { gCrumb = s; }
void installCrashReporter();
