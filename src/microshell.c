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
#include "vector.h"
#include "terminal.h"
#include "StringView.h"

int builtin_exit(int argc, char **argv);
int builtin_cd(int argc, char **argv);

char const *default_ps1 = "\\e[32;1m\\u@\\h\\e[0m[\\e[34m\\w\\e[0m] \\P ";

typedef int(*Program)(int argc, char **argv);

char const* const builtin_commands[] = {
  "exit",
  "cd"
};

const Program builtin_commands_handlers[] = {
  builtin_exit,
  builtin_cd
};

int builtin_exit(int argc, char **argv)
{
  if (argc > 1)
    exit(atoi(argv[1]));

  exit(EXIT_SUCCESS);
  return EXIT_SUCCESS;
}

int builtin_cd(int argc, char **argv)
{
  return chdir(argv[1]) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

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

enum PipeType
{
  None,
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

int execute_command(Command cmd, Vector path_dirs, int pipefd[2], int mode, pid_t *pid);

#define READ 0x2
#define WRITE 0x1

int eval_pipe(Command cmd, Vector path_dirs)
{
  int exit_code;
  pid_t p1, p2;
  int status;
  int pipefd[2];

  if (cmd.next == NULL)
    simple_command: return execute_command(cmd, path_dirs, pipefd, 0, NULL);
  
  switch (cmd.type) {
    case PIPE:
    {
      if (!cmd.next) {
        fprintf(stderr, "Internal bug: pipe should have next command.\n");
        exit(EXIT_FAILURE);
      }

      if (pipe(pipefd) < 0) {
        perror("microshell: pipe:");
        exit(EXIT_FAILURE);
      }
      execute_command(cmd, path_dirs, pipefd, WRITE, &p1);
      exit_code = execute_command(*cmd.next, path_dirs, pipefd, READ, &p2);
      wait(NULL);
      close(pipefd[0]);
      close(pipefd[1]);
      return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_SUCCESS;
    }
    break;

    case AND:
      exit_code = execute_command(cmd, path_dirs, pipefd, 0, NULL);
      return exit_code == EXIT_SUCCESS 
        ? eval_pipe(*cmd.next, path_dirs)
        : exit_code;
    
    case OR:
      exit_code = execute_command(cmd, path_dirs, pipefd, 0, NULL);
      return exit_code != EXIT_SUCCESS 
        ? eval_pipe(*cmd.next, path_dirs)
        : exit_code;

    case SEMICOLON:
      (void) execute_command(cmd, path_dirs, pipefd, 0, NULL);
      return eval_pipe(*cmd.next, path_dirs);
    
    case None:
      goto simple_command;
  }

  return EXIT_SUCCESS;
}

int execute_command(Command cmd, Vector path_dirs, int pipefd[2], int mode, pid_t *pid_ptr)
{
  pid_t pid;
  int status;
  size_t i;
  char buffer[1024];
  StringView cd;
  struct stat sb;
  int builtin = false;

  Vector args;
  StringView word = trim(find_word(cmd.value));
  char *cmdname = strview_to_cstr(word);

  /* check if is builtin */
  for (i = 0; i < sizeof(builtin_commands) / sizeof(*builtin_commands); ++i) {
    if (strcmp(cmdname, builtin_commands[i]) == 0) {
      /* store builtin index as (index + 1) - if we have not found index builtin will evaluate to 0 - wich means false */
      builtin = i + 1; 
      break;
    }
  }

  /* find command location */
  if (!builtin) {
    for (i = 0; i < path_dirs.size; ++i) {
      cd = vector(StringView, &path_dirs, i);
      memset(buffer, 0, sizeof(buffer) / sizeof(*buffer));

      /* string from PATH + command name construction */
      strncpy(buffer, cd.begin, strviewlen(cd));
      buffer[strviewlen(cd)] = '/';
      buffer[strviewlen(cd) + 1] = '\0';
      strcat(buffer, cmdname);

      if (stat(buffer, &sb) == 0 && sb.st_mode & S_IXUSR)
        break;
    }

    if (i == path_dirs.size) {
      printf(BRIGHT_RED "microshell: command {" BRIGHT_WHITE "%s" BRIGHT_RED "} was not found.\n" COLOR_RESET, cmdname);
      free(cmdname);
      return EXIT_FAILURE;
    }
  }

  fill(args, 0);
  vector(char*, &args, 0) = cmdname;
  if (word.end != cmd.value.end && *word.end == '"')
      word.end += 1;

  cmd.value.begin = word.end;
  cmd.value = trim(cmd.value);

  for (i = 1; word.end != cmd.value.end; ++i) {
    word = find_word(cmd.value);
    if (!strviewlen(trim(word)))
      break;

    vector(char*, &args, i) = strview_to_cstr(word);
    if (word.end != cmd.value.end && *word.end == '"')
      word.end += 1;

    cmd.value.begin = word.end;
    cmd.value = trim(cmd.value);
  }

  if (builtin) {
    int exit_code = builtin_commands_handlers[builtin-1](args.size, (char**)args.data);
    vector_destroy(&args);
    return exit_code;
  } else {
    if ((pid = fork()) == 0) { /* child */
      switch (mode) {
        case READ:
          close(STDOUT_FILENO);
          close(pipefd[1]);
          dup2(pipefd[0], STDOUT_FILENO);
          close(pipefd[0]);
          break;

        case WRITE:
          close(STDIN_FILENO);
          close(pipefd[0]);
          dup2(pipefd[1], STDIN_FILENO);
          close(pipefd[1]);
          break;

        default:
          break;
      }

      execv(buffer, (char**)args.data);
    }

    if (pid_ptr == NULL) {
      waitpid(pid, &status, 0);
      return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
    }
    else {
      *pid_ptr = pid;
      return EXIT_SUCCESS;
    }
  }
}

int main(int argc, char const* *argv)
{
  Command cmd;
  StringView input;
  int exit_code = 0;

  Vector path_dirs = parse_path_env(getenv("PATH"));

  for (;;) {
    print_evaluated_ps1(argv[0], /* has root privilages  */ geteuid() == 0, exit_code);
    if (!(input = readline(stdin)).begin)
      break;
    
    input.end--;
    cmd = parse_command(input);
    exit_code = eval_pipe(cmd, path_dirs);
    destroy_command(cmd);
    free(input.begin);
  }

  return 0;
}
