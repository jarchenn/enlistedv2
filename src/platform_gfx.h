#pragma once
// The graphics-context seam: window hints, context / API loading, the ImGui renderer backend, swap interval and present.
// Windows and Linux: OpenGL 4.5 through glad. macOS: the normandy-mac fork supplies mac/mac_platform.h (Metal), so the
// shared game files stay byte-identical between the two repositories (docs/mac.md in the fork).
#include <GLFW/glfw3.h>
#include <imgui.h>
#ifdef __APPLE__
#include "mac/mac_platform.h"
#else
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
inline void platformWindowHints() {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
}
inline bool platformInitContext(GLFWwindow* w) { glfwMakeContextCurrent(w); return gladLoadGL((GLADloadfunc)glfwGetProcAddress) != 0; }
inline void platformSwapInterval(int n) { glfwSwapInterval(n); }
inline void platformImGuiInit(GLFWwindow* w) { ImGui_ImplGlfw_InitForOpenGL(w, true); ImGui_ImplOpenGL3_Init("#version 450 core"); }
inline void platformImGuiNewFrame() { ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); }
inline void platformImGuiRender(ImDrawData* d) { ImGui_ImplOpenGL3_RenderDrawData(d); }
inline void platformImGuiShutdown() { ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); }
inline void platformPresent(GLFWwindow* w) { glfwSwapBuffers(w); }
#endif
