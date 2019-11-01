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

int try_match(StringView sv, char const *str)
{
  int i = 0;

  size_t count = 0;
  size_t len = strlen(str);

  for (i = 0; sv.begin != sv.end && str[i] != '\0'; ++i)
    if (sv.begin[i] == str[i])
      ++count;
    else {
      count = 0;
      break;
    }

  return count == len;
}

enum PipeType
{
  PIPE,
  AND,
  OR
};

typedef struct command
{
  StringView value;
  struct command *next;
  enum PipeType type;
} Command;

Command parse_simple_command(StringView *sv)
{
  Command cmd;
  cmd.value.begin = sv->begin;
  sv->begin = cmd.value.end = strpbrk(sv->begin, "|&");
  if (sv->begin == NULL)
    sv->begin = cmd.value.end = sv->end;
  
  return cmd;
}

Command* copy_to_heap(Command c)
{
  Command *r = malloc(sizeof(Command));
  memcpy(r, &c, sizeof(Command));
  return r;
}

Command parse_pipe(StringView sv)
{
  Command lhs;
  lhs = parse_simple_command(&sv);

  if (sv.begin == sv.end)
    return lhs;

  if (try_match(sv, "&&")) {
    sv.begin += 2;
    lhs.next = copy_to_heap(parse_pipe(trim(sv)));
    lhs.type = AND;
  }
  else if (try_match(sv, "||")) {
    sv.begin += 2;
    lhs.next = copy_to_heap(parse_pipe(trim(sv)));
    lhs.type = OR;
  }
  else if (try_match(sv, "|")) {
    sv.begin += 1;
    lhs.next = copy_to_heap(parse_pipe(trim(sv)));
    lhs.type = PIPE;
  }

  return lhs;
}

void print_indent(size_t indent)
{
  size_t i;
  for (i = 0; i < indent; ++i) putchar(' ');
}

void print_command(Command cmd, size_t indent)
{
  print_indent(indent);

  if (cmd.next != NULL) {
    switch (cmd.type) {
      case AND: puts("&&"); break;
      case OR:  puts("||"); break;
      case PIPE: puts("|"); break;
    }
    print_indent(indent += 2);
  }

  fwrite(cmd.value.begin, 1, strviewlen(cmd.value), stdout);
  putchar('\n');
  if (cmd.next == NULL)
    return;
  
  print_command(*cmd.next, indent);
}

void parse_command(StringView command)
{
  Command cmd = parse_pipe(command);
  print_command(cmd, 0);
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

    parse_command(command);
    free(command.begin);
  }

  return 0;
}
