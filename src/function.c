#include "function.h"
#include "kilo.h"
#include <string.h>
#include <ctype.h>
#include <unistd.h>

/* =============================== Find mode ================================ */

#define KILO_QUERY_LEN 256

void editorFind(int fd) {
    char query[KILO_QUERY_LEN+1] = {0};
    int qlen = 0;
    int last_match = -1; /* Last line where a match was found. -1 for none. */
    int find_next = 0; /* if 1 search next, if -1 search prev. */
    int saved_hl_line = -1;  /* No saved HL */
    char *saved_hl = NULL;

#define FIND_RESTORE_HL do { \
    if (saved_hl) { \
        memcpy(buffer->row[saved_hl_line].hl,saved_hl, buffer->row[saved_hl_line].rsize); \
        saved_hl = NULL; \
    } \
} while (0)

    /* Save the cursor position in order to restore it later. */
    int saved_cx = buffer->cx, saved_cy = buffer->cy;
    int saved_coloff = buffer->coloff, saved_rowoff = buffer->rowoff;

    while(1) {
        editorSetStatusMessage(
            "Search: %s (Use ESC/Arrows/Enter)", query);
        editorRefreshScreen();

        int c = editorReadKey(fd);
        if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
            if (qlen != 0) query[--qlen] = '\0';
            last_match = -1;
        } else if (c == ESC || c == ENTER) {
            if (c == ESC) {
                buffer->cx = saved_cx; buffer->cy = saved_cy;
                buffer->coloff = saved_coloff; buffer->rowoff = saved_rowoff;
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
        if (last_match == -1) find_next = 1;
        if (find_next) {
            char *match = NULL;
            int match_offset = 0;
            int i, current = last_match;

            for (i = 0; i < buffer->numrows; i++) {
                current += find_next;
                if (current == -1) current = buffer->numrows-1;
                else if (current == buffer->numrows) current = 0;
                match = strstr(buffer->row[current].render,query);
                if (match) {
                    match_offset = match-buffer->row[current].render;
                    break;
                }
            }
            find_next = 0;

            /* Highlight */
            FIND_RESTORE_HL;

            if (match) {
                erow *row = &buffer->row[current];
                last_match = current;
                if (row->hl) {
                    saved_hl_line = current;
                    saved_hl = malloc(row->rsize);
                    memcpy(saved_hl,row->hl,row->rsize);
                    memset(row->hl+match_offset,HL_MATCH,qlen);
                }
                buffer->cy = 0;
                buffer->cx = match_offset;
                buffer->rowoff = current;
                buffer->coloff = 0;
                /* Scroll horizontally as needed. */
                if (buffer->cx > buffer->screencols) {
                    int diff = buffer->cx - buffer->screencols;
                    buffer->cx -= diff;
                    buffer->coloff += diff;
                }
            }
        }
    }
}

void quitWithPrompt() {
  editorQuit(0);
}

void quitForce() {
  editorQuit(1);
}

void saveIgnoreError() {
  editorSave();
}

void saveAndQuit() {
  saveIgnoreError();
  quitWithPrompt();
}
