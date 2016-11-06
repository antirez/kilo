#ifndef KILO_PROCESS_KEYPRESS_H
#define KILO_PROCESS_KEYPRESS_H

enum vimMode {
  VM_NORMAL,
  VM_INSERT,
  VM_SELECTION,
};

void editorProcessKeypress(int fd);
void editorMoveCursorToRowEnd();

#endif
