/* Kilo -- A very simple editor in less than 1-kilo lines of code (as counted
 *         by "cloc"). Does not depend on libcurses, directly emits VT100
 *         escapes on the terminal.
 *
 * -----------------------------------------------------------------------
 *
 * Copyright (C) 2016 Salvatore Sanfilippo <antirez at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define KILO_VERSION "0.0.1"

#define _BSD_SOURCE
#define _GNU_SOURCE

#include <assert.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>

#include "kilo.h"
#include "process_keypress.h"
#include "init.h"
#include "syntax_highlighter.h"
#include "colon.h"

struct ptrVector openBuffers;
struct bufferConfig *buffer;

FILE *logfile;

void editorSetStatusMessage(const char *fmt, ...);

/* ======================= Low level terminal handling ====================== */

static struct termios orig_termios; /* In order to restore at exit.*/

void disableRawMode(int fd) {
    /* Don't even check the return value as it's too late. */
    if (buffer->rawmode) {
        tcsetattr(fd,TCSAFLUSH,&orig_termios);
        buffer->rawmode = 0;
    }
}

/* Called at exit to avoid remaining in raw mode. */
void editorAtExit(void) {
    disableRawMode(STDIN_FILENO);
}

/* Raw mode: 1960 magic shit. */
int enableRawMode(int fd) {
    struct termios raw;

    if (buffer->rawmode) return 0; /* Already enabled. */
    if (!isatty(STDIN_FILENO)) goto fatal;
    atexit(editorAtExit);
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer. */
    raw.c_cc[VMIN] = 0; /* Return each byte, or zero for timeout. */
    raw.c_cc[VTIME] = 1; /* 100 ms timeout (unit is tens of second). */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    buffer->rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

/* Read a key from the terminal put in raw mode, trying to handle
 * escape sequences. */
int editorReadKey(int fd) {
    int nread;
    char c, seq[3];
    while ((nread = read(fd,&c,1)) == 0);
    if (nread == -1) exit(1);

    while(1) {
        switch(c) {
        case ESC:    /* escape sequence */
            /* If this is just an ESC, we'll timeout here. */
            if (read(fd,seq,1) == 0) return ESC;
            if (read(fd,seq+1,1) == 0) return ESC;

            /* ESC [ sequences. */
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended escape, read additional byte. */
                    if (read(fd,seq+2,1) == 0) return ESC;
                    if (seq[2] == '~') {
                        switch(seq[1]) {
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        }
                    }
                } else {
                    switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                    }
                }
            }

            /* ESC O sequences. */
            else if (seq[0] == 'O') {
                switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
            break;
        default:
            return c;
        }
    }
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor is stored at *rows and *cols and 0 is returned. */
int getCursorPosition(int ifd, int ofd, int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    /* Report cursor location */
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf)-1) {
        if (read(ifd,buf+i,1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    /* Parse it. */
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf+2,"%d;%d",rows,cols) != 2) return -1;
    return 0;
}

/* Try to get the number of columns in the current terminal. If the ioctl()
 * call fails the function will try to query the terminal itself.
 * Returns 0 on success, -1 on error. */
int getWindowSize(int ifd, int ofd, int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int orig_row, orig_col, retval;

        /* Get the initial position so we can restore it later. */
        retval = getCursorPosition(ifd,ofd,&orig_row,&orig_col);
        if (retval == -1) goto failed;

        /* Go to right/bottom margin and get position. */
        if (write(ofd,"\x1b[999C\x1b[999B",12) != 12) goto failed;
        retval = getCursorPosition(ifd,ofd,rows,cols);
        if (retval == -1) goto failed;

        /* Restore position. */
        char seq[32];
        snprintf(seq,32,"\x1b[%d;%dH",orig_row,orig_col);
        if (write(ofd,seq,strlen(seq)) == -1) {
            /* Can't recover... */
        }
        return 0;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }

failed:
    return -1;
}

/* Select the syntax highlight scheme depending on the filename,
 * setting it in the global state E.syntax. */
void editorSelectSyntaxHighlight(char *filename) {
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = HLDB+j;
        unsigned int i = 0;
        while(s->filematch[i]) {
            char *p;
            int patlen = strlen(s->filematch[i]);
            if ((p = strstr(filename,s->filematch[i])) != NULL) {
                if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
                    buffer->syntax = s;
                    return;
                }
            }
            i++;
        }
    }
}

/* ======================= Text Object helpers ============================== */

textObject editorWordAtPoint(int x, int y, textObjectKind kind) {
  charIterator *forward = &(charIterator){x, y};
  charIterator *backward = &(charIterator){x, y};

  if (kind == TOK_INNER || kind == TOK_LEFT) {
    while (loadChar(backward) && loadChar(backward) == ' ')
      decrementChar(backward);
    while (loadChar(backward) && loadChar(backward) != ' ')
      decrementChar(backward);
  }

  if (kind == TOK_RIGHT) {
    while (loadChar(forward) && loadChar(forward) == ' ')
      incrementChar(forward);
    while (loadChar(forward) && loadChar(forward) != ' ')
      incrementChar(forward);
  }

  return (textObject){backward->x, backward->y, forward->x, forward->y};
}

static char complementOf(char c) {
  switch (c) {
  case '(': return ')';
  case '{': return '}';
  case '<': return '>';
  case '[': return ']';
  case ')': return '(';
  case '}': return '{';
  case '>': return '<';
  case ']': return '[';
  }
  return '\0';
}

static bool findNearestComplementableChar(charIterator *forward) {
  charIterator *backward = &(charIterator){forward->x, forward->y};

  for (;;) {
    char forcomp = complementOf(loadChar(forward));
    if (forcomp && forcomp < loadChar(forward))
      break;
    char backcomp = complementOf(loadChar(backward));
    if (backcomp && backcomp > loadChar(backward))
      break;

    /* We've reached EOF. */
    if (!loadChar(forward) || !loadChar(backward))
      return true;
    incrementChar(forward);
    decrementChar(backward);
  }

  if (complementOf(loadChar(forward)) == '\0') {
    forward->x = backward->x;
    forward->y = backward->y;
  }

  return false;
}

textObject editorComplementTextObject(int x, int y) {
  charIterator *iter = &(charIterator){x, y};
  char point = loadChar(iter);
  char complement = complementOf(point);
  bool goRight = point < complement;

  if (!complement) {
    if (findNearestComplementableChar(iter))
      return EMPTY_TEXT_OBJECT;
    point = loadChar(iter);
    complement = complementOf(point);
    goRight = point < complement;
  }

  void (*increment)(charIterator * it) =
      goRight ? incrementChar : decrementChar;

  for (;;) {
    char c = loadChar(iter);
    if (c == '\0')
      return EMPTY_TEXT_OBJECT;
    if (c == complement)
      break;
    increment(iter);
  }

  return goRight ? (textObject){x, y, iter->x, iter->y}
                 : (textObject){iter->x, iter->y, x, y};
}

/* Balanced region selector. */
textObject editorPairAtPoint(int x, int y, char lhs, char rhs, bool isInner) {
  charIterator *backwards = &(charIterator){x, y};
  charIterator *forwards = &(charIterator){x, y};

  if (loadChar(backwards) == '\0')
    return EMPTY_TEXT_OBJECT;

  int parenCount = 0;
  if (isInner) {
    for (;;) {
      char c = loadChar(backwards);
      if (c == '\0')
        return EMPTY_TEXT_OBJECT;
      if (c == rhs)
        ++parenCount;
      if (c == lhs)
       if (!parenCount--)
          break;
      decrementChar(backwards);
    }
  }

  parenCount = 0;
  for (;;) {
    char c = loadChar(forwards);
    if (c == '\0')
      return EMPTY_TEXT_OBJECT;
    if (c == lhs)
      ++parenCount;
    if (c == rhs)
      if (parenCount-- == 0)
        break;
    incrementChar(forwards);
  }

  return (textObject){backwards->x + 1, backwards->y, forwards->x - 1,
                      forwards->y};
}

textObject editorRegionObject() {
  if (cursorY() < regionY() ||
      (cursorY() == regionY() && cursorX() < regionX()))
    return (textObject){cursorX(), cursorY(), regionX(), regionY()};
  else
    return (textObject){regionX(), regionY(), cursorX(), cursorY()};
}

static bool editorDeleteRows(textObject obj) {
  if (badTextObject(obj))
    return true;

  int iter = obj.secondY - obj.firstY + 1;
  while (iter--)
    editorDelRow(obj.firstY);

  buffer->cx = obj.firstX - buffer->rowoff;
  buffer->cy = obj.firstY - buffer->coloff;

  return false;
}

static bool editorDeleteSelection(textObject obj) {
  if (badTextObject(obj))
    return true;

  char *begin, *end;

  /* Join the begin of begin_row and the end of end_row together */
  begin = strndup(buffer->row[obj.firstY].chars, obj.firstX);
  end = strdup(buffer->row[obj.secondY].chars + obj.secondX + 1);
  int size = strlen(begin) + strlen(end);

  begin = realloc(begin, size + 1);
  strcat(begin, end);

  editorDeleteRows(obj);
  editorInsertRow(obj.firstY, begin, size);

  free(end);
  free(begin);

  buffer->cx = obj.firstX - buffer->coloff;
  buffer->cy = obj.firstY - buffer->rowoff;
  return false;
}

bool editorDeleteTextObject(textObject obj) {
  if (buffer->mode == VM_VISUAL_LINE)
    return editorDeleteRows(obj);
  return editorDeleteSelection(obj);
}

/* ======================= Editor rows implementation ======================= */

/* Update the rendered version and the syntax highlight of a row. */
void editorUpdateRow(erow *row) {
    int tabs = 0, nonprint = 0, j, idx;

   /* Create a version of the row we can directly print on the screen,
     * respecting tabs, substituting non printable characters with '?'. */
    free(row->render);
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == TAB) tabs++;

    row->render = malloc(row->size + tabs*8 + nonprint*9 + 1);
    idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == TAB) {
            row->render[idx++] = ' ';
            while((idx+1) % 8 != 0) row->render[idx++] = ' ';
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
void editorInsertRow(int at, char *s, size_t len) {
    if (at > buffer->numrows) return;
    buffer->row = realloc(buffer->row,sizeof(erow)*(buffer->numrows+1));
    if (at != buffer->numrows) {
        memmove(buffer->row+at+1,buffer->row+at,sizeof(buffer->row[0])*(buffer->numrows-at));
        for (int j = at+1; j <= buffer->numrows; j++) buffer->row[j].idx++;
    }
    buffer->row[at].size = len;
    buffer->row[at].chars = malloc(len+1);
    memcpy(buffer->row[at].chars,s,len+1);
    buffer->row[at].hl = NULL;
    buffer->row[at].hl_oc = 0;
    buffer->row[at].render = NULL;
    buffer->row[at].rsize = 0;
    buffer->row[at].idx = at;
    editorUpdateRow(buffer->row+at);
    buffer->numrows++;
    buffer->dirty++;
}

/* Free row's heap allocated stuff. */
void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

/* Remove the row at the specified position, shifting the remainign on the
 * top. */
void editorDelRow(int at) {
    erow *row;

    if(at >= buffer->numrows) return;
    row = buffer->row+at;
    editorFreeRow(row);
    memmove(buffer->row+at,buffer->row+at+1,sizeof(buffer->row[0])*(buffer->numrows-at-1));
    for (int j = at; j < buffer->numrows-1; j++) buffer->row[j].idx++;
    buffer->numrows--;
    buffer->dirty++;
}

/* Turn the editor rows into a single heap-allocated string.
 * Returns the pointer to the heap-allocated string and populate the
 * integer pointed by 'buflen' with the size of the string, escluding
 * the final nulterm. */
char *editorRowsToString(int *buflen) {
    char *buf = NULL, *p;
    int totlen = 0;
    int j;

    /* Compute count of bytes */
    for (j = 0; j < buffer->numrows; j++)
        totlen += buffer->row[j].size+1; /* +1 is for "\n" at end of every row */
    *buflen = totlen;
    totlen++; /* Also make space for nulterm */

    p = buf = malloc(totlen);
    for (j = 0; j < buffer->numrows; j++) {
        memcpy(p,buffer->row[j].chars,buffer->row[j].size);
        p += buffer->row[j].size;
        *p = '\n';
        p++;
    }
    *p = '\0';
    return buf;
}

/* Insert a character at the specified position in a row, moving the remaining
 * chars on the right if needed. */
void editorRowInsertChar(erow *row, int at, int c) {
    if (at > row->size) {
        /* Pad the string with spaces if the insert location is outside the
         * current length by more than a single character. */
        int padlen = at-row->size;
        /* In the next line +2 means: new char and null term. */
        row->chars = realloc(row->chars,row->size+padlen+2);
        memset(row->chars+row->size,' ',padlen);
        row->chars[row->size+padlen+1] = '\0';
        row->size += padlen+1;
    } else {
        /* If we are in the middle of the string just make space for 1 new
         * char plus the (already existing) null term. */
        row->chars = realloc(row->chars,row->size+2);
        memmove(row->chars+at+1,row->chars+at,row->size-at+1);
        row->size++;
    }
    row->chars[at] = c;
    editorUpdateRow(row);
    buffer->dirty++;
}

/* Append the string 's' at the end of a row */
void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars,row->size+len+1);
    memcpy(row->chars+row->size,s,len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    buffer->dirty++;
}

/* Delete the character at offset 'at' from the specified row. */
void editorRowDelChar(erow *row, int at) {
    if (row->size <= at) return;
    memmove(row->chars+at,row->chars+at+1,row->size-at);
    editorUpdateRow(row);
    row->size--;
    buffer->dirty++;
}

/* Insert the specified char at the current prompt position. */
void editorInsertChar(int c) {
    int filerow = buffer->rowoff+buffer->cy;
    int filecol = buffer->coloff+buffer->cx;
    erow *row = (filerow >= buffer->numrows) ? NULL : &buffer->row[filerow];

    /* If the row where the cursor is currently located does not exist in our
     * logical representaion of the file, add enough empty rows as needed. */
    if (!row) {
        while(buffer->numrows <= filerow)
            editorInsertRow(buffer->numrows,"",0);
    }
    row = &buffer->row[filerow];
    editorRowInsertChar(row,filecol,c);
    if (buffer->cx == buffer->screencols-1)
        buffer->coloff++;
    else
        buffer->cx++;
    buffer->dirty++;
}

/* Inserting a newline is slightly complex as we have to handle inserting a
 * newline in the middle of a line, splitting the line as needed. */
void editorInsertNewline(void) {
    int filerow = buffer->rowoff+buffer->cy;
    int filecol = buffer->coloff+buffer->cx;
    erow *row = (filerow >= buffer->numrows) ? NULL : &buffer->row[filerow];

    if (!row) {
        if (filerow == buffer->numrows) {
            editorInsertRow(filerow,"",0);
            goto fixcursor;
        }
        return;
    }
    /* If the cursor is over the current line size, we want to conceptually
     * think it's just over the last character. */
    if (filecol >= row->size) filecol = row->size;
    if (filecol == 0) {
        editorInsertRow(filerow,"",0);
    } else {
        /* We are in the middle of a line. Split it between two rows. */
        editorInsertRow(filerow+1,row->chars+filecol,row->size-filecol);
        row = &buffer->row[filerow];
        row->chars[filecol] = '\0';
        row->size = filecol;
        editorUpdateRow(row);
    }
fixcursor:
    if (buffer->cy == buffer->screenrows-1) {
        buffer->rowoff++;
    } else {
        buffer->cy++;
    }
    buffer->cx = 0;
    buffer->coloff = 0;
}

/* Delete the char at the current prompt position. */
void editorDelChar() {
    int filerow = buffer->rowoff+buffer->cy;
    int filecol = buffer->coloff+buffer->cx;
    erow *row = (filerow >= buffer->numrows) ? NULL : &buffer->row[filerow];

    if (!row || (filecol == 0 && filerow == 0)) return;
    if (filecol == 0) {
        /* Handle the case of column 0, we need to move the current line
         * on the right of the previous one. */
        filecol = buffer->row[filerow-1].size;
        editorRowAppendString(&buffer->row[filerow-1],row->chars,row->size);
        editorDelRow(filerow);
        row = NULL;
        if (buffer->cy == 0)
            buffer->rowoff--;
        else
            buffer->cy--;
        buffer->cx = filecol;
        if (buffer->cx >= buffer->screencols) {
            int shift = (buffer->screencols-buffer->cx)+1;
            buffer->cx -= shift;
            buffer->coloff += shift;
        }
    } else {
        editorRowDelChar(row,filecol-1);
        if (buffer->cx == 0 && buffer->coloff)
            buffer->coloff--;
        else
            buffer->cx--;
    }
    if (row) editorUpdateRow(row);
    buffer->dirty++;
}

/* Load the specified program in the editor memory and returns 0 on success
 * or 1 on error. */
int editorOpen(char *filename) {
    FILE *fp;

    /* If we already have this buffer open, then switch to it. */
    bufferConfig *config;
    if ((config = editorFindBuffer(filename))) {
      buffer = config;
      return 0;
    }

    buffer = calloc(1, sizeof(bufferConfig));
    buffer->selection_row = -1;
    if (getWindowSize(STDIN_FILENO, STDOUT_FILENO, &buffer->screenrows,
                      &buffer->screencols) == -1) {
      perror("Unable to query the screen for size (columns / rows)");
      exit(1);
    }
    buffer->screenrows -= 2; /* Get room for status bar. */

    ptrVectorPushBack(&openBuffers, buffer);

    buffer->dirty = 0;
    free(buffer->filename);
    buffer->filename = strdup(filename);

    fp = fopen(filename,"r");
    if (!fp) {
      if (errno != ENOENT) {
        perror("Opening file");
        exit(1);
      }
      return 1;
    }

    editorSelectSyntaxHighlight(filename);

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
      if (linelen && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
        line[--linelen] = '\0';
      editorInsertRow(buffer->numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    buffer->dirty = 0;
    return 0;
}

/* Save the current file on disk. Return 0 on success, 1 on error. */
int editorSave() {
    int len;
    char *buf = editorRowsToString(&len);
    int fd = open(buffer->filename,O_RDWR|O_CREAT,0644);
    if (fd == -1) goto writeerr;

    /* Use truncate + a single write(2) call in order to make saving
     * a bit safer, under the limits of what we can do in a small editor. */
    if (ftruncate(fd,len) == -1) goto writeerr;
    if (write(fd,buf,len) != len) goto writeerr;

    close(fd);
    free(buf);
    buffer->dirty = 0;
    editorSetStatusMessage("%d bytes written on disk", len);
    return 0;

writeerr:
    free(buf);
    if (fd != -1) close(fd);
    editorSetStatusMessage("Can't save! I/O error: %s",strerror(errno));
    return 1;
}

void editorQuit(int force) {
  if (buffer->dirty && !force) {
    editorSetStatusMessage("WARNING!!! File has unsaved changes."
                           "Do you want to continue? (y/n)");
    editorRefreshScreen();
    char c = editorReadKey(STDIN_FILENO);
    if (!(c == 'y' || c == 'Y')) {
      editorSetStatusMessage("");
      return;
    }
  }
  exit(0);
}

/* ============================= Terminal update ============================ */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
  char *b;
  int len, cap;
};

static inline void abInit(struct abuf *ab, int init) {
  ab->b = malloc(ab->cap = init);
  ab->len = 0;
}

static inline void abAppendLen(struct abuf *ab, const char *s, int len) {
  while (ab->cap < ab->len + len)
    ab->b = realloc(ab->b, ab->cap *= 1.6f);
  memcpy(ab->b + ab->len, s, len);
  ab->len += len;
}

static inline void abAppend(struct abuf *ab, const char *s) {
  abAppendLen(ab, s, strlen(s));
}

static void abFree(struct abuf *ab) { free(ab->b); }

bool editorIsPointInRegion(int x, int y) {
  switch (buffer->mode) {
  case VM_VISUAL_CHAR:
    if (y != cursorY() && y != regionY())
      return clamp(cursorY(), regionY(), y);

    if (cursorY() == regionY())
      return y == cursorY() && inclusive_clamp(cursorX(), regionX(), x);

    if (cursorY() == y)
      return cursorY() < regionY() ? x >= cursorX() : x <= cursorX();

    assert(regionY() == y);

    return cursorY() < regionY() ? x <= regionX() : x >= regionX();
  case VM_VISUAL_LINE:
    return inclusive_clamp(cursorY(), regionY(), y);

  default:
    return false;
  }
}

#define T_INVERSE_BEGIN "\x1b[7m"
#define T_INVERSE_END "\x1b[27m"

/* This function writes the whole screen using VT100 escape characters
 * starting from the logical state of the editor in the global state 'E'. */
void editorRefreshScreen(void) {
  int y;
  bool inRegion = false;
  erow *r;
  char buf[32];
  struct abuf ab;
  abInit(&ab, 4096);

  abAppend(&ab, "\x1b[?25l"); /* Hide cursor. */
  abAppend(&ab, "\x1b[H");    /* Go home. */
  for (y = 0; y < buffer->screenrows; y++) {
    int filerow = buffer->rowoff + y;

    if (filerow >= buffer->numrows) {
      if (buffer->numrows == 0 && y == buffer->screenrows / 3) {
        char welcome[80];
        int welcomelen =
            snprintf(welcome, sizeof(welcome),
                     "Kilo editor -- verison %s\x1b[0K\r\n", KILO_VERSION);
        int padding = (buffer->screencols - welcomelen) / 2;
        if (padding) {
          abAppend(&ab, "~");
          padding--;
        }
        while (padding--)
          abAppend(&ab, " ");
        abAppendLen(&ab, welcome, welcomelen);
      } else {
        abAppend(&ab, "~\x1b[0K\r\n");
      }
      continue;
    }

    r = &buffer->row[filerow];

    int len = r->rsize - buffer->coloff;
    int current_color = -1;
    if (len > 0) {
      if (len > buffer->screencols)
        len = buffer->screencols;
      char *c = r->render + buffer->coloff;
      unsigned char *hl = r->hl + buffer->coloff;
      int j;
      for (j = 0; j < len; j++) {
        if (buffer->selection_row != -1 &&
            inRegion != editorIsPointInRegion(buffer->coloff + j, buffer->rowoff + y)) {
          if (inRegion)
            abAppend(&ab, T_INVERSE_END);
          else
            abAppend(&ab, T_INVERSE_BEGIN);

          inRegion = !inRegion;
        }
        if (hl[j] == HL_NONPRINT) {
          char sym;
          abAppend(&ab, "\x1b[7m");
          if (c[j] <= 26)
            sym = '@' + c[j];
          else
            sym = '?';
          abAppendLen(&ab, &sym, 1);
          abAppend(&ab, "\x1b[0m");
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(&ab, "\x1b[39m");
            current_color = -1;
          }
          abAppendLen(&ab, c + j, 1);
        } else {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            current_color = color;
            abAppendLen(&ab, buf, clen);
          }
          abAppendLen(&ab, c + j, 1);
        }
      }
    }
    abAppend(&ab, "\x1b[39m");
    abAppend(&ab, "\x1b[0K");
    abAppend(&ab, "\r\n");
  }

  /* Create a two rows status. First row: */
  abAppend(&ab, "\x1b[0K");
  abAppend(&ab, "\x1b[7m");
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", buffer->filename,
                     buffer->numrows, buffer->dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", buffer->rowoff + buffer->cy + 1,
                      buffer->numrows);
  if (len > buffer->screencols)
    len = buffer->screencols;
  abAppendLen(&ab, status, len);
  while (len < buffer->screencols) {
    if (buffer->screencols - len == rlen) {
      abAppendLen(&ab, rstatus, rlen);
      break;
    } else {
      abAppend(&ab, " ");
      len++;
    }
  }
  abAppend(&ab, "\x1b[0m\r\n");

  /* Second row depends on buffer->statusmsg and the status message update time. */
  abAppend(&ab, "\x1b[0K");
  int msglen = strlen(buffer->statusmsg);
  if (msglen && time(NULL) - buffer->statusmsg_time < 5)
    abAppendLen(&ab, buffer->statusmsg, msglen <= buffer->screencols ? msglen : buffer->screencols);

  /* Put cursor at its current position. Note that the horizontal position
   * at which the cursor is displayed may be different compared to 'buffer->cx'
   * because of TABs. */
  int j;
  int cx = 1;
  int filerow = buffer->rowoff + buffer->cy;
  erow *row = (filerow >= buffer->numrows) ? NULL : &buffer->row[filerow];
  if (row) {
    for (j = buffer->coloff; j < (buffer->cx + buffer->coloff); j++) {
      if (j < row->size && row->chars[j] == TAB)
        cx += 7 - ((cx) % 8);
      cx++;
    }
  } else {
    /* We're in the status bar! */
    cx += buffer->cx;
  }
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", buffer->cy + 1, cx);
  abAppendLen(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h"); /* Show cursor. */
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/* Set an editor status message for the second line of the status, at the
 * end of the screen. */
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(buffer->statusmsg,sizeof(buffer->statusmsg),fmt,ap);
    va_end(ap);
    buffer->statusmsg_time = time(NULL);
}

char *editorReadStringFromStatusBar(char *prefix) {
  int init_offset = strlen(prefix);
  editorSetStatusMessage(prefix);

  int restoreCX = buffer->cx, restoreCY = buffer->cy;

  buffer->cx = init_offset;
  buffer->cy = buffer->screencols - 1;

  int inspos = 0;
  int endpos = 0;

  int bufsz = 32;
  char *str = malloc(bufsz + init_offset);
  char statmsg[80];
  strcpy(statmsg, prefix);

  for (;;) {
    strcpy(statmsg + init_offset, str);
    editorSetStatusMessage(statmsg);
    editorRefreshScreen();
    int c = editorReadKey(STDIN_FILENO);
    switch (c) {
    case ENTER:
      str[endpos] = '\0';
      goto done;
    case DEL_KEY:
    case BACKSPACE:
      if (inspos) {
        memmove(str + inspos - 1, str + inspos, endpos - inspos + 1);
        inspos--;
        endpos--;
        buffer->cx--;
      }
      break;

    case ARROW_RIGHT:
      if (inspos < endpos) {
        inspos++;
        buffer->cx++;
        editorRefreshScreen();
      }
      break;
    case ARROW_LEFT:
      if (inspos) {
        inspos--;
        buffer->cx--;
        editorRefreshScreen();
      }
      break;
    case TAB: {
      str[endpos] = '\0';
      char *autocomplete = lookupPartialColonFunction(str);
      if (autocomplete == NULL) {
        break;
      }
      int autolen = strlen(autocomplete);
      while (endpos + autolen >= bufsz) {
        str = realloc(str, bufsz *= 2);
      }
      str = strcat(str, autocomplete);
      endpos = endpos + autolen;
      inspos = endpos;
      break;
    }

    case CTRL_C: /* Everything else: just bail out. */
    case CTRL_Q:
    case CTRL_S:
    case PAGE_UP:
    case PAGE_DOWN:
    case ARROW_UP:
    case ARROW_DOWN:
      goto fail;

    default:
      if (isprint(c)) {
        if (endpos == bufsz)
          str = realloc(str, bufsz *= 2);
        memmove(str + inspos + 1, str + inspos, endpos - inspos + 1);
        str[inspos++] = c;
        endpos++;
        buffer->cx++;
      }
    }
  }

fail:
  free(str);
  str = NULL;

done:
  editorSetStatusMessage("");
  buffer->cy = restoreCY;
  buffer->cx = restoreCX;
  editorRefreshScreen();
  return str;
}

/* Set the cursor to the absoulute coorodinates x, y */
bool editorSetCursorPos(int x, int y) {
  if (y < 0 || y >= buffer->numrows || x < 0 || x > buffer->row[y].size)
    return true;

  buffer->cy = y - buffer->rowoff;
  buffer->cx = x - buffer->coloff;

  /* If we've moved the cursor off the screen, reset it. */
  while (buffer->cy >= buffer->screenrows) {
    ++buffer->rowoff;
    --buffer->cy;
  }
  while (buffer->cy < 0) {
    --buffer->rowoff;
    ++buffer->cy;
  }

  return false;
}

void openLogFile(char *filename) {
  logfile = fopen(filename, "w+");
}

void logmsg(char *fmt, ...) {
  va_list vl;
  va_start(vl, fmt);
  vfprintf(logfile, fmt, vl);
  fflush(logfile);
}

/* ========================= Editor buffer handling  ======================== */

bufferConfig *editorFindBuffer(char *name) {
  int idx;
  for (idx = 0; idx < openBuffers.idx; ++idx)
    if (strcmp(name, ((bufferConfig *)openBuffers.data[idx])->filename) == 0)
      return openBuffers.data[idx];
  return NULL;
}

void editorSwitchBuffer() {
  char *name = "";
  bufferConfig *cfg = editorFindBuffer(name);
  if (!cfg)
    return;// true;
  buffer = cfg;
  return;// false;
}

/* ========================= Editor events handling  ======================== */

/* Handle cursor position change because arrow keys were pressed. */
void editorMoveCursor(enum DIRECTION dir) {
    int filerow = buffer->rowoff+buffer->cy;
    int filecol = buffer->coloff+buffer->cx;
    int rowlen;
    erow *row = (filerow >= buffer->numrows) ? NULL : &buffer->row[filerow];

    switch(dir) {
    case LEFT:
        if (buffer->cx == 0) {
            if (buffer->coloff) {
                buffer->coloff--;
            } else {
                if (filerow > 0) {
                    buffer->cy--;
                    buffer->cx = buffer->row[filerow-1].size;
                    if (buffer->cx > buffer->screencols-1) {
                        buffer->coloff = buffer->cx-buffer->screencols+1;
                        buffer->cx = buffer->screencols-1;
                    }
                }
            }
        } else {
            buffer->cx -= 1;
        }
        break;
    case RIGHT:
        if (row && filecol < row->size) {
            if (buffer->cx == buffer->screencols-1) {
                buffer->coloff++;
            } else {
                buffer->cx += 1;
            }
        } else if (row && filecol == row->size) {
            buffer->cx = 0;
            buffer->coloff = 0;
            if (buffer->cy == buffer->screenrows-1) {
                buffer->rowoff++;
            } else {
                buffer->cy += 1;
            }
        }
        break;
    case UP:
        if (buffer->cy == 0) {
            if (buffer->rowoff) buffer->rowoff--;
        } else {
            buffer->cy -= 1;
        }
        break;
    case DOWN:
        if (filerow < buffer->numrows) {
            if (buffer->cy == buffer->screenrows-1) {
                buffer->rowoff++;
            } else {
                buffer->cy += 1;
            }
        }
        break;
    }
    /* Fix cx if the current line has not enough chars. */
    filerow = buffer->rowoff+buffer->cy;
    filecol = buffer->coloff+buffer->cx;
    row = (filerow >= buffer->numrows) ? NULL : &buffer->row[filerow];
    rowlen = row ? row->size : 0;
    if (filecol > rowlen) {
        buffer->cx -= filecol-rowlen;
        if (buffer->cx < 0) {
            buffer->coloff += buffer->cx;
            buffer->cx = 0;
        }
    }
}

void editorMoveCursorToRowEnd() {
    int filerow = buffer->rowoff+buffer->cy;
    int filecol = buffer->coloff+buffer->cx;
    erow *row = (filerow >= buffer->numrows) ? NULL : &buffer->row[filerow];
    if (row == NULL) {
        return;
    }
    int rowlen = row ? row->size : 0;
    int size = rowlen - buffer->cx-buffer->coloff;

    while (size != 0) {
        editorMoveCursor(RIGHT);
        size = size -1;
    }
}

bool editorMoveCursorToFirst(char c) {
  int filerow = buffer->rowoff+buffer->cy;
  erow *row = (filerow >= buffer->numrows) ? NULL : &buffer->row[filerow];

  if (row == NULL) {
    return 0;
  }

  int size = row->size;

  int i = buffer->cx+buffer->coloff;
  int initialOffSet = cursorX();
  bool found = false;

  while (row->chars[i] != c && i != size) {
    if (row->chars[i+1] == c) {
      found = true;
    }
    i++;
  }

  if (found == true) {
    while (i - initialOffSet != 0) {
      i--;
      editorMoveCursor(RIGHT);
    }
  }
  return found;
}

int editorFileWasModified(void) {
    return buffer->dirty;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr,"Usage: kilo <filename>\n");
        exit(1);
    }

    ptrVectorInit(&openBuffers);

    openLogFile(LOG_FILENAME);
    logmsg("editor is initializing...\n");
    editorOpen(argv[1]);
    initUser();
    enableRawMode(STDIN_FILENO);
    editorSetStatusMessage(
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
    while(1) {
        editorRefreshScreen();
        editorProcessKeypress(STDIN_FILENO);
    }
    return 0;
}
