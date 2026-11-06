#pragma once
// W6 AI test content (selftest_ai_course.cpp): obstacle course and random soak map builders, test arguments.
#include "common.h"
class App;
extern int gSoakMinutes; extern std::string gSoakMaps;
bool buildAiObstacleCourse(App& app, std::string& err);
bool buildRandomSoakMap(App& app, uint32_t seed, std::string& err);
