#include "colon.h"
#include <stdio.h>
#include "trie.h"
#include "kilo.h"

static struct trie colonFunctions;

char *lookupPartialColonFunction(char *partialName) {
  struct trie *t = triePartialLookup(&colonFunctions, partialName);
  if (t == NULL) {
    return NULL;
  }

  int i, found;

  int bufsz = 8;
  int len = 0;
  char *buf = malloc(bufsz);

  for (;;) {
    if (t->value != NULL) {
      goto done;
    }
    found = -1;
    for (i = 0; i < CHAR_MAX; i++) {
      if (t->next[i] != NULL) {
        if (found != -1) {
          // There are multiple possible branches in the trie so return the
          // current location.
          goto done;
        }
        found = i;
      }
    }
    t = t->next[found];
    if (len + 1 >= bufsz) { // leave room for a null byte
      buf = realloc(buf, bufsz *= 2);
    }
    buf[len++] = found;
  }

done:
  buf[len] = '\0';
  return buf;
}

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
