/* Kilo -- A very simple editor in less than 1-kilo lines of code (as counted
 *         by "cloc"). Originally emitted VT100 escapes directly to a real
 *         terminal via termios.
 *
 * -----------------------------------------------------------------------
 * OS1/NEXS PORT
 * -----------------------------------------------------------------------
 * This is a port of antirez's "kilo" to the OS1/NEXS userspace API
 * (<os1.h>, <graphics.h>, <posix_types.h>, <syscall_nums.h>).
 *
 * Porting strategy (deliberately minimal — see notes at EOF):
 *
 *   1. INPUT:  kilo's editorReadKey() already works as a blocking
 *      byte-stream read() on fd 0. OS1's read(0, buf, count) gives us
 *      exactly that (stdin = keyboard stream for our own window), so the
 *      core key-reading / escape-sequence-decoding logic is UNCHANGED.
 *
 *   2. OUTPUT: kilo's editorRefreshScreen() builds one big VT100/ANSI
 *      escape buffer and writes it with a single write(). OS1's
 *      write(1, buf, count) routes to "own window" (per os1.h), whose
 *      backing terminal emulator in the compositor understands cursor
 *      positioning + xterm-style SGR color codes. So the *rendering*
 *      logic is also UNCHANGED — only the syscalls underneath differ.
 *
 *   3. SCREEN SIZE: there is no ioctl(TIOCGWINSZ) / SIGWINCH in OS1.
 *      We create our own window with create_window() at a fixed pixel
 *      size and derive screenrows/screencols from the terminal cell
 *      size (CELL_W x CELL_H below). No live resize support.
 *
 *   4. FILES: OS1's open()/ftruncate() don't support O_CREAT/O_TRUNC
 *      (VFS limitation, see posix_types.h). editorOpen() now reads the
 *      whole file with fopen/fseek/fread and splits it into lines itself
 *      (no getline()). editorSave() now writes the whole buffer in one
 *      shot via file_write(), which is OS1's create-or-overwrite helper.
 *
 *   5. MISC: no termios, no signal(), no errno/strerror/perror, no
 *      ctype.h, no time(). Tiny local shims are provided below.
 *
 * Everything else — the row/buffer model, syntax highlighting, search,
 * cursor movement, keypress dispatch — is the original kilo logic,
 * untouched.
 *
 * Copyright (C) 2016 Salvatore Sanfilippo <antirez at gmail dot com>
 * (original kilo.c, BSD-2-Clause, see end of upstream file for license)
 * OS1/NEXS adapter layer additions: public domain / same terms as kilo.
 */

#define KILO_VERSION "0.0.1-os1"

#include <input.h>
#include <os1.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================ OS1 adapter ================================ */

/* fd numbers for the "own window" stdio model (os1.h: 0=stdin, 1/2=own
 * window). Kept as named constants so the body of kilo below reads the
 * same as upstream. */
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

/* --- ctype.h shim (only the three predicates kilo actually uses) --- */
static int os1_isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}
static int os1_isdigit(int c) { return c >= '0' && c <= '9'; }
static int os1_isprint(int c) { return c >= 32 && c < 127; }
#define isspace os1_isspace
#define isdigit os1_isdigit
#define isprint os1_isprint

/* --- terminal cell geometry ---------------------------------------------
 * The compositor's text/terminal renderer is bitmap-font based. These two
 * values are the pixel size of one character cell; adjust them to match
 * the actual font used by the window terminal emulator on your build.
 * 8x16 is the common default for VGA-style bitmap fonts. */
#define CELL_W 8
#define CELL_H 16

/* Window geometry: 80x25 "terminal" -> pixel size. */
#define WIN_COLS 80
#define WIN_ROWS 25
#define WIN_X 40
#define WIN_Y 40
#define WIN_W (WIN_COLS * CELL_W)
#define WIN_H (WIN_ROWS * CELL_H)

/* ===========================================================================
 * Original kilo.c below (lightly adapted, see header comment).
 * ===========================================================================
 */

/* Syntax highlight types */
#define HL_NORMAL 0
#define HL_NONPRINT 1
#define HL_COMMENT 2   /* Single line comment. */
#define HL_MLCOMMENT 3 /* Multi-line comment. */
#define HL_KEYWORD1 4
#define HL_KEYWORD2 5
#define HL_STRING 6
#define HL_NUMBER 7
#define HL_MATCH 8 /* Search match. */

#define HL_HIGHLIGHT_STRINGS (1 << 0)
#define HL_HIGHLIGHT_NUMBERS (1 << 1)

struct editorSyntax {
  const char **filematch;
  const char **keywords;
  char singleline_comment_start[3];
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

typedef struct hlcolor {
  int r, g, b;
} hlcolor;

struct editorConfig {
  int cx, cy;     /* Cursor x and y position in characters */
  int rowoff;     /* Offset of row displayed. */
  int coloff;     /* Offset of column displayed. */
  int screenrows; /* Number of rows that we can show */
  int screencols; /* Number of cols that we can show */
  int numrows;    /* Number of rows */
  int rawmode;    /* unused on OS1, kept for struct-layout compatibility */
  erow *row;      /* Rows */
  int dirty;      /* File modified but not saved. */
  char *filename; /* Currently open filename */
  char statusmsg[80];
  long statusmsg_time;
  struct editorSyntax *syntax; /* Current syntax highlight, or NULL. */
};

static struct editorConfig E;

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

static void editorSetStatusMessage(const char *fmt, ...);

/* =========================== Syntax highlights DB =========================
 *
 * In order to add a new syntax, define two arrays with a list of file name
 * matches and keywords. The file name matches are used in order to match
 * a given syntax with a given file name: if a match pattern starts with a
 * dot, it is matched as the last past of the filename, for example ".c".
 * Otherwise the pattern is just searched inside the filenme, like "Makefile").
 *
 * The list of keywords to highlight is just a list of words, however if they
 * a trailing '|' character is added at the end, they are highlighted in
 * a different color, so that you can have two different sets of keywords.
 *
 * Finally add a stanza in the HLDB global variable with two two arrays
 * of strings, and a set of flags in order to enable highlighting of
 * comments and numbers.
 *
 * The characters for single and multi line comments must be exactly two
 * and must be provided as well (see the C language example).
 *
 * There is no support to highlight patterns currently. */

/* C / C++ */
static const char *C_HL_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cc",
                                        NULL};
static const char *C_HL_keywords[] = {
    /* C Keywords */
    "auto", "break", "case", "continue", "default", "do", "else", "enum",
    "extern", "for", "goto", "if", "register", "return", "sizeof", "static",
    "struct", "switch", "typedef", "union", "volatile", "while", "NULL",

    /* C++ Keywords */
    "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor", "class",
    "compl", "constexpr", "const_cast", "deltype", "delete", "dynamic_cast",
    "explicit", "export", "false", "friend", "inline", "mutable", "namespace",
    "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
    "private", "protected", "public", "reinterpret_cast", "static_assert",
    "static_cast", "template", "this", "thread_local", "throw", "true", "try",
    "typeid", "typename", "virtual", "xor", "xor_eq",

    /* C types */
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "short|", "auto|", "const|", "bool|", NULL};

/* Here we define an array of syntax highlights by extensions, keywords,
 * comments delimiters and flags. */
static struct editorSyntax HLDB[] = {{/* C / C++ */
                               C_HL_extensions, C_HL_keywords, "//", "/*", "*/",
                               HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS}};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* ======================= Low level input handling ========================= */
/* (Upstream had termios raw-mode setup here. OS1 windows already deliver
 *  a raw, unbuffered keystream on fd 0 to the focused window, so there is
 *  nothing to enable/disable -- this section is intentionally empty.) */

/* Read one logical key, blocking until something arrives.
 *
 * OS1 PORT: instead of a raw byte stream + ESC-sequence decoding, we use the
 * unified input abstraction (input_poll_event, <input.h>).  Special keys carry
 * an evdev scancode with an ASCII byte of 0, so arrows/Enter/Backspace/Tab/Esc
 * are matched on scancode; printable characters and Ctrl-folded control codes
 * (Ctrl-S/Q/F, delivered by the keyboard driver as 0x01..0x1A) arrive as the
 * ASCII .key byte.  Releases are ignored; we yield() while idle. */
static int editorReadKey(int fd) {
  (void)fd;
  input_event_t ev;

  while (1) {
    while (input_poll_event(&ev)) {
      if (ev.type != INPUT_TYPE_KEYBOARD)
        continue;
      if (ev.keyboard.state == KEY_RELEASED)
        continue; /* press or repeat only */

      switch (ev.keyboard.scancode) {
      case INPUT_KEY_UP:
        return ARROW_UP;
      case INPUT_KEY_DOWN:
        return ARROW_DOWN;
      case INPUT_KEY_LEFT:
        return ARROW_LEFT;
      case INPUT_KEY_RIGHT:
        return ARROW_RIGHT;
      case INPUT_KEY_BACKSPACE:
        return BACKSPACE;
      case INPUT_KEY_ENTER:
        return ENTER;
      case INPUT_KEY_TAB:
        return TAB;
      case INPUT_KEY_ESC:
        return ESC;
      default:
        break;
      }

      unsigned char c = ev.keyboard.key;
      if (c == '\r' || c == '\n')
        return ENTER; /* normalise LF/CR to kilo's ENTER (13) */
      if (c == 127)
        return BACKSPACE;
      if (c != 0)
        return c; /* printable, or Ctrl-x control code */
    }
    yield();
  }
}

/* ====================== Syntax highlight color scheme  ==================== */

static int is_separator(int c) {
  return c == '\0' || isspace(c) || strchr(",.()+-/*=~%[];", c) != NULL;
}

/* Return true if the specified row last char is part of a multi line comment
 * that starts at this row or at one before, and does not end at the end
 * of the row but spawns to the next row. */
static int editorRowHasOpenComment(erow *row) {
  if (row->hl && row->rsize && row->hl[row->rsize - 1] == HL_MLCOMMENT &&
      (row->rsize < 2 || (row->render[row->rsize - 2] != '*' ||
                          row->render[row->rsize - 1] != '/')))
    return 1;
  return 0;
}

/* Set every byte of row->hl (that corresponds to every character in the line)
 * to the right syntax highlight type (HL_* defines). */
static void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  if (E.syntax == NULL)
    return; /* No syntax, everything is HL_NORMAL. */

  int i, prev_sep, in_string, in_comment;
  char *p;
  const char **keywords = E.syntax->keywords;
  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  /* Point to the first non-space char. */
  p = row->render;
  i = 0; /* Current char offset */
  while (*p && isspace(*p)) {
    p++;
    i++;
  }
  prev_sep = 1;   /* Tell the parser if 'i' points to start of word. */
  in_string = 0;  /* Are we inside "" or '' ? */
  in_comment = 0; /* Are we inside multi-line comment? */

  /* If the previous line has an open comment, this line starts
   * with an open comment state. */
  if (row->idx > 0 && editorRowHasOpenComment(&E.row[row->idx - 1]))
    in_comment = 1;

  while (*p) {
    /* Handle // comments. */
    if (prev_sep && *p == scs[0] && *(p + 1) == scs[1]) {
      /* From here to end is a comment */
      memset(row->hl + i, HL_COMMENT, row->size - i);
      return;
    }

    /* Handle multi line comments. */
    if (in_comment) {
      row->hl[i] = HL_MLCOMMENT;
      if (*p == mce[0] && *(p + 1) == mce[1]) {
        row->hl[i + 1] = HL_MLCOMMENT;
        p += 2;
        i += 2;
        in_comment = 0;
        prev_sep = 1;
        continue;
      } else {
        prev_sep = 0;
        p++;
        i++;
        continue;
      }
    } else if (*p == mcs[0] && *(p + 1) == mcs[1]) {
      row->hl[i] = HL_MLCOMMENT;
      row->hl[i + 1] = HL_MLCOMMENT;
      p += 2;
      i += 2;
      in_comment = 1;
      prev_sep = 0;
      continue;
    }

    /* Handle "" and '' */
    if (in_string) {
      row->hl[i] = HL_STRING;
      if (*p == '\\') {
        row->hl[i + 1] = HL_STRING;
        p += 2;
        i += 2;
        prev_sep = 0;
        continue;
      }
      if (*p == in_string)
        in_string = 0;
      p++;
      i++;
      continue;
    } else {
      if (*p == '"' || *p == '\'') {
        in_string = *p;
        row->hl[i] = HL_STRING;
        p++;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    /* Handle non printable chars. */
    if (!isprint(*p)) {
      row->hl[i] = HL_NONPRINT;
      p++;
      i++;
      prev_sep = 0;
      continue;
    }

    /* Handle numbers */
    if ((isdigit(*p) && (prev_sep || row->hl[i - 1] == HL_NUMBER)) ||
        (*p == '.' && i > 0 && row->hl[i - 1] == HL_NUMBER)) {
      row->hl[i] = HL_NUMBER;
      p++;
      i++;
      prev_sep = 0;
      continue;
    }

    /* Handle keywords and lib calls */
    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2)
          klen--;

        if (!memcmp(p, keywords[j], klen) && is_separator(*(p + klen))) {
          /* Keyword */
          memset(row->hl + i, kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          p += klen;
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue; /* We had a keyword match */
      }
    }

    /* Not special chars */
    prev_sep = is_separator(*p);
    p++;
    i++;
  }

  /* Propagate syntax change to the next row if the open commen
   * state changed. This may recursively affect all the following rows
   * in the file. */
  int oc = editorRowHasOpenComment(row);
  if (row->hl_oc != oc && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
  row->hl_oc = oc;
}

/* Maps syntax highlight token types to terminal colors. */
static int editorSyntaxToColor(int hl) {
  switch (hl) {
  case HL_COMMENT:
  case HL_MLCOMMENT:
    return 36; /* cyan */
  case HL_KEYWORD1:
    return 33; /* yellow */
  case HL_KEYWORD2:
    return 32; /* green */
  case HL_STRING:
    return 35; /* magenta */
  case HL_NUMBER:
    return 31; /* red */
  case HL_MATCH:
    return 34; /* blu */
  default:
    return 37; /* white */
  }
}

/* Select the syntax highlight scheme depending on the filename,
 * setting it in the global state E.syntax. */
static void editorSelectSyntaxHighlight(char *filename) {
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = HLDB + j;
    unsigned int i = 0;
    while (s->filematch[i]) {
      char *p;
      int patlen = strlen(s->filematch[i]);
      if ((p = strstr(filename, s->filematch[i])) != NULL) {
        if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
          E.syntax = s;
          return;
        }
      }
      i++;
    }
  }
}

/* ======================= Editor rows implementation ======================= */

/* Update the rendered version and the syntax highlight of a row. */
static void editorUpdateRow(erow *row) {
  unsigned int tabs = 0, nonprint = 0;
  int j, idx;

  /* Create a version of the row we can directly print on the screen,
   * respecting tabs, substituting non printable characters with '?'. */
  free(row->render);
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == TAB)
      tabs++;

  unsigned long long allocsize =
      (unsigned long long)row->size + tabs * 8 + nonprint * 9 + 1;
  if (allocsize > UINT32_MAX) {
    printf("Some line of the edited file is too long for kilo\n");
    exit(1);
  }

  row->render = malloc(row->size + tabs * 8 + nonprint * 9 + 1);
  idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == TAB) {
      row->render[idx++] = ' ';
      while ((idx + 1) % 8 != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->rsize = idx;
  row->render[idx] = '\0';

  /* Update the syntax highlighting attributes of the row. */
  editorUpdateSyntax(row);
}

/* Insert a row at the specified position, shifting the other rows on the bottom
 * if required. */
static void editorInsertRow(int at, const char *s, size_t len) {
  if (at > E.numrows)
    return;
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  if (at != E.numrows) {
    memmove(E.row + at + 1, E.row + at, sizeof(E.row[0]) * (E.numrows - at));
    for (int j = at + 1; j <= E.numrows; j++)
      E.row[j].idx++;
  }
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.row[at].hl = NULL;
  E.row[at].hl_oc = 0;
  E.row[at].render = NULL;
  E.row[at].rsize = 0;
  E.row[at].idx = at;
  editorUpdateRow(E.row + at);
  E.numrows++;
  E.dirty++;
}

/* Free row's heap allocated stuff. */
static void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

/* Remove the row at the specified position, shifting the remainign on the
 * top. */
static void editorDelRow(int at) {
  erow *row;

  if (at >= E.numrows)
    return;
  row = E.row + at;
  editorFreeRow(row);
  memmove(E.row + at, E.row + at + 1, sizeof(E.row[0]) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++)
    E.row[j].idx++;
  E.numrows--;
  E.dirty++;
}

/* Turn the editor rows into a single heap-allocated string.
 * Returns the pointer to the heap-allocated string and populate the
 * integer pointed by 'buflen' with the size of the string, escluding
 * the final nulterm. */
static char *editorRowsToString(int *buflen) {
  char *buf = NULL, *p;
  int totlen = 0;
  int j;

  /* Compute count of bytes */
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1; /* +1 is for "\n" at end of every row */
  *buflen = totlen;
  totlen++; /* Also make space for nulterm */

  p = buf = malloc(totlen);
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  *p = '\0';
  return buf;
}

/* Insert a character at the specified position in a row, moving the remaining
 * chars on the right if needed. */
static void editorRowInsertChar(erow *row, int at, int c) {
  if (at > row->size) {
    /* Pad the string with spaces if the insert location is outside the
     * current length by more than a single character. */
    int padlen = at - row->size;
    /* In the next line +2 means: new char and null term. */
    row->chars = realloc(row->chars, row->size + padlen + 2);
    memset(row->chars + row->size, ' ', padlen);
    row->chars[row->size + padlen + 1] = '\0';
    row->size += padlen + 1;
  } else {
    /* If we are in the middle of the string just make space for 1 new
     * char plus the (already existing) null term. */
    row->chars = realloc(row->chars, row->size + 2);
    memmove(row->chars + at + 1, row->chars + at, row->size - at + 1);
    row->size++;
  }
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

/* Append the string 's' at the end of a row */
static void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(row->chars + row->size, s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

/* Delete the character at offset 'at' from the specified row. */
static void editorRowDelChar(erow *row, int at) {
  if (row->size <= at)
    return;
  memmove(row->chars + at, row->chars + at + 1, row->size - at);
  editorUpdateRow(row);
  row->size--;
  E.dirty++;
}

/* Insert the specified char at the current prompt position. */
static void editorInsertChar(int c) {
  int filerow = E.rowoff + E.cy;
  int filecol = E.coloff + E.cx;
  erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

  /* If the row where the cursor is currently located does not exist in our
   * logical representaion of the file, add enough empty rows as needed. */
  if (!row) {
    while (E.numrows <= filerow)
      editorInsertRow(E.numrows, "", 0);
  }
  row = &E.row[filerow];
  editorRowInsertChar(row, filecol, c);
  if (E.cx == E.screencols - 1)
    E.coloff++;
  else
    E.cx++;
  E.dirty++;
}

/* Inserting a newline is slightly complex as we have to handle inserting a
 * newline in the middle of a line, splitting the line as needed. */
static void editorInsertNewline(void) {
  int filerow = E.rowoff + E.cy;
  int filecol = E.coloff + E.cx;
  erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

  if (!row) {
    if (filerow == E.numrows) {
      editorInsertRow(filerow, "", 0);
      goto fixcursor;
    }
    return;
  }
  /* If the cursor is over the current line size, we want to conceptually
   * think it's just over the last character. */
  if (filecol >= row->size)
    filecol = row->size;
  if (filecol == 0) {
    editorInsertRow(filerow, "", 0);
  } else {
    /* We are in the middle of a line. Split it between two rows. */
    editorInsertRow(filerow + 1, row->chars + filecol, row->size - filecol);
    row = &E.row[filerow];
    row->chars[filecol] = '\0';
    row->size = filecol;
    editorUpdateRow(row);
  }
fixcursor:
  if (E.cy == E.screenrows - 1) {
    E.rowoff++;
  } else {
    E.cy++;
  }
  E.cx = 0;
  E.coloff = 0;
}

/* Delete the char at the current prompt position. */
static void editorDelChar(void) {
  int filerow = E.rowoff + E.cy;
  int filecol = E.coloff + E.cx;
  erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

  if (!row || (filecol == 0 && filerow == 0))
    return;
  if (filecol == 0) {
    /* Handle the case of column 0, we need to move the current line
     * on the right of the previous one. */
    filecol = E.row[filerow - 1].size;
    editorRowAppendString(&E.row[filerow - 1], row->chars, row->size);
    editorDelRow(filerow);
    row = NULL;
    if (E.cy == 0)
      E.rowoff--;
    else
      E.cy--;
    E.cx = filecol;
    if (E.cx >= E.screencols) {
      int shift = (E.screencols - E.cx) + 1;
      E.cx -= shift;
      E.coloff += shift;
    }
  } else {
    editorRowDelChar(row, filecol - 1);
    if (E.cx == 0 && E.coloff)
      E.coloff--;
    else
      E.cx--;
  }
  if (row)
    editorUpdateRow(row);
  E.dirty++;
}

/* ============================ File I/O (OS1) ============================= */

/* Load the specified program in the editor memory and returns 0 on success
 * or 1 on error.
 *
 * OS1 NOTE: upstream used getline() in a loop. OS1's libc has no getline(),
 * so we read the whole file in one shot (fopen/fseek/ftell/fread, all
 * available per <stdio.h>) and split it into rows ourselves. */
static int editorOpen(char *filename) {
  FILE *fp;
  char *data = NULL;
  long fsize = 0;

  E.dirty = 0;
  free(E.filename);
  size_t fnlen = strlen(filename) + 1;
  E.filename = malloc(fnlen);
  memcpy(E.filename, filename, fnlen);

  fp = fopen(filename, "r");
  if (!fp) {
    /* New file: nothing to load, start with an empty buffer. */
    return 1;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return 1;
  }
  fsize = ftell(fp);
  if (fsize < 0) {
    fclose(fp);
    return 1;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return 1;
  }

  if (fsize > 0) {
    data = malloc((size_t)fsize + 1);
    size_t got = fread(data, 1, (size_t)fsize, fp);
    data[got] = '\0';
    fsize = (long)got;
  }
  fclose(fp);

  if (data) {
    char *line = data;
    long i;
    for (i = 0; i < fsize; i++) {
      if (data[i] == '\n') {
        long linelen = i - (line - data);
        if (linelen > 0 && line[linelen - 1] == '\r')
          linelen--;
        editorInsertRow(E.numrows, line, (size_t)linelen);
        line = data + i + 1;
      }
    }
    /* Trailing partial line with no terminating newline. */
    if (line < data + fsize) {
      long linelen = (data + fsize) - line;
      if (linelen > 0 && line[linelen - 1] == '\r')
        linelen--;
      if (linelen > 0)
        editorInsertRow(E.numrows, line, (size_t)linelen);
    }
    free(data);
  }

  E.dirty = 0;
  return 0;
}

/* Save the current file on disk. Return 0 on success, 1 on error.
 *
 * OS1 NOTE: upstream used open(O_CREAT)+ftruncate()+write(). OS1's VFS
 * doesn't support O_CREAT/O_TRUNC (see posix_types.h), so we instead use
 * file_write(path, buf, size, offset), which creates-or-overwrites the
 * whole file in a single call (#251 in syscall_nums.h). */
static int editorSave(void) {
  int len;
  char *buf = editorRowsToString(&len);

  int written = file_write(E.filename, buf, len, 0);
  free(buf);

  if (written < 0 || written != len) {
    editorSetStatusMessage("Can't save! I/O error (%d)", written);
    return 1;
  }

  E.dirty = 0;
  editorSetStatusMessage("%d bytes written on disk", len);
  return 0;
}

/* ============================= Terminal update ============================ */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

static void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(new + ab->len, s, len);
  ab->b = new;
  ab->len += len;
}

static void abFree(struct abuf *ab) { free(ab->b); }

/* This function writes the whole screen using VT100 escape characters
 * starting from the logical state of the editor in the global state 'E'.
 *
 * OS1 NOTE: write(1, ...) is "own window" per os1.h; the window's
 * terminal-emulator backend interprets the same cursor/SGR escapes a real
 * VT100 terminal would. compositor_render() is called at the end to make
 * sure the frame actually hits the screen. */
static void editorRefreshScreen(void) {
  int y;
  erow *r;
  char buf[32];
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6); /* Hide cursor. */
  abAppend(&ab, "\x1b[H", 3);    /* Go home. */
  for (y = 0; y < E.screenrows; y++) {
    int filerow = E.rowoff + y;

    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen =
            snprintf(welcome, sizeof(welcome),
                     "Kilo editor -- verison %s\x1b[0K\r\n", KILO_VERSION);
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(&ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(&ab, " ", 1);
        abAppend(&ab, welcome, welcomelen);
      } else {
        abAppend(&ab, "~\x1b[0K\r\n", 7);
      }
      continue;
    }

    r = &E.row[filerow];

    int len = r->rsize - E.coloff;
    int current_color = -1;
    if (len > 0) {
      if (len > E.screencols)
        len = E.screencols;
      char *c = r->render + E.coloff;
      unsigned char *hl = r->hl + E.coloff;
      int j;
      for (j = 0; j < len; j++) {
        if (hl[j] == HL_NONPRINT) {
          char sym;
          abAppend(&ab, "\x1b[7m", 4);
          if (c[j] <= 26)
            sym = '@' + c[j];
          else
            sym = '?';
          abAppend(&ab, &sym, 1);
          abAppend(&ab, "\x1b[0m", 4);
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(&ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(&ab, c + j, 1);
        } else {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color) {
            char cbuf[16];
            int clen = snprintf(cbuf, sizeof(cbuf), "\x1b[%dm", color);
            current_color = color;
            abAppend(&ab, cbuf, clen);
          }
          abAppend(&ab, c + j, 1);
        }
      }
    }
    abAppend(&ab, "\x1b[39m", 5);
    abAppend(&ab, "\x1b[0K", 4);
    abAppend(&ab, "\r\n", 2);
  }

  /* Create a two rows status. First row: */
  abAppend(&ab, "\x1b[0K", 4);
  abAppend(&ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename,
                     E.numrows, E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.rowoff + E.cy + 1,
                      E.numrows);
  if (len > E.screencols)
    len = E.screencols;
  abAppend(&ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(&ab, rstatus, rlen);
      break;
    } else {
      abAppend(&ab, " ", 1);
      len++;
    }
  }
  abAppend(&ab, "\x1b[0m\r\n", 6);

  /* Second row depends on E.statusmsg and the status message update time. */
  abAppend(&ab, "\x1b[0K", 4);
  int msglen = strlen(E.statusmsg);
  if (msglen && get_time() - E.statusmsg_time < 5000)
    abAppend(&ab, E.statusmsg, msglen <= E.screencols ? msglen : E.screencols);

  /* Put cursor at its current position. Note that the horizontal position
   * at which the cursor is displayed may be different compared to 'E.cx'
   * because of TABs. */
  int j;
  int cx = 1;
  int filerow = E.rowoff + E.cy;
  erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
  if (row) {
    for (j = E.coloff; j < (E.cx + E.coloff); j++) {
      if (j < row->size && row->chars[j] == TAB)
        cx += 7 - ((cx) % 8);
      cx++;
    }
  }
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, cx);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6); /* Show cursor. */
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);

  /* Make sure the compositor actually presents the frame we just wrote. */
  compositor_render();
}

/* Set an editor status message for the second line of the status, at the
 * end of the screen. */
static void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = get_time();
}

/* =============================== Find mode ================================ */

#define KILO_QUERY_LEN 256

static void editorFind(int fd) {
  char query[KILO_QUERY_LEN + 1] = {0};
  int qlen = 0;
  int last_match = -1;    /* Last line where a match was found. -1 for none. */
  int find_next = 0;      /* if 1 search next, if -1 search prev. */
  int saved_hl_line = -1; /* No saved HL */
  char *saved_hl = NULL;

#define FIND_RESTORE_HL                                                        \
  do {                                                                         \
    if (saved_hl) {                                                            \
      memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);   \
      free(saved_hl);                                                          \
      saved_hl = NULL;                                                         \
    }                                                                          \
  } while (0)

  /* Save the cursor position in order to restore it later. */
  int saved_cx = E.cx, saved_cy = E.cy;
  int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

  while (1) {
    editorSetStatusMessage("Search: %s (Use ESC/Arrows/Enter)", query);
    editorRefreshScreen();

    int c = editorReadKey(fd);
    if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
      if (qlen != 0)
        query[--qlen] = '\0';
      last_match = -1;
    } else if (c == ESC || c == ENTER) {
      if (c == ESC) {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
      }
      FIND_RESTORE_HL;
      editorSetStatusMessage("");
      return;
    } else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
      find_next = 1;
    } else if (c == ARROW_LEFT || c == ARROW_UP) {
      find_next = -1;
    } else if (isprint(c)) {
      if (qlen < KILO_QUERY_LEN) {
        query[qlen++] = c;
        query[qlen] = '\0';
        last_match = -1;
      }
    }

    /* Search occurrence. */
    if (last_match == -1)
      find_next = 1;
    if (find_next) {
      char *match = NULL;
      int match_offset = 0;
      int i, current = last_match;

      for (i = 0; i < E.numrows; i++) {
        current += find_next;
        if (current == -1)
          current = E.numrows - 1;
        else if (current == E.numrows)
          current = 0;
        match = strstr(E.row[current].render, query);
        if (match) {
          match_offset = match - E.row[current].render;
          break;
        }
      }
      find_next = 0;

      /* Highlight */
      FIND_RESTORE_HL;

      if (match) {
        erow *row = &E.row[current];
        last_match = current;
        if (row->hl) {
          saved_hl_line = current;
          saved_hl = malloc(row->rsize);
          memcpy(saved_hl, row->hl, row->rsize);
          memset(row->hl + match_offset, HL_MATCH, qlen);
        }
        E.cy = 0;
        E.cx = match_offset;
        E.rowoff = current;
        E.coloff = 0;
        /* Scroll horizontally as needed. */
        if (E.cx > E.screencols) {
          int diff = E.cx - E.screencols;
          E.cx -= diff;
          E.coloff += diff;
        }
      }
    }
  }
}

/* ========================= Editor events handling  ======================== */

/* Handle cursor position change because arrow keys were pressed. */
static void editorMoveCursor(int key) {
  int filerow = E.rowoff + E.cy;
  int filecol = E.coloff + E.cx;
  int rowlen;
  erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

  switch (key) {
  case ARROW_LEFT:
    if (E.cx == 0) {
      if (E.coloff) {
        E.coloff--;
      } else {
        if (filerow > 0) {
          E.cy--;
          E.cx = E.row[filerow - 1].size;
          if (E.cx > E.screencols - 1) {
            E.coloff = E.cx - E.screencols + 1;
            E.cx = E.screencols - 1;
          }
        }
      }
    } else {
      E.cx -= 1;
    }
    break;
  case ARROW_RIGHT:
    if (row && filecol < row->size) {
      if (E.cx == E.screencols - 1) {
        E.coloff++;
      } else {
        E.cx += 1;
      }
    } else if (row && filecol == row->size) {
      E.cx = 0;
      E.coloff = 0;
      if (E.cy == E.screenrows - 1) {
        E.rowoff++;
      } else {
        E.cy += 1;
      }
    }
    break;
  case ARROW_UP:
    if (E.cy == 0) {
      if (E.rowoff)
        E.rowoff--;
    } else {
      E.cy -= 1;
    }
    break;
  case ARROW_DOWN:
    if (filerow < E.numrows) {
      if (E.cy == E.screenrows - 1) {
        E.rowoff++;
      } else {
        E.cy += 1;
      }
    }
    break;
  }
  /* Fix cx if the current line has not enough chars. */
  filerow = E.rowoff + E.cy;
  filecol = E.coloff + E.cx;
  row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
  rowlen = row ? row->size : 0;
  if (filecol > rowlen) {
    E.cx -= filecol - rowlen;
    if (E.cx < 0) {
      E.coloff += E.cx;
      E.cx = 0;
    }
  }
}

/* Process events arriving from the input stream, which is, the user
 * is typing on the keyboard. */
#define KILO_QUIT_TIMES 3
static void editorProcessKeypress(int fd) {
  /* When the file is modified, requires Ctrl-q to be pressed N times
   * before actually quitting. */
  static int quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey(fd);
  switch (c) {
  case ENTER: /* Enter */
    editorInsertNewline();
    break;
  case CTRL_C: /* Ctrl-c */
    /* We ignore ctrl-c, it can't be so simple to lose the changes
     * to the edited file. */
    break;
  case CTRL_Q: /* Ctrl-q */
    /* Quit if the file was already saved. */
    if (E.dirty && quit_times) {
      editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                             "Press Ctrl-Q %d more times to quit.",
                             quit_times);
      quit_times--;
      return;
    }
    exit(0);
    break;
  case CTRL_S: /* Ctrl-s */
    editorSave();
    break;
  case CTRL_F:
    editorFind(fd);
    break;
  case BACKSPACE: /* Backspace */
  case CTRL_H:    /* Ctrl-h */
  case DEL_KEY:
    editorDelChar();
    break;
  case PAGE_UP:
  case PAGE_DOWN:
    if (c == PAGE_UP && E.cy != 0)
      E.cy = 0;
    else if (c == PAGE_DOWN && E.cy != E.screenrows - 1)
      E.cy = E.screenrows - 1;
    {
      int times = E.screenrows;
      while (times--)
        editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;
  case CTRL_L: /* ctrl+l, clear screen */
    /* Just refresht the line as side effect. */
    break;
  case ESC:
    /* Nothing to do for ESC in this mode. */
    break;
  default:
    editorInsertChar(c);
    break;
  }

  quit_times = KILO_QUIT_TIMES; /* Reset it to the original value. */
}

/* ============================== Init / main =============================== */

/* OS1 NOTE: no ioctl(TIOCGWINSZ), no SIGWINCH.  The compositor terminal cell
 * size depends on the active (proportional) font, so the real grid is NOT
 * 80x25 — we query it from the window with window_grid() and reserve two rows
 * for the status bar + message line.  win_id < 0 (or a failed query) falls
 * back to the compile-time WIN_COLS/WIN_ROWS estimate. */
static void updateWindowSize(int win_id) {
  int cols = WIN_COLS, rows = WIN_ROWS;
  if (win_id >= 0)
    window_grid(win_id, &cols, &rows);
  if (cols < 1)
    cols = WIN_COLS;
  if (rows < 3)
    rows = WIN_ROWS;
  E.screencols = cols;
  E.screenrows = rows - 2;
}

static void initEditor(void) {
  E.cx = 0;
  E.cy = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.syntax = NULL;
  updateWindowSize(-1); /* provisional; refined once the window exists */
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: kilo <filename>\n");
    exit(1);
  }

  /* Create our own window and grab keyboard focus. fd 0/1 ("own window")
   * then refer to this window's input stream / terminal renderer. */
  int win = create_window(WIN_X, WIN_Y, WIN_W, WIN_H, "kilo");
  if (win < 0) {
    printf("kilo: create_window failed (%d)\n", win);
    exit(1);
  }
  set_focus(get_pid());

  initEditor();
  updateWindowSize(win); /* size to the real terminal grid of our window */
  editorSelectSyntaxHighlight(argv[1]);
  editorOpen(argv[1]);
  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress(STDIN_FILENO);
  }
  return 0;
}

/* ===========================================================================
 * PORTING NOTES / TODO
 * ===========================================================================
 *
 * - CELL_W/CELL_H (8x16): tune these to match the actual bitmap font used
 *   by the compositor's window terminal emulator, otherwise the status
 *   bar / wrap column will be visually off even though the logic is correct.
 *
 * - editorReadKey() assumes the window input stream forwards arrow keys,
 *   Home/End, PageUp/Down and Del as standard "ESC [ ..." sequences (as a
 *   real VT100/xterm would). If OS1's keyboard IPC delivers raw scancodes
 *   or a different encoding instead, only editorReadKey()'s ESC-sequence
 *   switch needs to change -- everything above (the row/buffer model) and
 *   below (movement/keypress dispatch) is keycode-agnostic via the
 *   KEY_ACTION enum.
 *
 * - compositor_render() is called once per refresh (i.e. roughly once per
 *   keystroke). If this is too slow/flickery on real hardware, it can be
 *   removed if window_write()/write(1,...) already triggers a compositor
 *   update on its own.
 *
 * - No SIGWINCH / live resize: the window is created at a fixed WIN_W x
 *   WIN_H. If OS1 windows can be resized by the user, editorRefreshScreen()
 *   will keep using the original WIN_COLS x WIN_ROWS grid regardless.
 *
 * - file_write() is assumed to create the file if it does not exist and to
 *   truncate it to exactly `len` bytes (matching the old open+ftruncate+
 *   write sequence). If the underlying VFS instead *appends* or does not
 *   truncate on overwrite, editorSave() will need a delete-then-write or an
 *   explicit truncate step once such a syscall exists.
 */