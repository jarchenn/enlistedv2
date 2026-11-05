#include "app.h"

int main(int argc, char** argv) {
  App app;
  gApp = &app;
  return app.run(argc, argv);
}
