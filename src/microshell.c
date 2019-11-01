#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>

#include "core.h"
#include "terminal.h"

char const *default_ps1 = "\\e[32;1m\\u@\\h\\e[0m[\\e[34m\\w\\e[0m] \\P ";


void print_evaluated_ps1(char const *shell_exec_name, int has_root_privilages, int last_command_result)
{
  int escape_next = false;
  char buffer[BUFSIZ];

  char const *ps1 = getenv("PS1");
  if (!ps1)
    ps1 = default_ps1;

  for (; *ps1 != '\0'; ++ps1) {
    if (*ps1 == '\\') {
      escape_next = true;
      continue;
    }
    
    if (escape_next) {
      escape_next = false;
      switch (*ps1) {
        case '\\': putchar('\\'); break;
        case 'e': putchar('\x1b'); break;
        case 'n': putchar('\n'); break;
        case 'r': putchar('\r'); break;
        case 'a': putchar('\007'); break;
        case 's': fputs(shell_exec_name, stdout); break;
        case '$': putchar(has_root_privilages ? '#' : '$'); break;
        
        case 'h':
        case 'H': /* hostname */
          gethostname(buffer, BUFSIZ);
          fputs(buffer, stdout);
          break;
        
        case 'l': /* name of terminal shell device */
          fputs(basename(ttyname(STDOUT_FILENO)), stdout);
          break;
        
        case 'u': /* username */
          fputs(getenv("USER"), stdout);
          break;

        case 'w': /* current working directory */
          fputs(getcwd(buffer, BUFSIZ), stdout);
          break;
        
        case 'W': /* basename of current working directory */
          fputs(basename(getcwd(buffer, BUFSIZ)), stdout);
          break;
        
        case 'P': /* custom: color highlighet $ or # depending on result of previous command */
          printf("%s%c\x1b[0m", 
            last_command_result == 0 ? GREEN : RED, 
            has_root_privilages ? '#' : '$');
          break;  
      }
    }
    else
      putchar(*ps1);
  }
}


StringView readline(FILE *in)
{
  StringView result;
  size_t len = 0;
  ssize_t length = 0;

  result.begin = result.end = NULL;
  memset(&result, 0, sizeof(result));
  if ((length = getline(&(result.begin), &len, in)) < 0) {
    perror("getline");
    free(result.begin);
    result.begin = result.end = NULL;
    return result;
  }

  result.end = result.begin + length;
  return result;
}

StringView trim(StringView sv)
{
  while (sv.begin != sv.end && (*sv.begin == ' ' || *sv.begin == '\t'))
    ++sv.begin;
  
  while (sv.begin != sv.end && (*(sv.end - 1) == ' ' || *(sv.end - 1) == '\t'))
    --sv.end;

  return sv;
}

int strview_str_cmp(StringView sv, char const* str)
{
  while (sv.begin != sv.end) {
    if (*sv.begin != *str)
      break;
    ++sv.begin;
    ++str;
  }

  if (sv.begin == sv.end)
    return  *str == '\0' ? 0 : -1;

  return ((unsigned char)*sv.begin) - ((unsigned char)*str);
}

int main(int argc, char const* *argv)
{
  StringView input;
  StringView command;

  for (;;) {
    print_evaluated_ps1(argv[0], false, 0);
    if (!(input = readline(stdin)).begin)
      break;
    
    command = input;
    command.end--;
    command = trim(command);
    if (strview_str_cmp(command, "exit") == 0)
      exit(0);

    free(command.begin);
  }
  return 0;
}
