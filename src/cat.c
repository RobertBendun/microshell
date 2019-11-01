#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>

#define BUF_SIZE 16384

void transfer(int fd_in, int fd_out, int *pipefd)
{
  while (splice(fd_in, NULL, pipefd[1], NULL, BUF_SIZE, 0))
    splice(pipefd[0], NULL, fd_out, NULL, BUF_SIZE, 0);
}

void transfer_fallback(int fd_in, int fd_out, int *pipefd)
{
  char buffer[BUF_SIZE];
  ssize_t r;

  while (r = read(fd_in, buffer, BUF_SIZE))
    do 
      r -= write(fd_out, buffer, r);
    while (r > 0);
}

void direct_splice(int fd_in, int fd_out, int *pipefd)
{
  while (splice(fd_in, NULL, fd_out, NULL, BUF_SIZE, 0))
    ;
}

int main(int argc, char** argv) {
  int pipefd[2];
  (void) pipe(pipefd);

  struct stat stat;
  fstat(STDOUT_FILENO, &stat);

  void(*t)(int, int, int*);

  if (S_ISFIFO(stat.st_mode))
    t = direct_splice;
  else
    t = (fcntl(STDOUT_FILENO, F_GETFL, 0) & O_APPEND) ? transfer_fallback : transfer;

  for (int i = 1; i < argc; ++i) {
    int fd = strcmp("-", argv[i]) ? open(argv[i], O_RDONLY) : STDIN_FILENO;
    if (fd < 0) {
      fprintf(stderr, "%s: No such file or directory\n", argv[i]);
      exit(1);
    }

    t(fd, STDOUT_FILENO, pipefd);
    close(fd);
  }

  return 0;
}
