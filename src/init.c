#include "init.h"
#include "function.h"
#include "colon.h"
#include "kilo.h"

void initUser(void) {
  registerColonFunction("q", &quitWithPrompt);
  registerColonFunction("q!", &quitForce);
  registerColonFunction("w", &saveIgnoreError);
  registerColonFunction("wq", &saveAndQuit);
}
