#include <unistd.h>

#include "process_keypress.h"
#include "colon.h"
#include "function.h"
#include "kilo.h"
#include <unistd.h>

#define ENTER_MODE(NAME)                                                       \
  do {                                                                         \
    buffer->mode = VM_##NAME;                                                        \
    editorSetStatusMessage(#NAME);                                             \
  } while (0)

static textObject editorParseTextObjectOverride(char override) {
  if (buffer->selection_row != -1)
    return editorRegionObject();

  int c = override ? override : editorReadKey(STDIN_FILENO);

  bool isInner = false;
  switch (c) {
  case 'i':
    isInner = true;
    c = editorReadKey(STDIN_FILENO);
    break;
  }

  textObject obj;

  switch (c) {
  case 'w':
    obj = editorWordAtPoint(cursorX(), cursorY(),
                            isInner ? TOK_INNER : TOK_RIGHT);
    break;
  case 'b':
    obj =
        editorWordAtPoint(cursorX(), cursorY(), isInner ? TOK_INNER : TOK_LEFT);
    break;
  case '%':
    obj = editorComplementTextObject(cursorX(), cursorY());
    break;
  case '(':
  case ')':
    return editorPairAtPoint(cursorX(), cursorY(), '(', ')', isInner);
  case '{':
  case '}':
    return editorPairAtPoint(cursorX(), cursorY(), '{', '}', isInner);
  case '<':
  case '>':
    return editorPairAtPoint(cursorX(), cursorY(), '<', '>', isInner);
  case '[':
  case ']':
    return editorPairAtPoint(cursorX(), cursorY(), '[', ']', isInner);
  case '"':
  case '\'':
    return editorPairAtPoint(cursorX(), cursorY(), c, c, isInner);

  default:
    return EMPTY_TEXT_OBJECT;
  }

  return obj;
}

static textObject editorParseTextObject() {
  return editorParseTextObjectOverride('\0');
}

/* Process events arriving from the standard input, which is, the user
 * is typing stuff on the terminal. */
void editorProcessKeypress(int fd) {
  /* When the file is modified, requires Ctrl-q to be pressed N times
   * before actually quitting. */

  int c = editorReadKey(fd);

  if (buffer->mode == VM_NORMAL || buffer->mode == VM_VISUAL_CHAR ||
      buffer->mode == VM_VISUAL_LINE) {
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
      if (c == PAGE_UP && buffer->cy != 0)
        buffer->cy = 0;
      else if (c == PAGE_DOWN && buffer->cy != buffer->screenrows - 1)
        buffer->cy = buffer->screenrows - 1;
      {
        int times = buffer->screenrows;
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
      editorInsertRow(buffer->cy + buffer->rowoff + 1, "", 0);
      editorMoveCursor(DOWN);
      ENTER_MODE(INSERT);
      break;
    case 'O':
      editorInsertRow(buffer->cy + buffer->rowoff, "", 0);
      ENTER_MODE(INSERT);
      break;
    case 'A':
      editorMoveCursorToRowEnd();
      ENTER_MODE(INSERT);
      break;
    case 'f':
      if (editorMoveCursorToFirst(editorReadKey(STDIN_FILENO))) {
        ENTER_MODE(INSERT);
      }
      break;
    case 'q':
      exit(0);
    case 'i':
      ENTER_MODE(INSERT);
      break;
    case 'x':
      editorDelChar();
      break;
    case 'v':
      if (buffer->mode == VM_VISUAL_CHAR) {
        ENTER_MODE(NORMAL);
        buffer->selection_row = -1;
        buffer->selection_offset = 0;
        break;
      }
      if (buffer->mode == VM_NORMAL) {
        buffer->selection_row = buffer->rowoff + buffer->cy;
        buffer->selection_offset = buffer->coloff + buffer->cx;
      }
      ENTER_MODE(VISUAL_CHAR);
      break;
    case 'V':
      if (buffer->mode == VM_VISUAL_LINE) {
        ENTER_MODE(NORMAL);
        buffer->selection_row = -1;
        buffer->selection_offset = 0;
        break;
      }
      if (buffer->mode == VM_NORMAL) {
        buffer->selection_row = buffer->rowoff + buffer->cy;
        buffer->selection_offset = 0;
      }
      ENTER_MODE(VISUAL_LINE);
      break;
    case 'd': {
      textObject obj = editorParseTextObject();
      if (!badTextObject(obj))
        editorDeleteTextObject(obj);

      buffer->selection_row = -1;
      buffer->selection_offset = 0;
      ENTER_MODE(NORMAL);
      break;
    }
    case 'w':
    case 'b':
    case '%': {
      textObject obj = editorParseTextObjectOverride(c);
      if (badTextObject(obj))
        break;

      if (obj.firstX == cursorX() && obj.firstY == cursorY())
        editorSetCursorPos(obj.secondX, obj.secondY);
      else
        editorSetCursorPos(obj.firstX, obj.firstY);

      break;
    }
    case CTRL_L: /* ctrl+l, clear screen */
      /* Just refresh the line as side effect. */
      break;
    case ESC:
      if (buffer->mode != VM_NORMAL) {
        buffer->selection_row = -1;
        buffer->selection_offset = 0;
        ENTER_MODE(NORMAL);
      }
      break;
    default:
      /* Unhandled input, ignore. */
      break;
    }
  } else if (buffer->mode == VM_INSERT) {
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
      ENTER_MODE(NORMAL);
      break;
    default:
      editorInsertChar(c);
    }
  }
}
