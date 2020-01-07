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
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <setjmp.h>
#include <linux/limits.h>

#include "core.h"
#include "vector.h"
#include "terminal.h"
#include "StringView.h"
#include "allocators.h"

int run_command(StringView command);

int builtin_exit(int argc, char **argv);
int builtin_cd(int argc, char **argv);
int builtin_help(int argc, char **argv);

int builtin_history();
int builtin_goto(int argc, char **argv);
int builtin_history_clear();

int builtin_replace_part_of_command(int argc, char **argv);

int builtin_set(int argc, char **argv);
int builtin_save(int argc, char **argv);
int builtin_append(int argc, char **argv);

int builtin_ps1(int argc, char **argv);

char const *default_ps1 = "\\e[32;1m\\u\\e[0m[\\e[34m\\w\\e[0m]{\\!}\\P ";

typedef int(*Program)(int argc, char **argv);

Vector history;
Vector path_dirs;
InterprocessSharedMemoryAllocator isma;

struct GlobalState
{
  int exit_code;
  char cwd[PATH_MAX];
  int clear_history;
  char ps1[PATH_MAX];
} *globals;

static jmp_buf jump_buffer;

typedef struct
{
  StringView command;
  int exit_code;
} HistoryEntry;

struct {
  char const* name;
  char const* syntax;
  Program handler;
  char const* help;
} builtin_commands[] = {
  {
    "exit",
    "syntax: 'exit [exit_code]' if exit_code is not specified 0 is returned.",
    builtin_exit,
    "exit [N]\n"
    "  Exit the shell.\n"
    "\n"
    "  Exits the shell with a status of N. If n is omitted,\n"
    "  the exit status is defaulted to 0\n"
    "\n"
    "Known bug:\n"
    "  on Windows Subsystem For Linux sigaction is not implemented.\n"
    "  With current design, on WSL exit code is always 0, regardless N parameter"
  },
  {
    "cd",
    "syntax: 'cd [path]' changes current working directory to path",
    builtin_cd,
    "cd [path]\n"
    "  Changes current working directory to path.\n"
    "  if path is '" BOLD "~" COLOR_RESET "' changes cwd to user's home directory\n"
    "  if path is '" BOLD "-" COLOR_RESET "' reads actual path from stdin\n"
    "\n"
    BOLD "  Example:\n" COLOR_RESET
    "  du | cut -f2- | grep microshell | sort | cd -\n"
    "  comand above will find microshell folder and cd to it"
  },
  {
    "help",
    "syntax: 'help [builtin]' prints help about microshell or describes builtin command if specified",
    builtin_help,
    "help [builtin]\n"
    "  Displays information about builtin command.\n"
    "  If builtin is not specified help prints information about microshell"
  },
  {
    "history",
    "syntax: 'history' - displays history of commands",
    builtin_history,
    "  Displays history of inputed commands\n"
  },
  {
    "history-clear",
    "syntax: 'history-clear' - clears commands history",
    builtin_history_clear,
    ""
  },
  {
    "goto",
    "syntax: 'goto [n]' - executes n entry from history",
    builtin_goto,
    BOLD "goto" COLOR_RESET "[n]\n"
    "  executes n history entry. To make jump conditional use && and || operators.\n"
    "  To compose command with jump after it's execution use ; operator.\n"
    "\n"
    BOLD "  Example:\n" COLOR_RESET
    "    1: echo hello\n"
    "    2: false && goto 1\n"
    "    3: true && goto 1\n"
    "    4: false || goto 1\n"
    "    5: true || goto 1\n"
    "\n"
    "  Line 2 and 5 will print nothing and line 3 and 4 will print hello\n"
  },
  {
    "^",
    "syntax: '^ [text1] [text2]' - replaces first occurrence of text1 with text2 in previous command.",
    builtin_replace_part_of_command,
    BOLD "^" COLOR_RESET " [text1] [text2]\n"
    "  replaces first occurrence of text1 with text2 in previous command.\n"
  },
  {
    ">",
    "syntax: '> [filename]' - saves stdin in filename",
    builtin_save,
    BOLD ">" COLOR_RESET " [filename]\n"
    " saves stdin to filename."
    "\n\n"
    BOLD " Idiomatic use:\n" COLOR_RESET
    "   wc -l microshell.c |> statistics.txt"
  },
  {
    ">>",
    "syntax: '>> [filename]' - appends stdin to filename",
    builtin_append,
    BOLD ">>" COLOR_RESET " [filename]\n"
    " appends stdin to filename."
    "\n\n"
    BOLD " Idiomatic use:\n" COLOR_RESET
    "   wc -l microshell.c |>> statistics.txt"
  },
  {
    "ps1",
    "syntax: 'ps1 [string]' - sets string as default prompt",
    builtin_ps1,
    BOLD "ps1" COLOR_RESET " [string]\n"
    "  set prompt string to string\n"
    "  available escape sequences:\n"
    "    " BOLD "\\\\" COLOR_RESET " - print backslash\n"
    "    " BOLD "\\e" COLOR_RESET " - escape character useful for ansi color sequences\n"
    "    " BOLD "\\n" COLOR_RESET " - newline\n"
    "    " BOLD "\\r" COLOR_RESET " - carrige return\n"
    "    " BOLD "\\a" COLOR_RESET " - bell character\n"
    "    " BOLD "\\s" COLOR_RESET " - shell exec name\n"
    "    " BOLD "\\$" COLOR_RESET " - if user has root privilages prints #, otherwise $\n"
    "    " BOLD "\\!" COLOR_RESET " - prints current history entry number\n"
    "    " BOLD "\\h, \\H" COLOR_RESET " - print hostname\n"
    "    " BOLD "\\l" COLOR_RESET " - name of terminal shell device\n"
    "    " BOLD "\\u" COLOR_RESET " - username\n"
    "    " BOLD "\\w" COLOR_RESET " - current working directory\n"
    "    " BOLD "\\W" COLOR_RESET " - basename of current working directory\n"
    "    " BOLD "\\P" COLOR_RESET " - same as \\$ but with color encoded last program result"
  }
};

static void print_help_to_command(int(*handler)(int, char**))
{
  unsigned i;
  for (i = 0; i < arraylen(builtin_commands); ++i)
    if (builtin_commands[i].handler == handler) {
      puts(builtin_commands[i].syntax);
      return;
    }

  puts("---- invalid handler ----");
}

int builtin_exit(int argc, char **argv)
{
  int ec = argc >= 2 ? atoi(argv[1]) : EXIT_SUCCESS;
  globals->exit_code = ec;
  kill(getppid(), SIGUSR1);
  return EXIT_SUCCESS;
}

int builtin_cd(int argc, char **argv)
{
  char buffer[PATH_MAX], input_buffer[PATH_MAX];
  struct stat s;
  char const *input;
  char *end;

  if (strcmp(argv[1], "-") == 0) {
    if (fgets(input_buffer, PATH_MAX, stdin))
      if ((end = strchr(input_buffer, '\n')) != NULL)
        *end = '\0';
      else if (end == NULL) {
        goto to_many_characters;
      }  
    input = input_buffer;
  }
  else
    input = argv[1];

  if (strlen(input) > PATH_MAX) {
    to_many_characters:
    fprintf(stderr, "cd: path must be at most %u bytes!\n", (unsigned) PATH_MAX);
    return 1;
  }

  if (strcmp(input, "~") == 0) {
    strcpy(globals->cwd, ((struct passwd *)getpwuid(getuid()))->pw_dir);
    return EXIT_SUCCESS;
  }

  if (realpath(input, buffer) == NULL && stat(buffer, &s) != 0) {
    fprintf(stderr, BRIGHT_RED "No such file or directory.\n" COLOR_RESET);
    return EXIT_FAILURE;
  }

  if (chdir(buffer) != 0) {
    fprintf(stderr, BRIGHT_RED "Can't go to cd\n" COLOR_RESET);
    return EXIT_FAILURE;
  }

  strcpy(globals->cwd, buffer);
  return EXIT_SUCCESS;
}

int builtin_help(int argc, char **argv)
{
  size_t i;

  if (argc == 1) {
    printf(
      "  _____  ____  __  __  _____ "   "   " "" "\n"
      " |  __ \\|  _ \\|  \\/  |/ ____|""   " "microshell by Robert Bendun" "\n"
      " | |__) | |_) | \\  / | (___  "  "   " "features:" "\n"
      " |  _  /|  _ <| |\\/| |\\___ \\ ""   " "  - powerful builtin commands" "\n"
      " | | \\ \\| |_) | |  | |____) |" "   " "  - |, || and && operators" "\n"
      " |_|  \\_\\____/|_|  |_|_____/ " "   " "  - PS1 format support for nice prompt" "\n"
      "\n"
    );

    printf("list of builtins: \n");
    for (i = 0; i < arraylen(builtin_commands); ++i)
      printf(" * %s\n", builtin_commands[i].name);

    puts("To get more information type: 'help [builtin] e.g. 'help ^' or 'help exit'");
    return EXIT_SUCCESS;
  } 

  for (i = 0; i < arraylen(builtin_commands); ++i)
    if (strcmp(builtin_commands[i].name, argv[1]) == 0) {
      puts(builtin_commands[i].help);
      return EXIT_SUCCESS;
    }

  printf(
    BRIGHT_WHITE "%s"
    BRIGHT_RED " does not have help page or is not a builtin command\n" COLOR_RESET, argv[1]);

  return EXIT_FAILURE;
}

int builtin_history()
{
  size_t i;
  StringView sv;

  for (i = 0; i < history.size; ++i) {
    printf("%5ld ", i + 1);
    sv = (vector(HistoryEntry, &history, i)).command;
    fwrite(sv.begin, 1, strviewlen(sv), stdout);
    puts("");
  }

  return EXIT_SUCCESS;
}

int builtin_goto(int argc, char **argv)
{
  size_t i;
  int n;

  if (argc == 1) {
    print_help_to_command(builtin_goto);
    return EXIT_FAILURE;
  }
  
  n = atoi(argv[1]) - 1;

  for (i = 0; i < history.size; ++i) {
    if (i == (size_t) n)
      return run_command((vector(HistoryEntry, &history, i)).command);
  }

  printf(BRIGHT_WHITE "goto: " RED "cannot find history entry at index: %d\n", n + 1);
  return EXIT_FAILURE;
}

int builtin_history_clear()
{
  globals->clear_history = 1;
  return EXIT_SUCCESS;
}

int builtin_set(int argc, char **argv)
{
  return -1 * setenv(argv[1], argv[2], true);
}

static int stdin_to_file(char const* filename, char const *mode, char const *message)
{
  FILE *f;
  char buffer[BUFSIZ * 64];
  size_t r;

  if ((f = fopen(filename, mode)) == NULL) {
    perror(message);
    return EXIT_FAILURE;
  }

  while ((r = fread(buffer, 1, arraylen(buffer), stdin)))
    fwrite(buffer, 1, r, f);

  if (ferror(f)) {
    fclose(f);
    return EXIT_FAILURE;
  }
  fclose(f);
  return EXIT_SUCCESS;
}

/* @Incomplate: no handling for empty argv[1] */
int builtin_save(int argc, char **argv)
{
  if (argc == 1) {
    print_help_to_command(builtin_save);
    return EXIT_FAILURE;
  }
  return stdin_to_file(argv[1], "w", ">");
}

/* @Incomplate: no handling for empty argv[1] */
int builtin_append(int argc, char **argv)
{
  if (argc == 1) {
    print_help_to_command(builtin_append);
    return EXIT_FAILURE;
  }
  return stdin_to_file(argv[1], "a", ">>");
}

int builtin_ps1(int argc, char **argv)
{
  strcpy(globals->ps1, argv[1]);
  return EXIT_SUCCESS;
}

void wait_for_child();

int builtin_replace_part_of_command(int argc, char **argv)
{
  StringView sv;
  Vector new;
  char *str, *match;
  size_t i;

  if (argc == 1) {
    print_help_to_command(builtin_replace_part_of_command);
    return EXIT_FAILURE;
  }

  if (history.size == 0) {
    fprintf(stderr, BRIGHT_WHITE "microshell: "
      RED " cannot replace text in previous command if you don't have previous command"
      COLOR_RESET);

    return EXIT_FAILURE;
  }

  sv = (vector(HistoryEntry, &history, history.size - 1)).command;
  str = strview_to_cstr(sv);
  
  if ((match = strstr(str, argv[1])) == NULL) {
    printf("not mached\n");
    return EXIT_FAILURE;
  }
  
  fill(new, 0);
  vector_reserve(char, &new, strviewlen(sv));

  for (i = 0; i < (size_t)(match - str); ++i)
    vector(char, &new, i) = str[i];

  for (i = 0; i < strlen(argv[2]); ++i)
    vector(char, &new, new.size) = argv[2][i];
  
  match += strlen(argv[1]);
  for (; *match != '\0'; ++match)
    vector(char, &new, new.size) = *match;

  vector(char, &new, new.size) = '\0';

  sv.begin = new.data;
  sv.end   = new.data + new.size;

  atexit(wait_for_child);
  return run_command(sv);
}

void print_evaluated_ps1(char const *shell_exec_name, int has_root_privilages, int last_command_result)
{
  int escape_next = false;
  char buffer[BUFSIZ];
  char const *ps1 = globals->ps1;
  char const *path;
  

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
        case '!': printf("%lu", history.size + 1); break;

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
          if (strstr(globals->cwd, (path = ((struct passwd *)getpwuid(getuid()))->pw_dir)) == globals->cwd)
            printf("~%s", globals->cwd + strlen(path));
          else
            fputs(globals->cwd, stdout);
          break;

        case 'W': /* basename of current working directory */
          fputs(basename(getcwd(buffer, BUFSIZ)), stdout);
          break;

        case 'P': /* custom: color highlighet $ or # depending on result of previous command */
          printf("%s%c" COLOR_RESET,
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
    if (!string_mode && !escape_mode) {
      if (*p == ';' || *p == '|' || *p == '&')
        break;
      else if (*p == '#') {
        cmd.value.begin = sv->begin;
        cmd.value.end = p;
        sv->begin = sv->end;
        return cmd;
      }
    }

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
      case None: assert(false); break;
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
  Vector      dirs;
  StringView *current;
  char const *prev = path;

  fill(dirs, 0);

  for (; *path != '\0'; ++path)
    if (*path == ':') {
      current = &vector(StringView, &dirs, dirs.size);
      current->begin = cast(char*, prev);
      current->end = cast(char*, path);
      prev = path + 1;
   }

  return dirs;
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

void execute_command(Command cmd);

int extract_exit_code_from_status(int wait_status)
{
  return WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : EXIT_FAILURE;
}

void wait_for_child()
{
  wait(NULL);
}

Vector build_args(Command cmd, char const* cmdname, StringView word)
{
  int i;
  Vector args;
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

  return args;
}

int eval_pipe(Command cmd, int not_fork)
{
  pid_t pid;
  int status;
  int pipefd[2];
  MAYBE_UNUSED StringView sv;

  if (cmd.next == NULL) {
    simple_command:
    sv = find_word(cmd.value);
    if (not_fork || (pid = fork()) == 0)
      execute_command(cmd);
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
        if (pipe(pipefd) < 0){
          fprintf(stderr, "Cannot open pipe to handle processes.\n");
          exit(EXIT_FAILURE);
        }

        if (fork() == 0) {
          /* child process for rhs of pipe operator */
          close(0);
          assert(dup(pipefd[0]) >= 0);
          close(pipefd[0]);
          close(pipefd[1]);
          atexit(wait_for_child);
          eval_pipe(*cmd.next, true);
        } else {
          close(1);
          assert(dup(pipefd[1]) >= 0);
          close(pipefd[0]);
          close(pipefd[1]);
          atexit(wait_for_child);
          execute_command(cmd);
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
        execute_command(cmd);

      wait(&status);
      status = extract_exit_code_from_status(status);

      return cmd.type == SEMICOLON || (status == 0 ? cmd.type == AND : cmd.type == OR)
        ? eval_pipe(*cmd.next, false)
        : status;
  }

  return EXIT_SUCCESS;
}

void execute_command(Command cmd)
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

  if (strchr(cmdname, '/') != NULL) {
    strcpy(buffer, cmdname);
    goto exec;
  }

  /* check if command is builtin */
  for (i = 0; i < sizeof(builtin_commands) / sizeof(*builtin_commands); ++i) {
    if (strcmp(cmdname, builtin_commands[i].name) == 0) {
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
      exit(EXIT_FAILURE);
    }
  }

  exec:

  args = build_args(cmd, cmdname, word);
  
  /* execute command or builtin with given args */
  if (builtin) {
    exit(builtin_commands[builtin-1].handler(args.size, (char**)args.data));
  } else {
    execv(buffer, (char**)args.data);
  }
}

void handle_exit()
{
  exit(globals->exit_code);
}

int run_command(StringView command)
{
  int ec = EXIT_FAILURE;
  Command cmd = parse_command(command);
  if (cmd.value.begin != cmd.value.end)
    ec = eval_pipe(cmd, false);
  destroy_command(cmd);
  return ec;
}

void clear_history()
{
  size_t i;
  for (i = 0; i < history.size; ++i)
    free((vector(HistoryEntry, &history, i)).command.begin);
  vector_destroy(&history);
  fill(history, 0);
}

void pass()
{
  longjmp(jump_buffer, 1);
}

void set_ps1()
{
  char const *ps1;
  ps1 = getenv("PS1");
  if (!ps1) 
    ps1 = default_ps1;
  strcpy(globals->ps1, ps1);
}

void clear_interprocess_memory()
{
  interprocess_shared_memory_allocator(&isma, 0, globals);
}

int main(int argc, char const* *argv)
{
  HistoryEntry history_entry;
  StringView input;

  memset(&isma, 0, sizeof(isma));
  fill(history, 0);
  path_dirs = parse_path_env(getenv("PATH"));

  if (signal(SIGUSR1, handle_exit) == SIG_ERR)
    fprintf(stderr, "microshell: cannot register signal for exit command.\n" BRIGHT_RED "That makes exit command useless\n" COLOR_RESET);
  
  globals = interprocess_shared_memory_allocator(&isma, sizeof(struct GlobalState), NULL);
  
  while (getcwd(globals->cwd, sizeof(globals->cwd)) == NULL)
    ;

  signal(SIGINT, pass);

  set_ps1();
  atexit(clear_interprocess_memory);

  if (setjmp(jump_buffer) != 0) {
    setjmp(jump_buffer);
    puts("");
  }

  for (;;) {
    while (wait(NULL) > 0)
      ;

    if (globals->clear_history) {
      clear_history();
      globals->clear_history = false;
    }
    
    if (chdir(globals->cwd) < 0)
      while (getcwd(globals->cwd, sizeof(globals->cwd)) == NULL)
        ;

    print_evaluated_ps1(argv[0], /* has root privilages  */ geteuid() == 0, globals->exit_code);
    if (!(input = readline(stdin)).begin)
      break;

    input.end--;
    if (input.begin == input.end)
      continue;

    globals->exit_code = run_command(input);
    
    history_entry.command = input;
    history_entry.exit_code = globals->exit_code;
    vector(HistoryEntry, &history, history.size) = history_entry;
  }

  return EXIT_SUCCESS;
}
