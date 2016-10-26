#include "init.h"
#include "function.h"
#include "colon.h"

void initUser(void) {
  registerColonFunction("q", &quit);
}
