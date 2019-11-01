#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>

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
  OR,
  SEMICOLON
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
  char *p;
  int escape_mode = false;
  int string_mode = false;

  for (p = sv->begin; p != sv->end; ++p) {
    if (!string_mode && !escape_mode)
      if (*p == ';' || *p == '|' || *p == '&')
        break;

    if (!escape_mode && *p == '"')
      string_mode = !string_mode;
    else if (!escape_mode && *p == '\\')
      escape_mode = true;
    else
      escape_mode = false;    
  }

  cmd.value.begin = sv->begin;
  sv->begin = cmd.value.end = p;

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
  else if (try_match(sv, ";")) {
    sv.begin += 1;
    lhs.next = copy_to_heap(parse_pipe(trim(sv)));
    lhs.type = SEMICOLON;
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
      case AND:       puts("&&"); break;
      case OR:        puts("||"); break;
      case PIPE:      puts("|");  break;
      case SEMICOLON: puts(";");  break;
    }
    print_indent(indent += 2);
  }

  fwrite(cmd.value.begin, 1, strviewlen(cmd.value), stdout);
  putchar('\n');
  if (cmd.next == NULL)
    return;
  
  print_command(*cmd.next, indent);
}


void destroy_command(Command cmd)
{
  Command *tmp, *c = cmd.next;
  while (c != NULL) {
    tmp = c->next;
    free(c);
    c = tmp;
  }
}

Command parse_command(StringView command)
{
  return parse_pipe(command);
}

#include "vector.h"

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
      current->end = cast(char*, path);
      prev = path + 1;
   }

  return path_dirs;
}

StringView find_word(StringView command)
{
  int escape_mode = false;
  int string_mode = false;
  
  char *p = command.begin;
  char *str_begin = NULL, *str_end = NULL;

  for (; p != command.end; ++p) {
    if (!escape_mode && *p == '"') {
      if (!string_mode) str_begin = p;
      else str_end = p;

      string_mode = !string_mode;
    }

    if ((!string_mode && !escape_mode) && *p == ' ')
      break;

    if (!escape_mode && *p == '\\')
      escape_mode = true;
    else
      escape_mode = false;
  }
  
  if (str_end+1 == p) {
    command.begin = str_begin + 1;
    command.end   = str_end;
  }
  else
    command.end = p;

  return command;
}

char* strview_to_cstr(StringView sv)
{
  char *str = malloc(strviewlen(sv) + 1);
  str[strviewlen(sv)] = '\0';
  memcpy(str, sv.begin, strviewlen(sv));
  fputs(str, stderr);
  return str;
}

int eval_simple_command(Command cmd, Vector path_dirs)
{
  pid_t pid;
  int status, i;
  char buffer[1024];
  StringView cd;
  struct stat sb;

  StringView word = trim(find_word(cmd.value));
  char *cmdname = strview_to_cstr(word);

  /* find command location */
  for (i = 0; i < path_dirs.size; ++i) {
    cd = vector(StringView, &path_dirs, i);
    memset(buffer, 0, sizeof(buffer) / sizeof(*buffer));

    strncpy(buffer, cd.begin, strviewlen(cd));
    buffer[strviewlen(cd)] = '/';
    buffer[strviewlen(cd) + 1] = '\0';
    strcat(buffer, cmdname);

    if (stat(buffer, &sb) == 0 && sb.st_mode & S_IXUSR)
      break;
  }

  if (i == path_dirs.size) {
    printf("Cannot find command ");
    puts(cmdname);
    return EXIT_FAILURE;
  }

  Vector args;
  vector(char*, &args, 0) = cmdname;

  if ((pid = fork()) == 0) { /* child */
    execv(buffer, (char**)args.data);
  }

  waitpid(pid, &status, 0);

  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return EXIT_FAILURE;
}

int main(int argc, char const* *argv)
{
  Command cmd;
  StringView input;
  int exit_code = 0;

  Vector path_dirs = parse_path_env(getenv("PATH"));

  for (;;) {
    print_evaluated_ps1(argv[0], false, exit_code);
    if (!(input = readline(stdin)).begin)
      break;
    
    input.end--;
    cmd = parse_command(input);
    exit_code = eval_simple_command(cmd, path_dirs);
    destroy_command(cmd);
    free(input.begin);
  }

  return 0;
}
