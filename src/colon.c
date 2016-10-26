#include "colon.h"
#include <stdio.h>

void printHello() {
  printf("Hello\n");
}

void (*lookupColonFunction(char *name))() {
  return &printHello; // TODO implement key value store get
}

void handleColonFunction(char *name) {
  void (*func)() = lookupColonFunction(name);
  func();
}

void registerColonFunction(char *name, void (*func)()) {
  // TODO implement key value store set
}
