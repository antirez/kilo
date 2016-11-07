#ifndef KILO_COLON_H
#define KILO_COLON_H

typedef void colonFunction();
typedef _Bool unaryColonFunction(char *);

int handleColonFunction(char *name, char *arg);
void registerColonFunction(char *name, colonFunction *func);
void registerColonFunctionWithArg(char *name, unaryColonFunction *func);
char *lookupPartialColonFunction(char *partialName);

#endif
