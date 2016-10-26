#include "trie.h"

#include <stdlib.h>
#include <string.h>

struct trie *newTrie() {
  struct trie *t = (struct trie*)malloc(sizeof(struct trie));
  t->value = NULL;
  t->node = 0;
  memset(t->next, 0, sizeof(struct trie*) * CHAR_MAX);
  return t;
}

void trieAddKeyValue(struct trie *t, char *key, void *value) {
  struct trie *n = newTrie();
  t->next[(int)*key] = n;
  n->node = *key;
  // set the value if this is the last byte of the key
  if (*(key+1) == '\0') {
    n->value = value;
  } else {
    trieAddKeyValue(n, key+1, value);
  }
}

void *trieLookup(struct trie *t, char *key) {
  struct trie *next;
  if (*key == '\0') {
    return t->value;
  } else if ((next = t->next[(int)*key])) {
    return trieLookup(next, key+1);
  } else {
    return NULL;
  }
}
