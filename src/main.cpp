#include "app.h"

#ifdef _WIN32
// Ask hybrid-graphics laptops (NVIDIA Optimus, AMD PowerXpress) to run the game on the dedicated GPU instead of the
// integrated one. The drivers look these exported symbols up in the exe; they have no effect anywhere else.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char** argv) {
  App app;
  gApp = &app;
  return app.run(argc, argv);
}
