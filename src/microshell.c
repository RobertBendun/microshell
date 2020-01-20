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
#include "map.h"

int run_command(StringView command);

int builtin_exit(int argc, char **argv);
int builtin_cd(int argc, char **argv);
int builtin_help(int argc, char **argv);

int builtin_history();
int builtin_goto(int argc, char **argv);
int builtin_history_clear();
int builtin_history_load(int argc, char **argv);
int builtin_history_save(int argc, char **argv);

int builtin_replace_part_of_command(int argc, char **argv);

int builtin_save(int argc, char **argv);
int builtin_append(int argc, char **argv);

int builtin_ps1(int argc, char **argv);

int builtin_add_history_entry(int argc, char **argv);

int builtin_var(int argc, char **argv);


void set_env_if_present();

char const *default_ps1 = "\\e[32;1m\\u\\e[0m[\\e[34;1m\\w\\e[0m]{\\!}\\P ";

typedef int(*Program)(int argc, char **argv);

Vector history;
Vector path_dirs;
InterprocessSharedMemoryAllocator isma;
int is_child = 0;
int history_index = 0;

pid_t marked_fork()
{
  pid_t v = fork();
  if (!is_child) is_child = v == 0;
  if (is_child) {
    set_env_if_present();
    signal(SIGINT, SIG_DFL);
  }
  return v;
}

enum VariableCommand
{
  NoVariableCommand,
  SetVariable,
  MathOperation
};

enum HistoryCommand
{
  NoHistoryCommand,
  ClearHistory,
  LoadHistory,
  SetHistoryIndex
};

struct GlobalState
{
  int exit_code;
  char cwd[PATH_MAX];
  enum HistoryCommand history_command;
  char ps1[PATH_MAX];
  char text[PATH_MAX];
  char optional_text[PATH_MAX];
  size_t new_history_index;
  enum VariableCommand variableCommand;
  char ops[3];
} *globals;

static sigjmp_buf jump_env;

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
  char const* more_help;
} builtin_commands[] = {
  {
    "exit",
    "syntax: 'exit [exit_code]' if exit_code is not specified 0 is returned.",
    builtin_exit,
    BOLD "exit" COLOR_RESET " [N]\n"
    "  Exit the shell.\n"
    "\n"
    "  Exits the shell with a status of N. If n is omitted,\n"
    "  the exit status is defaulted to 0",
    ""
  },
  {
    "cd",
    "syntax: 'cd [path]' changes current working directory to path",
    builtin_cd,
    BOLD "cd" COLOR_RESET " [path]\n"
    "  Changes current working directory to path.\n"
    "  If path is '" BOLD "~" COLOR_RESET "' changes cwd to user's home directory.\n"
    "  If path is '" BOLD "-" COLOR_RESET "' reads actual path from stdin.\n"
    "\n"
    BOLD "  Example:\n" COLOR_RESET
    "  du | cut -f2- | grep microshell | sort | cd -\n"
    "  comand above will find microshell folder and cd to it",
    ""
  },
  {
    "help",
    "syntax: 'help [builtin]' prints help about microshell or describes builtin command if specified",
    builtin_help,
    BOLD "help" COLOR_RESET " [builtin]\n"
    "  Displays information about builtin command.\n"
    "  If builtin is not specified help prints information about microshell.",
    ""
  },
  {
    "history",
    "syntax: 'history' - displays history of commands",
    builtin_history,
    BOLD "history" COLOR_RESET
    "  Useful utility with scripting potential (Commodore64 BASIC style).\n"
    "  Displays history of inputed or added commands in increasing order",
    ""
  },
  {
    "history-clear",
    "syntax: 'history-clear' - clears commands history",
    builtin_history_clear,
    BOLD "history-clear \n" COLOR_RESET
    "  Removes history content leaving nothing behind.\n"
    "  Next command will have index 1 in history.",
    ""
  },
  {
    "history-load",
    "syntax: 'history-load [filename] - loads history from file\n",
    builtin_history_load,
    BOLD "history-load" COLOR_RESET,
    ""
  },
  {
    "history-save",
    "syntax: 'history-save [filename] - saves commands history to filename",
    builtin_history_save,
    BOLD "history-save" COLOR_RESET " [filename]\n"
    "  Saves history to file specified by filename.",
    ""
  },
  {
    "goto",
    "syntax: 'goto [n]' - executes n entry from history",
    builtin_goto,
    BOLD "goto" COLOR_RESET "[n]\n"
    "  executes n history entry.\n"
    "  To make jump conditional use && and || operators.\n"
    "  To compose command with jump after it's execution use ; operator.\n"
    "\n"
    BOLD "  Example:\n" COLOR_RESET
    "    1: echo hello\n"
    "    2: false && goto 1\n"
    "    3: true && goto 1\n"
    "    4: false || goto 1\n"
    "    5: true || goto 1\n"
    "\n"
    "  Line 2 and 5 will print nothing and line 3 and 4 will print hello",
    ""
  },
  {
    "^",
    "syntax: '^ [text1] [text2]' - replaces first occurrence of text1 with text2 in previous command.",
    builtin_replace_part_of_command,
    BOLD "^" COLOR_RESET " [text1] [text2]\n"
    "  replaces first occurrence of text1 with text2 in previous command.",
    ""
  },
  {
    ">",
    "syntax: '> [filename]' - saves stdin in filename",
    builtin_save,
    BOLD ">" COLOR_RESET " [filename]\n"
    "  saves stdin to filename."
    "\n\n"
    BOLD " Idiomatic use:\n" COLOR_RESET
    "   wc -l microshell.c |> statistics.txt",
    ""
  },
  {
    ">>",
    "syntax: '>> [filename]' - appends stdin to filename",
    builtin_append,
    BOLD ">>" COLOR_RESET " [filename]\n"
    "  appends stdin to filename."
    "\n\n"
    BOLD " Idiomatic use:\n" COLOR_RESET
    "   wc -l microshell.c |>> statistics.txt",
    ""
  },
  {
    "ps1",
    "syntax: 'ps1 [string]' - sets string as default prompt",
    builtin_ps1,
    BOLD "ps1" COLOR_RESET " [string]\n"
    "  set prompt string to string.\n"
    "  If string is not specified uses default ps1.\n"
    "  Available escape sequences:\n"
    "    " BOLD "\\\\" COLOR_RESET " - print backslash\n"
    "    " BOLD "\\e" COLOR_RESET " - escape character useful for ansi color sequences\n"
    "    " BOLD "\\n" COLOR_RESET " - newline\n"
    "    " BOLD "\\r" COLOR_RESET " - carrige return\n"
    "    " BOLD "\\a" COLOR_RESET " - bell character\n"
    "    " BOLD "\\s" COLOR_RESET " - shell exec name\n",
    "    " BOLD "\\$" COLOR_RESET " - if user has root privilages prints #, otherwise $\n"
    "    " BOLD "\\!" COLOR_RESET " - prints current history entry number\n"
    "    " BOLD "\\h, \\H" COLOR_RESET " - print hostname\n"
    "    " BOLD "\\l" COLOR_RESET " - name of terminal shell device\n"
    "    " BOLD "\\u" COLOR_RESET " - username\n"
    "    " BOLD "\\w" COLOR_RESET " - current working directory\n"
    "    " BOLD "\\W" COLOR_RESET " - basename of current working directory\n"
    "    " BOLD "\\P" COLOR_RESET " - same as \\$ but with color encoded last program result"
  },
  {
    ":",
    "syntax: ': [index] [command]",
    builtin_add_history_entry,
    BOLD ": [index] [command]" COLOR_RESET "\n"
    "  sets history entry at index to command\n"
    "  if command is not specified, command will be read from stdin."
  },
  {
    "var",
    "syntax: 'var [command] [param1] [param2]'",
    builtin_var,
    BOLD "var" COLOR_RESET "[command] [param1] [param2]\n"
    "  general utility for enviromental variable manipulation.\n"
    "  Math and relational operators are only defined for int32.\n"
    "  Strings are NOT supported.\n\n"
    "  available commands:\n"
    "  - get - prints variable defined by param1\n"
    "  - set - sets variable defined by param1 to either param2 if is specified\n"
    "          or to line from stdin\n"
    "  - {math} - prints result of math binary operation where lhs is param1\n"
    "          and rhs is param2. If param1 or param2 begins with digit, \n",
    "          parameter is treated as integer literal. Otherwise parameter\n"
    "          is resolved to environment variable.\n"
    "          Available operators: +, -, *, /, %, and, or, xor\n"
    "  - {rel} - returns from program comparison result (0 is true, 1 is false).\n"
    "          Treats parameters like math operators described above.\n"
    "          Available operators: ==, !=, >, <, >=, <=\n\n"
    "  Example:\n"
    "  1: var set a 10\n"
    "  2: var + a 1 | var set a\n"
    "  Line 1 will set variable a to 10 and line 2 will increment variable a.\n"
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

  if (argc == 1)
    goto go_home;

  if (strcmp(argv[1], "-") != 0) {
    input = argv[1];
  }
  else {
    if (!fgets(input_buffer, PATH_MAX, stdin)) {
      fprintf(stderr, BRIGHT_RED "Error reading path from stdin. Parhaps input is empty?" COLOR_RESET);
      return 1;
    }

    if ((end = strchr(input_buffer, '\n')) == NULL)
      goto to_many_characters;
    *end = '\0';
    input = input_buffer;
  }

  if (strlen(input) > PATH_MAX) {
    to_many_characters:
    fprintf(stderr, BRIGHT_RED "Path must be at most %u bytes!\n" COLOR_RESET, (unsigned) PATH_MAX);
    return 1;
  }

  if (strcmp(input, "~") == 0) {
    go_home:
    strcpy(globals->cwd, ((struct passwd *) getpwuid(getuid()))->pw_dir);
    return EXIT_SUCCESS;
  }

  if (realpath(input, buffer) == NULL && stat(buffer, &s) != 0) {
    fprintf(stderr, BRIGHT_RED "No such file or directory.\n" COLOR_RESET);
    return EXIT_FAILURE;
  }

  if (chdir(buffer) != 0) {
    fprintf(stderr, BRIGHT_RED "No such file or directory.\n" COLOR_RESET);
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
      " |  __ \\|  _ \\|  \\/  |/ ____|""   " "microshell 1.1.0 by Robert Bendun" "\n"
      " | |__) | |_) | \\  / | (___  "  "   " "  - CTRL-C signal handling" "\n"
      " |  _  /|  _ <| |\\/| |\\___ \\ ""   " "  - powerful builtin commands" "\n"
      " | | \\ \\| |_) | |  | |____) |" "   " "  - |, ;, || and && operators" "\n"
      " |_|  \\_\\____/|_|  |_|_____/ " "   " "  - PS1 format support for nice prompt" "\n"
      "                               "   "   " "- string quoting\n"
    );

    printf("list of builtins: \n");
    for (i = 0; i < arraylen(builtin_commands); ++i)
      printf(" * %s\n", builtin_commands[i].name);

    puts("To get more information type: 'help [builtin] e.g. 'help ^' or 'help exit'");
    return EXIT_SUCCESS;
  } 

  for (i = 0; i < arraylen(builtin_commands); ++i)
    if (strcmp(builtin_commands[i].name, argv[1]) == 0) {
      printf("%s%s\n", builtin_commands[i].help, builtin_commands[i].more_help);
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
  ptrdiff_t *kv;

  for (i = 0; i < veclen(history); i += 2) {
    kv = &vector(ptrdiff_t, &history, i);
    printf("%4ld\t%s\n", fst(kv), (char*)snd(kv));
  }

  return EXIT_SUCCESS;
}

int builtin_goto(int argc, char **argv)
{
  size_t i;
  int n;
  ptrdiff_t *kv;
  StringView sv;

  if (argc == 1) {
    print_help_to_command(builtin_goto);
    return EXIT_FAILURE;
  }
  
  n = atoi(argv[1]);

  kv = map_search(&history, n);
  if (kv != NULL) {
    sv.begin = snd(kv);
    sv.end = sv.begin + strlen(sv.begin);
    return run_command(sv);
  }
  printf(BRIGHT_WHITE "goto: " BRIGHT_RED "cannot find history entry at index: " BRIGHT_WHITE "%d\n" COLOR_RESET, n);
  return EXIT_FAILURE;
}

int builtin_history_clear()
{
  globals->history_command = ClearHistory;
  return EXIT_SUCCESS;
}

int builtin_history_load(int argc, char **argv)
{
  globals->history_command = LoadHistory;
  strcpy(globals->text, argv[1]);
  return EXIT_SUCCESS;
}

int builtin_history_save(int argc, char **argv)
{
  size_t i;
  ptrdiff_t *kv;
  FILE *f;

  if (argc == 1) {
    print_help_to_command(builtin_history_save);
    return EXIT_FAILURE;
  }

  if ((f = fopen(argv[1], "w")) == NULL) {
    perror("history-save");
    return EXIT_FAILURE;
  }

  fprintf(f, "%ld\n", veclen(history) / 2);

  for (i = 0; i < veclen(history); i += 2) {
    kv = &vector(ptrdiff_t, &history, i);
    fprintf(f, "%ld %s\n", kv[0], (char const*) kv[1]);
  }
  fclose(f);
  return EXIT_SUCCESS;
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
  if (argc == 1)
    strcpy(globals->ps1, default_ps1);
  else
    strcpy(globals->ps1, argv[1]);
  return EXIT_SUCCESS;
}

int builtin_add_history_entry(int argc, char **argv)
{
  char buffer[1024];
  char const *input;

  if (argc == 1) {
    print_help_to_command(builtin_add_history_entry);
    return EXIT_FAILURE;
  }

  if (argc == 2) {
    fgets(buffer, 1024, stdin);
    *strchr(buffer, '\n') = '\0';
    input = buffer;
  }
  else
    input = argv[2];

  strcpy(globals->text, input);
  globals->new_history_index = atoi(argv[1]);
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

int literal_or_env(char const *str)
{
  if (str[0] >= '0' && str[0] <= '9')
    return atoi(str);
  return atoi(getenv(str));
}

int builtin_var(int argc, char **argv)
{
  StringView sv;
  size_t i;
  int r = 0;
  char const *command, *s;

  if (argc < 3) {
    print_help: print_help_to_command(builtin_var);
    return EXIT_FAILURE;
  }

  command = argv[1];

  if (strcmp(command, "get") == 0) {
    if ((s = getenv(argv[2])) == NULL)
      return EXIT_FAILURE;

    printf("%s\n", s);
    return EXIT_SUCCESS;
  }
  
  if (strcmp(command, "set") == 0) {
    globals->variableCommand = SetVariable;
    strcpy(globals->optional_text, argv[2]);
    if (argc <= 3) {
      sv = readline(stdin);
      strncpy(globals->text, sv.begin, sv.end - sv.begin + 1);
      setenv(globals->optional_text, globals->text, 1);
      return EXIT_SUCCESS;
    }
    strcpy(globals->text, argv[3]);
    setenv(globals->optional_text, globals->text, 1);
    return EXIT_SUCCESS;
  }

  if (argc < 4)
    goto print_help;

#define MATH_OP(O, X) \
  if (strcmp(command, O) == 0) { \
    printf("%d\n", literal_or_env(argv[2]) X literal_or_env(argv[3])); \
    return EXIT_SUCCESS; \
  }

#define REL_OP(O, X) \
  if (strcmp(command, O) == 0) \
    return !(literal_or_env(argv[2]) X literal_or_env(argv[3]));

  MATH_OP("+", +);
  MATH_OP("-", -);
  MATH_OP("*", *);
  MATH_OP("/", /);
  MATH_OP("%", %);
  MATH_OP("and", &);
  MATH_OP("or", |);
  MATH_OP("xor", ^);

  REL_OP("==", ==);
  REL_OP("!=", !=);
  REL_OP(">", >);
  REL_OP("<", <);
  REL_OP(">=", >=);
  REL_OP("<=", <=);

#undef MATH_OP
#undef REL_OP
  goto print_help;
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
        case '!': printf("%d", history_index + 1); break;

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

  if (lhs.next && lhs.next->value.begin == lhs.next->value.end) {
    fprintf(stderr, BRIGHT_RED "microshell: syntax error - unexpected end of the line\n" COLOR_RESET);
    lhs.type = None;
    lhs.value.end = lhs.value.begin;
    free(lhs.next);
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

  vector(char*, &args, veclen(args)) = NULL;
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
    if (not_fork || (pid = marked_fork()) == 0)
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
      if (not_fork || (pid = marked_fork()) == 0) {
        /* child process for lhs of pipe operator */
        if (pipe(pipefd) < 0){
          fprintf(stderr, "Cannot open pipe to handle processes.\n");
          exit(EXIT_FAILURE);
        }

        if (marked_fork() == 0) {
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
      if ((pid = marked_fork()) == 0)
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
      command_not_found:
      printf(BRIGHT_RED "microshell: command {" BRIGHT_WHITE "%s" BRIGHT_RED "} was not found.\n" COLOR_RESET, cmdname);
      exit(EXIT_FAILURE);
    }
  }

  exec:
  args = build_args(cmd, cmdname, word);
  
  /* execute command or builtin with given args */
  if (builtin) {
    exit(builtin_commands[builtin-1].handler(args.size - 1, (char**)args.data));
  } else {
    execv(buffer, (char**)args.data);
    goto command_not_found;
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
  for (i = 1; i < history.size; i += 2)
    free(vector(ptrdiff_t, &history, i));
  
  vector_destroy(&history);
  fill(history, 0);
  history_index = 0;
}

#define MAX(a, b) ((a) >= (b) ? (a) : (b))

int load_history(char const *filename) 
{
  size_t i, n, r;
  FILE *f;
  ptrdiff_t v, *kv;
  StringView sv, cpy;

  if ((f = fopen(filename, "r")) == 0) {
    perror("history-load");
    return EXIT_FAILURE;
  }

  fscanf(f, "%zu\n", &n);
  vector_reserve(ptrdiff_t, &history, MAX(history.capacity, n));

  for (i = 0; i < n; ++i) {
    cpy = sv = readline(f);
    cpy.begin = 1 + strchr(sv.begin, ' ');
    sscanf(sv.begin, "%ld ", &v);
    if ((kv = map_search(&history, v)) != NULL)
      free(kv[1]);
    map_insert(&history, v, strview_to_cstr(cpy));
    free(sv.begin);
  }

  fclose(f);
  return EXIT_SUCCESS;
}

void return_to_main_loop(int signal_number)
{
  (void)signal_number;
  siglongjmp(jump_env, 0xdeadbeef);
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

void set_env_if_present()
{
  if (globals->variableCommand == SetVariable) {
    setenv(globals->optional_text, globals->text, 1);
  }
}

int main(int argc, char const* *argv)
{
  StringView sv, input;
  ptrdiff_t *kv;

  memset(&isma, 0, sizeof(isma));
  fill(history, 0);
  path_dirs = parse_path_env(getenv("PATH"));

  if (signal(SIGUSR1, handle_exit) == SIG_ERR)
    fprintf(stderr, "microshell: cannot register signal for exit command.\n" BRIGHT_RED "That makes exit command useless\n" COLOR_RESET);
  
  globals = interprocess_shared_memory_allocator(&isma, sizeof(struct GlobalState), NULL);
  
  while (getcwd(globals->cwd, sizeof(globals->cwd)) == NULL)
    ;

  signal(SIGINT, return_to_main_loop);

  set_ps1();
  atexit(clear_interprocess_memory);

  
  if (sigsetjmp(jump_env, 1) == (int)0xdeadbeef)
    putchar('\n');

  for (;;) {
    while (wait(NULL) > 0)
      ;

    if (globals->history_command == ClearHistory) {
      clear_history();
      globals->history_command = NoHistoryCommand;
    } else if (globals->history_command == LoadHistory) {
      globals->exit_code = load_history(globals->text);
      globals->history_command = NoHistoryCommand;
    } else if (globals->new_history_index > 0) {
      sv.begin = globals->text;
      sv.end = sv.begin + strlen(sv.begin);
      if ((kv = map_search(&history, globals->new_history_index)) != NULL) {
        free(kv[1]);
      }
      map_insert(&history, globals->new_history_index, strview_to_cstr(sv));
      globals->new_history_index = 0;
      --history_index;
    } else {
      set_env_if_present();
      globals->variableCommand = NoVariableCommand;
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
    input.begin[input.end - input.begin] = '\0';
    map_insert(&history, ++history_index, input.begin);
  }

  return EXIT_SUCCESS;
}
