#include <stdio.h>
#include <stdlib.h>

#include "vector.h"
#include "terminal.h"

Vector parse_path_env(char const *path)
{
  Vector      path_dirs;
  StringView *current;
  char const *prev = path;
 
  fill(path_dirs, 0);

  for (; *path != '\0'; ++path)
    if (*path == ':') {
      current = &vector(StringView, &path_dirs, path_dirs.size);
      current->begin = cast(char*, prev);
      current->end = cast(char*, path - 1);
      prev = path + 1;
   }

  return path_dirs;
}

int main()
{
  StringView *current;
  Vector path_dirs;
  size_t i;

  path_dirs = parse_path_env(getenv("PATH"));

  for (i = 0; i < veclen(path_dirs); ++i) {
    printf("[%2ld]: ", i);
    current = &vector(StringView, &path_dirs, i);
    fwrite(current->begin, 1, strviewlen(*current), stdout);
    putchar('\n');
  }

  return EXIT_SUCCESS;
}

