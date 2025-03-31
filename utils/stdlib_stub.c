extern int _end;

void *_sbrk(int incr) {
  unsigned char *prev_heap;

  return prev_heap;
}

int _close(int file) {
  return -1;
}

int _fstat(int file) {
  return 0;
}

int _isatty(int file) {
  return 1;
}

int _lseek(int file, int ptr, int dir) {
  return 0;
}

void _exit(int status) {
  return;
}

void _kill(int pid, int sig) {
  return;
}

int _getpid(void) {
  return -1;
}

int _write (int file, char * ptr, int len) {
  return -1;
}

int _read (int file, char * ptr, int len) {
  return -1;
}