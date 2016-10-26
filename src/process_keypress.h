#ifndef KILO_PROCESS_KEYPRESS_H
#define KILO_PROCESS_KEYPRESS_H

enum vimMode {
  VM_NORMAL,
  VM_VISUAL_CHAR,
  VM_VISUAL_LINE,
  VM_INSERT,
};

void editorProcessKeypress(int fd);

#endif
