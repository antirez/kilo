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

struct editorConfig E;

FILE *logfile;

void editorSetStatusMessage(const char *fmt, ...);

/* ======================= Low level terminal handling ====================== */

static struct termios orig_termios; /* In order to restore at exit.*/

void disableRawMode(int fd) {
    /* Don't even check the return value as it's too late. */
    if (E.rawmode) {
        tcsetattr(fd,TCSAFLUSH,&orig_termios);
        E.rawmode = 0;
    }
}

/* Called at exit to avoid remaining in raw mode. */
void editorAtExit(void) {
    disableRawMode(STDIN_FILENO);
}

/* Raw mode: 1960 magic shit. */
int enableRawMode(int fd) {
    struct termios raw;

    if (E.rawmode) return 0; /* Already enabled. */
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
    E.rawmode = 1;
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
    if (at > E.numrows) return;
    E.row = realloc(E.row,sizeof(erow)*(E.numrows+1));
    if (at != E.numrows) {
        memmove(E.row+at+1,E.row+at,sizeof(E.row[0])*(E.numrows-at));
        for (int j = at+1; j <= E.numrows; j++) E.row[j].idx++;
    }
    E.row[at].size = len;
    E.row[at].chars = malloc(len+1);
    memcpy(E.row[at].chars,s,len+1);
    E.row[at].hl = NULL;
    E.row[at].hl_oc = 0;
    E.row[at].render = NULL;
    E.row[at].rsize = 0;
    E.row[at].idx = at;
    editorUpdateRow(E.row+at);
    E.numrows++;
    E.dirty++;
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

    if (at >= E.numrows) return;
    row = E.row+at;
    editorFreeRow(row);
    memmove(E.row+at,E.row+at+1,sizeof(E.row[0])*(E.numrows-at-1));
    for (int j = at; j < E.numrows-1; j++) E.row[j].idx++;
    E.numrows--;
    E.dirty++;
}

void editorDeleteRows(int begin_row, int end_row) {
  if (begin_row > end_row)
    SWAP(begin_row, end_row);

  int iter = end_row - begin_row + 1;
  while (iter--)
    editorDelRow(begin_row);
}

void editorDeleteSelection(int begin_row, int begin_col, int end_row,
                           int end_col) {
  if (begin_col > E.row[begin_row].size || end_col > E.row[end_row].size)
    return;

  char *begin, *end;

  if (begin_row > end_row || (begin_row == end_row && begin_col > end_col)) {
    begin = strndup(E.row[end_row].chars, end_col);
    end = strdup(E.row[begin_row].chars + begin_col + 1);
  } else {
    /* Join the begin of begin_row and the end of end_row together */
    begin = strndup(E.row[begin_row].chars, begin_col);
    end = strdup(E.row[end_row].chars + end_col + 1);
  }

  int size = strlen(begin) + strlen(end);

  begin = realloc(begin, size + 1);
  strcat(begin, end);

  editorDeleteRows(begin_row, end_row);
  editorInsertRow(begin_row < end_row ? begin_row : end_row, begin, size);

  free(end);
  free(begin);

  E.cx = begin_col < end_col ? begin_col : end_col;
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
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size+1; /* +1 is for "\n" at end of every row */
    *buflen = totlen;
    totlen++; /* Also make space for nulterm */

    p = buf = malloc(totlen);
    for (j = 0; j < E.numrows; j++) {
        memcpy(p,E.row[j].chars,E.row[j].size);
        p += E.row[j].size;
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
    E.dirty++;
}

/* Append the string 's' at the end of a row */
void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars,row->size+len+1);
    memcpy(row->chars+row->size,s,len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

/* Delete the character at offset 'at' from the specified row. */
void editorRowDelChar(erow *row, int at) {
    if (row->size <= at) return;
    memmove(row->chars+at,row->chars+at+1,row->size-at);
    editorUpdateRow(row);
    row->size--;
    E.dirty++;
}

/* Insert the specified char at the current prompt position. */
void editorInsertChar(int c) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    /* If the row where the cursor is currently located does not exist in our
     * logical representaion of the file, add enough empty rows as needed. */
    if (!row) {
        while(E.numrows <= filerow)
            editorInsertRow(E.numrows,"",0);
    }
    row = &E.row[filerow];
    editorRowInsertChar(row,filecol,c);
    if (E.cx == E.screencols-1)
        E.coloff++;
    else
        E.cx++;
    E.dirty++;
}

/* Inserting a newline is slightly complex as we have to handle inserting a
 * newline in the middle of a line, splitting the line as needed. */
void editorInsertNewline(void) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row) {
        if (filerow == E.numrows) {
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
        row = &E.row[filerow];
        row->chars[filecol] = '\0';
        row->size = filecol;
        editorUpdateRow(row);
    }
fixcursor:
    if (E.cy == E.screenrows-1) {
        E.rowoff++;
    } else {
        E.cy++;
    }
    E.cx = 0;
    E.coloff = 0;
}

/* Delete the char at the current prompt position. */
void editorDelChar() {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row || (filecol == 0 && filerow == 0)) return;
    if (filecol == 0) {
        /* Handle the case of column 0, we need to move the current line
         * on the right of the previous one. */
        filecol = E.row[filerow-1].size;
        editorRowAppendString(&E.row[filerow-1],row->chars,row->size);
        editorDelRow(filerow);
        row = NULL;
        if (E.cy == 0)
            E.rowoff--;
        else
            E.cy--;
        E.cx = filecol;
        if (E.cx >= E.screencols) {
            int shift = (E.screencols-E.cx)+1;
            E.cx -= shift;
            E.coloff += shift;
        }
    } else {
        editorRowDelChar(row,filecol-1);
        if (E.cx == 0 && E.coloff)
            E.coloff--;
        else
            E.cx--;
    }
    if (row) editorUpdateRow(row);
    E.dirty++;
}

/* Load the specified program in the editor memory and returns 0 on success
 * or 1 on error. */
int editorOpen(char *filename) {
    FILE *fp;

    E.dirty = 0;
    free(E.filename);
    E.filename = strdup(filename);

    fp = fopen(filename,"r");
    if (!fp) {
        if (errno != ENOENT) {
            perror("Opening file");
            exit(1);
        }
        return 1;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while((linelen = getline(&line,&linecap,fp)) != -1) {
        if (linelen && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
            line[--linelen] = '\0';
        editorInsertRow(E.numrows,line,linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
    return 0;
}

/* Save the current file on disk. Return 0 on success, 1 on error. */
int editorSave() {
    int len;
    char *buf = editorRowsToString(&len);
    int fd = open(E.filename,O_RDWR|O_CREAT,0644);
    if (fd == -1) goto writeerr;

    /* Use truncate + a single write(2) call in order to make saving
     * a bit safer, under the limits of what we can do in a small editor. */
    if (ftruncate(fd,len) == -1) goto writeerr;
    if (write(fd,buf,len) != len) goto writeerr;

    close(fd);
    free(buf);
    E.dirty = 0;
    editorSetStatusMessage("%d bytes written on disk", len);
    return 0;

writeerr:
    free(buf);
    if (fd != -1) close(fd);
    editorSetStatusMessage("Can't save! I/O error: %s",strerror(errno));
    return 1;
}

void editorQuit(int force) {
  if (E.dirty && !force) {
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
    int len;
};

#define ABUF_INIT {NULL,0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b,ab->len+len);

    if (new == NULL) return;
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/* This function writes the whole screen using VT100 escape characters
 * starting from the logical state of the editor in the global state 'E'. */
void editorRefreshScreen(void) {
  int y;
  int in_region = 0;
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
        if (E.selection_row != -1 &&
            (E.selection_row == y && E.selection_offset == j) !=
                (E.cy + E.coloff == y && E.cx + E.rowoff == j)) {
          if (in_region) {
            abAppend(&ab, "\x1b[27m", 5);
            in_region = 0;
          } else {
            abAppend(&ab, "\x1b[7m", 4);
            in_region = 1;
          }
        }
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
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            current_color = color;
            abAppend(&ab, buf, clen);
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
  if (msglen && time(NULL) - E.statusmsg_time < 5)
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
}

/* Set an editor status message for the second line of the status, at the
 * end of the screen. */
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

char *editorReadStringFromStatusBar(char *prefix) {
  int init_offset = strlen(prefix);
  editorSetStatusMessage(prefix);

  int restoreCX = E.cx, restoreCY = E.cy;

  E.cx = init_offset;
  E.cy = E.screencols - 1;

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
        E.cx--;
      }
      break;

    case ARROW_RIGHT:
      if (inspos < endpos) {
        inspos++;
        E.cx++;
        editorRefreshScreen();
      }
      break;
    case ARROW_LEFT:
      if (inspos) {
        inspos--;
        E.cx--;
        editorRefreshScreen();
      }
      break;
    case CTRL_C: /* Everything else: just bail out. */
    case CTRL_Q:
    case CTRL_S:
    case PAGE_UP:
    case PAGE_DOWN:
    case ARROW_UP:
    case ARROW_DOWN:
      goto fail;

    default:
      if (endpos == bufsz)
        str = realloc(str, bufsz *= 2);
      memmove(str + inspos + 1, str + inspos, endpos - inspos + 1);
      str[inspos++] = c;
      endpos++;
      E.cx++;
    }
  }

fail:
  free(str);
  str = NULL;

done:
  editorSetStatusMessage("");
  E.cy = restoreCY;
  E.cx = restoreCX;
  editorRefreshScreen();
  return str;
}

void openLogFile(char *filename) {
  logfile = fopen(filename, "w+");
}

void logmsg(char *fmt, ...) {
  va_list vl;
  va_start(vl, fmt);
  vfprintf(logfile, fmt, vl);
}

/* ========================= Editor events handling  ======================== */

/* Handle cursor position change because arrow keys were pressed. */
void editorMoveCursor(enum DIRECTION dir) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    int rowlen;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    switch(dir) {
    case LEFT:
        if (E.cx == 0) {
            if (E.coloff) {
                E.coloff--;
            } else {
                if (filerow > 0) {
                    E.cy--;
                    E.cx = E.row[filerow-1].size;
                    if (E.cx > E.screencols-1) {
                        E.coloff = E.cx-E.screencols+1;
                        E.cx = E.screencols-1;
                    }
                }
            }
        } else {
            E.cx -= 1;
        }
        break;
    case RIGHT:
        if (row && filecol < row->size) {
            if (E.cx == E.screencols-1) {
                E.coloff++;
            } else {
                E.cx += 1;
            }
        } else if (row && filecol == row->size) {
            E.cx = 0;
            E.coloff = 0;
            if (E.cy == E.screenrows-1) {
                E.rowoff++;
            } else {
                E.cy += 1;
            }
        }
        break;
    case UP:
        if (E.cy == 0) {
            if (E.rowoff) E.rowoff--;
        } else {
            E.cy -= 1;
        }
        break;
    case DOWN:
        if (filerow < E.numrows) {
            if (E.cy == E.screenrows-1) {
                E.rowoff++;
            } else {
                E.cy += 1;
            }
        }
        break;
    }
    /* Fix cx if the current line has not enough chars. */
    filerow = E.rowoff+E.cy;
    filecol = E.coloff+E.cx;
    row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    rowlen = row ? row->size : 0;
    if (filecol > rowlen) {
        E.cx -= filecol-rowlen;
        if (E.cx < 0) {
            E.coloff += E.cx;
            E.cx = 0;
        }
    }
}

void editorMoveCursorToRowEnd() {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    if (row == NULL) {
        return;
    }
    int rowlen = row ? row->size : 0;
    int size = rowlen - E.cx-E.coloff;
    
    while (size != 0 ) {
        editorMoveCursor(RIGHT);
        size = size -1;
    }
}

int editorFileWasModified(void) {
    return E.dirty;
}

void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.syntax = NULL;
    E.selection_row = -1;
    E.selection_offset = 0;
    if (getWindowSize(STDIN_FILENO, STDOUT_FILENO, &E.screenrows,
                      &E.screencols) == -1) {
      perror("Unable to query the screen for size (columns / rows)");
      exit(1);
    }
    E.screenrows -= 2; /* Get room for status bar. */
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr,"Usage: kilo <filename>\n");
        exit(1);
    }

    openLogFile(LOG_FILENAME);
    logmsg("editor is initializing\n");
    initEditor();
    initUser();
    editorSelectSyntaxHighlight(argv[1]);
    editorOpen(argv[1]);
    enableRawMode(STDIN_FILENO);
    editorSetStatusMessage(
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
    while(1) {
        editorRefreshScreen();
        editorProcessKeypress(STDIN_FILENO);
    }
    return 0;
}
