#include <unistd.h>

#include "process_keypress.h"
#include "function.h"
#include "kilo.h"
#include "colon.h"

enum vimMode mode;

/* Process events arriving from the standard input, which is, the user
 * is typing stuff on the terminal. */
void editorProcessKeypress(int fd) {
  /* When the file is modified, requires Ctrl-q to be pressed N times
   * before actually quitting. */

  int c = editorReadKey(fd);

  if (mode == VM_NORMAL || mode == VM_VISUAL_CHAR || mode == VM_VISUAL_LINE) {
    switch (c) {
    case ENTER: /* Enter */
      editorMoveCursor(DOWN);
      break;
    case CTRL_C: /* Ctrl-c */
      /* We ignore ctrl-c, it can't be so simple to lose the changes
       * to the edited file. */
      break;
    case CTRL_S: /* Ctrl-s */
      editorSave();
      break;
    case '/':
      editorFind(fd);
      break;
    case ':': {
      char *fn = editorReadStringFromStatusBar(":");
      if (fn && handleColonFunction(fn))
        editorSetStatusMessage("function '%s' not found", fn);
      break;
    }
    case BACKSPACE: /* Backspace */
    case DEL_KEY:
      editorMoveCursor(LEFT);
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
          editorMoveCursor(c == PAGE_UP ? UP : DOWN);
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c - ARROW_LEFT);
      break;
    case 'h':
      editorMoveCursor(LEFT);
      break;
    case 'j':
      editorMoveCursor(DOWN);
      break;
    case 'k':
      editorMoveCursor(UP);
      break;
    case 'l':
      editorMoveCursor(RIGHT);
      break;
    case 'o':
      editorInsertRow(E.cy + E.rowoff + 1, "", 0);
      editorMoveCursor(DOWN);
      mode = VM_INSERT;
      break;
    case 'O':
      editorInsertRow(E.cy + E.rowoff, "", 0);
      mode = VM_INSERT;
      break;
    case 'A':
      editorMoveCursorToRowEnd();
      mode = VM_INSERT;
      break;
    case 'f':
      if (1 == editorMoveCursorToFirst(editorReadKey(STDIN_FILENO))) {
        mode = VM_INSERT;
      }
      break;
    case 'q':
      exit(0);
    case 'i':
      editorSetStatusMessage("INSERT");
      mode = VM_INSERT;
      break;
    case 'x':
      editorDelChar();
      break;
    case 'v':
      if (mode == VM_VISUAL_CHAR) {
        mode = VM_NORMAL;
        E.selection_row = -1;
        E.selection_offset = 0;
        editorSetStatusMessage("INSERT");
        break;
      }
      if (mode == VM_NORMAL) {
        E.selection_row = E.rowoff + E.cy;
        E.selection_offset = E.coloff + E.cx;
      }
      mode = VM_VISUAL_CHAR;
      editorSetStatusMessage("VISUAL CHAR");
      break;
    case 'V':
      if (mode == VM_VISUAL_LINE) {
        mode = VM_NORMAL;
        E.selection_row = -1;
        E.selection_offset = 0;
        editorSetStatusMessage("INSERT");
        break;
      }
      if (mode == VM_NORMAL) {
        E.selection_row = E.rowoff + E.cy;
        E.selection_offset = 0;
      }
      mode = VM_VISUAL_LINE;
      editorSetStatusMessage("VISUAL LINE");
      break;
    case 'd':
      if (mode != VM_NORMAL) {
        if (mode == VM_VISUAL_CHAR)
          editorDeleteSelection(E.selection_row, E.selection_offset,
                                E.cy + E.rowoff, E.cx + E.coloff);
        else if (mode == VM_VISUAL_LINE)
          editorDeleteRows(E.selection_row, E.cy + E.rowoff);
        mode = VM_NORMAL;
        E.selection_row = -1;
        E.selection_offset = 0;
        editorSetStatusMessage("NORMAL");
        break;
      }
      break;
    case CTRL_L: /* ctrl+l, clear screen */
      /* Just refresh the line as side effect. */
      break;
    case ESC:
      if (mode != VM_NORMAL) {
        E.selection_row = -1;
        E.selection_offset = 0;
      }
      break;
    default:
      /* Unhandled input, ignore. */
      break;
    }
  } else if (mode == VM_INSERT) {
    switch (c) {
    case ARROW_UP: // noob
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c - ARROW_LEFT);
      break;
    case BACKSPACE: /* Backspace */
    case DEL_KEY:
      editorDelChar();
      break;
    case ENTER:
      editorInsertNewline();
      break;
    case ESC:
      mode = VM_NORMAL;
      editorSetStatusMessage("NORMAL");
      break;
    default:
      editorInsertChar(c);
    }
  }
}
