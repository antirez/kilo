#include "colon.h"

#include <stdio.h>
#include "trie.h"
#include "kilo.h"

static struct trie colonFunctions;

void (*lookupColonFunction(char *name))() {
  return (void (*)())trieLookup(&colonFunctions, name);
}

int handleColonFunction(char *name) {
  void (*func)() = lookupColonFunction(name);
  if (func) {
    func();
    return 0;
  } else {
    return 1;
  }
}

void registerColonFunction(char *name, void (*func)()) {
  trieAddKeyValue(&colonFunctions, name, (void*)func);
}
