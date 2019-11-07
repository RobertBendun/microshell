#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>

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
  union sigval sigval;
  sigval.sival_int = argc >= 2 ? atoi(argv[1]) : 0;
  if (sigqueue(getppid(), SIGUSR1, sigval) < 0) {
    perror("exit: "); /* windows subsystem for linux prints Function not implemented */
    kill(getppid(), SIGUSR1);
  }
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

void execute_command(Command cmd, Vector path_dirs);


int extract_exit_code_from_status(int wait_status)
{
  return WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : EXIT_FAILURE;
}

void wait_for_child()
{
  wait(NULL);
}

int eval_pipe(Command cmd, Vector path_dirs, int not_fork)
{
  pid_t pid;
  int status;
  int pipefd[2];

  if (cmd.next == NULL) {
    simple_command: 
    if (not_fork || (pid = fork()) == 0)
      execute_command(cmd, path_dirs);
    else {
      wait(&status);
      return extract_exit_code_from_status(status);
    }
  }

  switch (cmd.type) {
    case None:
      goto simple_command;

    case PIPE:
      if (not_fork || (pid = fork()) == 0) {
        /* child process for lhs of pipe operator */
        pipe(pipefd);
        if (fork() == 0) {
          /* child process for rhs of pipe operator */
          close(0);
          dup(pipefd[0]);
          close(pipefd[0]);
          close(pipefd[1]);
          eval_pipe(*cmd.next, path_dirs, true);
        } else {
          close(1);
          dup(pipefd[1]);
          close(pipefd[0]);
          close(pipefd[1]);
          atexit(wait_for_child);
          execute_command(cmd, path_dirs);
        }
      } else {
        wait(&status);
        return extract_exit_code_from_status(status);
      }
      break;

    case AND:
    case OR:
    case SEMICOLON:
      if ((pid = fork()) == 0)
        execute_command(cmd, path_dirs);

      wait(&status);
      status = extract_exit_code_from_status(status);

      return cmd.type == SEMICOLON || (status == 0 ? cmd.type == AND : cmd.type == OR)
        ? eval_pipe(*cmd.next, path_dirs, false) 
        : status;
  }

  return EXIT_SUCCESS;
}

void execute_command(Command cmd, Vector path_dirs)
{
  char buffer[1024];
  char const *cmdname;
  StringView word, current_directory;
  struct stat s;
  int builtin = false; /* since !0 is true in C, no builtin is 0, builtin is (builtin_index + 1) */
  Vector args;
  size_t i;

  word = trim(find_word(cmd.value));
  cmdname = strview_to_cstr(word); /* @Incomplete: Check if cmdname is heap allocated and if so deallocate it */

  /* check if command is builtin */
  for (i = 0; i < sizeof(builtin_commands) / sizeof(*builtin_commands); ++i) {
    if (strcmp(cmdname, builtin_commands[i]) == 0) {
      builtin = i + 1;
      break;
    }
  }

  if (!builtin) {
    /* find command location */
    for (i = 0; i < path_dirs.size; ++i) {
      current_directory = vector(StringView, &path_dirs, i);
      fill(buffer, 0);
      strncpy(buffer, current_directory.begin, strviewlen(current_directory));
      memcpy(buffer + strviewlen(current_directory), "/", 2);
      strcat(buffer, cmdname);

      if (stat(buffer, &s) == 0 && s.st_mode & S_IXUSR)
        break;
    }

    if (i == path_dirs.size) {
      printf(BRIGHT_RED "microshell: command {" BRIGHT_WHITE "%s" BRIGHT_RED "} was not found.\n" COLOR_RESET, cmdname);
      return 0;
    }
  }

  fill(args, 0);
  vector(char*, &args, 0) = (char*)cmdname;

  if (word.end != cmd.value.end && *word.end == '"')
      word.end += 1;

  cmd.value.begin = word.end;
  cmd.value = trim(cmd.value);

  /* build arguments table */
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

  /* execute command or builtin with given args */
  if (builtin) {
    exit(builtin_commands_handlers[builtin-1](args.size, (char**)args.data));
  } else {
    execv(buffer, (char**)args.data);
  }
}

void handle_exit_signal(int sig, siginfo_t *si, void *ucontext)
{
  exit(si->si_value.sival_int);
}

int main(int argc, char const* *argv)
{
  Command cmd;
  StringView input;
  int exit_code = 0;

  Vector path_dirs = parse_path_env(getenv("PATH"));

  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = handle_exit_signal;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGUSR1, &sa, NULL);

 
  for (;;) {
    print_evaluated_ps1(argv[0], /* has root privilages  */ geteuid() == 0, exit_code);
    if (!(input = readline(stdin)).begin)
      break;
    
    input.end--;
    cmd = parse_command(input);
    exit_code = eval_pipe(cmd, path_dirs, false);
    destroy_command(cmd);
    free(input.begin);
  }

  return 0;
}
