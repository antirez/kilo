#ifndef KILO_SYNTAX_HIGHLIGHTER_H
#define KILO_SYNTAX_HIGHLIGHTER_H

#include "kilo.h"

void editorUpdateSyntax(erow *raw);
int editorSyntaxToColor(int hl);

extern struct editorSyntax HLDB[1];

#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0]))

#endif
