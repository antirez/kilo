#ifndef KILO_KILO_H
#define KILO_KILO_H

#include <stddef.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>

/* Declare some base manuiplation functions */

struct editorSyntax {
  char **filematch;
  char **keywords;
  char singleline_comment_start[2];
  char multiline_comment_start[3];
  char multiline_comment_end[3];
  int flags;
};

/* This structure represents a single line of the file we are editing. */
typedef struct erow {
  int idx;           /* Row index in the file, zero-based. */
  int size;          /* Size of the row, excluding the null term. */
  int rsize;         /* Size of the rendered row. */
  char *chars;       /* Row content. */
  char *render;      /* Row content "rendered" for screen (for TABs). */
  unsigned char *hl; /* Syntax highlight type for each character in render.*/
  int hl_oc;         /* Row had open comment at end in last syntax highlight
                        check. */
} erow;

typedef struct hlcolor { int r, g, b; } hlcolor;

/* FIXME: This is a layering violation! */
enum vimMode {
  VM_NORMAL,
  VM_VISUAL_CHAR,
  VM_VISUAL_LINE,
  VM_INSERT,
};
typedef enum vimMode vimMode;

typedef struct bufferConfig {
  int cx, cy;     /* Cursor x and y position in characters */
  int rowoff;     /* Offset of row displayed. */
  int coloff;     /* Offset of column displayed. */
  int screenrows; /* Number of rows that we can show */
  int screencols; /* Number of cols that we can show */
  int numrows;    /* Number of rows */
  int rawmode;    /* Is terminal raw mode enabled? */
  erow *row;      /* Rows */
  int dirty;      /* File modified but not saved. */
  char *filename; /* Currently open filename */
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax; /* Current syntax highlight, or NULL. */
  int selection_row;
  int selection_offset;
  vimMode mode;
} bufferConfig;

typedef struct ptrVector {
  void **data;
  int idx, cap;
} ptrVector;

static inline void ptrVectorInit(ptrVector *vec) {
  vec->data = malloc((vec->cap = 32) * sizeof(void *));
  vec->idx = 0;
}

static inline void ptrVectorPushBack(ptrVector *vec, void *elem) {
  if (vec->idx == vec->cap)
    vec->data = realloc(vec->data, vec->cap *= 2);
  vec->data[vec->idx++] = elem;
}

static inline void *ptrVectorPopBack(ptrVector *vec) {
  if (vec->idx == 0)
    return NULL;
  return vec->data[vec->idx--];
}

/* Linear search: somethimes you gotta do it! */
static inline int ptrVectorFind(ptrVector *vec, void *elem) {
  int idx;
  for (idx = 0; idx < vec->idx; ++idx)
    if (vec->data[idx] == elem)
      return idx;
  return -1;
}

static inline void ptrVectorDelete(ptrVector *vec, void *elem) {
  int idx = ptrVectorFind(vec, elem);
  if (idx != -1)
    memmove(vec->data + idx, vec->data + idx + 1,
            (vec->idx - idx) * sizeof(void *));
}

extern struct ptrVector openbuffers;
extern struct bufferConfig *buffer;

enum DIRECTION {
  LEFT = 0,
  RIGHT = 1,
  UP = 2,
  DOWN = 3
};

enum KEY_ACTION {
  KEY_NULL = 0,    /* NULL */
  CTRL_C = 3,      /* Ctrl-c */
  CTRL_D = 4,      /* Ctrl-d */
  CTRL_F = 6,      /* Ctrl-f */
  CTRL_H = 8,      /* Ctrl-h */
  TAB = 9,         /* Tab */
  CTRL_L = 12,     /* Ctrl+l */
  ENTER = 13,      /* Enter */
  CTRL_Q = 17,     /* Ctrl-q */
  CTRL_S = 19,     /* Ctrl-s */
  CTRL_U = 21,     /* Ctrl-u */
  ESC = 27,        /* Escape */
  BACKSPACE = 127, /* Backspace */
  /* The following are just soft codes, not really reported by the
   * terminal directly. */
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/* Syntax highlight types */
#define HL_NORMAL 0
#define HL_NONPRINT 1
#define HL_COMMENT 2   /* Single line comment. */
#define HL_MLCOMMENT 3 /* Multi-line comment. */
#define HL_KEYWORD1 4
#define HL_KEYWORD2 5
#define HL_STRING 6
#define HL_NUMBER 7
#define HL_MATCH 8      /* Search match. */

#define HL_HIGHLIGHT_STRINGS (1<<0)
#define HL_HIGHLIGHT_NUMBERS (1<<1)

#define LOG_FILENAME "kilo.log"

#define bool _Bool
#define true 1
#define false 0

typedef struct {
  int firstX, firstY;
  int secondX, secondY;
} textObject;

typedef enum {
  TOK_LEFT,
  TOK_RIGHT,
  TOK_INNER
} textObjectKind;

#define EMPTY_TEXT_OBJECT                       \
  (textObject) { -1, -1, -1, -1 }

static inline bool badTextObject(textObject obj) {
  return obj.firstY < 0 || obj.firstY >= buffer->numrows || obj.secondY < 0 ||
         obj.secondY >= buffer->numrows || obj.firstX > buffer->row[obj.firstY].size ||
         obj.secondX > buffer->row[obj.secondY].size;
}

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);
bool editorSetCursorPos(int x, int y);

int editorReadKey(int fd);
int getCursorPosition(int ifd, int ofd, int *rows, int *cols);
void editorMoveCursor(enum DIRECTION dir);
int getWindowSize(int ifd, int ofd, int *rows, int *cols);

/* ======================= Editor rows implementation ======================= */

/* Cursor's position in the text buffer. */
static inline int cursorX() { return buffer->coloff + buffer->cx; }
static inline int cursorY() { return buffer->rowoff + buffer->cy; }

/* Region's position in the text buffer. */
static inline int regionX() { return buffer->selection_offset; }
static inline int regionY() { return buffer->selection_row; }

/* Is c in between a and b? */
static inline bool clamp(int a, int b, int c) {
  return (c > b && c < a) || (c > a && c < b);
}
static inline bool inclusive_clamp(int a, int b, int c) {
  return clamp(a, b, c) || c == a || c == b;
}

/* Stream over text, abstracts rows. */
typedef struct { int x, y; } charIterator;
static inline void incrementChar(charIterator *it) {
  if (buffer->row[it->y].size == it->x) {
    ++it->y;
    it->x = 0;
  } else
    ++it->x;
}

static inline void decrementChar(charIterator *it) {
  if (it->x == 0) {
    it->y--;
    it->x = buffer->row[it->y].size;
  } else
    --it->x;
}

static inline char loadChar(charIterator *it) {
  if (!inclusive_clamp(0, buffer->numrows - 1, it->y) ||
      !inclusive_clamp(0, buffer->row[it->y].size, it->x))
    return '\0';
  if (buffer->row[it->y].size == it->x)
    return '\n';
  return buffer->row[it->y].chars[it->x];
}

void editorUpdateRow(erow *row);
void editorInsertRow(int at, char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int at);
char *editorRowsToString(int *buflen);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowAppendString(erow *row, char *s, size_t len);
void editorRowDelChar(erow *row, int at);
void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar();
char *editorReadStringFromStatusBar(char *prefix);
void editorMoveCursorToRowEnd(void);
bool editorMoveCursorToFirst(char c);
bool editorIsPointInRegion(int x, int y);

textObject editorSelectionAsTextObject(void);
textObject editorWordAtPoint(int x, int y, textObjectKind kind);
textObject editorRegionObject(void);
textObject editorPairAtPoint(int x, int y, char lhs, char rhs, bool isInner);
textObject editorComplementTextObject(int x, int y);
bool editorDeleteTextObject(textObject obj);

bool editorOpen(char *filename);

bufferConfig *editorFindBuffer(char *name);
bool editorSwitchBuffer(char *);

int editorSave();

void logmsg(char *fmt, ...);

void editorQuit(int force);

#define SWAP(a, b)                                                             \
  do {                                                                         \
    (a) = (a) ^ (b);                                                           \
    (b) = (a) ^ (b);                                                           \
    (a) = (a) ^ (b);                                                           \
  } while (0)

#endif
