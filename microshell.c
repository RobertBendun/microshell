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
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#define false (!!0)
#define true  (!false)

#define cast(type, value) ((type)(value))


#define fill(var, val) (memset(&(var), val, sizeof(var)))

#define arraylen(a) (sizeof(a) / sizeof(*a))



/* Incomplete: portability */
#define MAYBE_UNUSED __attribute__((unused))

typedef struct
{
  char *data;
  size_t capacity;
  size_t size;
} Vector;


void  vector_destroy(Vector *vec);
void  vector_reserve_bytes(Vector *vec, size_t minimum_capacity, size_t type_size);
void* access_vector_element(Vector *vec, size_t n, size_t type_size);


#define vector(type, vec, pos) *((type*) access_vector_element((vec), (pos), sizeof(type)))
#define vector_reserve(type, vec, capacity) (vector_reserve_bytes((vec), (capacity), sizeof(type)))
#define veclen(vec) ((vec).size)


#define COLOR_RESET  "\x1b[0m"

#define BLACK   "\x1b[30m"
#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define WHITE   "\x1b[37m"

#define BRIGHT_BLACK   "\x1b[30;1m"
#define BRIGHT_RED     "\x1b[31;1m"
#define BRIGHT_GREEN   "\x1b[32;1m"
#define BRIGHT_YELLOW  "\x1b[33;1m"
#define BRIGHT_BLUE    "\x1b[34;1m"
#define BRIGHT_MAGENTA "\x1b[35;1m"
#define BRIGHT_CYAN    "\x1b[36;1m"
#define BRIGHT_WHITE   "\x1b[37;1m"

#define BOLD      "\x1b[1m"
#define UNDERLINE "\x1b[4m"
#define REVERSED  "\x1b[7m"

#define CLEAR_SCREEN "\x1b[1;1H\x1b[2J"


typedef struct {
  char *begin;
  char *end;
} StringView;

#define strviewlen(strview) ((strview).end - (strview).begin)

StringView readline(FILE *in);
int strview_str_cmp(StringView sv, char const* str);
int try_match(StringView sv, char const *str);
StringView trim(StringView sv);
char* strview_to_cstr(StringView sv);

typedef struct 
{
  int shared_memory_fd;
  void* allocated_memory;
  size_t allocated_memory_length;
  int id;
} InterprocessSharedMemoryAllocator;


void* malloc_allocator(void *allocator_data, size_t new_size, void *old_ptr);
void* interprocess_shared_memory_allocator(InterprocessSharedMemoryAllocator *data, size_t new_size, void *old_ptr);


void* malloc_allocator(void *allocator_data, size_t new_size, void *old_ptr)
{
  if (old_ptr == NULL)
    return malloc(new_size);
  
  if (new_size == 0) {
    free(old_ptr);
    return NULL;
  }

  return realloc(old_ptr, new_size);
}

static void write_serialized_id(char *mem, int id)
{
  const int range = '9' - '0' + 'z' - 'a' + 'Z' - 'A' + 3;
  int c, p;
  for (mem += 3; id != 0; --mem) {
    c = id % range;
    id /= range;

    *mem = 
      (p = c, c -= ('9' - '0' + 1)) < 0 ? p + '0' : 
      (p = c, c -= ('z' - 'a' + 1)) < 0 ? p + 'a' : 
      (p = c, c -= ('Z' - 'A' + 1)) < 0 ? p + 'A' : -1;
  }
}

/*
this allocator should be object specific - each allocated object should have own copy of InterprocessSharedMemoryAllocator struct
 hovewer you can reuse it after original user free it.

if reallocation fails NULL is returned and pointed memory is unchanged
*/
void* interprocess_shared_memory_allocator(InterprocessSharedMemoryAllocator *data, size_t new_size, void *old_ptr)
{
#if defined(__func__)
  #define UNIQUE_PREFIX "/" __func__ "0000"
  #define UNIQUE_PREFIX_OFF (sizeof("/" __func__) - 1)
#elif defined(__FUNCTION__)
  #define UNIQUE_PREFIX "/" __FUNCTION__ "0000"
  #define UNIQUE_PREFIX_OFF (sizeof("/" __FUNCTION__) - 1)
#elif defined(__PRETTY_FUNCTION__)
  #define UNIQUE_PREFIX "/" __PRETTY_FUNCTION__ "0000"
  #define UNIQUE_PREFIX_OFF (sizeof("/" __PRETTY_FUNCTION__) - 1)
#else
  #define UNIQUE_PREFIX "/" "bendun_interprocess_shared_memory_allocator" "0000"
  #define UNIQUE_PREFIX_OFF (sizeof("/" "bendun_interprocess_shared_memory_allocator") - 1)
#endif

  static int id = 0;
  char buffer[] = UNIQUE_PREFIX;
  void *remaped;

 
  if (old_ptr == NULL) {
    open_with_unique_name:
    data->id = ++id;
    write_serialized_id(buffer + UNIQUE_PREFIX_OFF, data->id);
    if ((data->shared_memory_fd = shm_open(buffer, O_RDWR | O_CREAT | O_EXCL, 0666)) < 0) {
      if (errno == EEXIST)
        goto open_with_unique_name;
      return NULL;
    }

    if (ftruncate(data->shared_memory_fd, new_size) < 0) {
      shm_unlink(buffer);
      return NULL;
    }

    data->allocated_memory_length = new_size;
    data->allocated_memory = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, data->shared_memory_fd, 0);
    if (data->allocated_memory == MAP_FAILED) {
      shm_unlink(buffer);
      return NULL;
    }
    
    return data->allocated_memory;
  }

  if (new_size == 0) {
    write_serialized_id(buffer + UNIQUE_PREFIX_OFF, data->id);
    munmap(data->allocated_memory, data->allocated_memory_length);
    shm_unlink(buffer);
    return NULL;
  }

  if (ftruncate(data->shared_memory_fd, new_size) < 0)
    return NULL;
  
  data->allocated_memory_length = new_size;
  if ((remaped = mremap(old_ptr, data->allocated_memory_length, new_size, MREMAP_MAYMOVE | MAP_SHARED)) == MAP_FAILED)
    return NULL;
  return data->allocated_memory = remaped;

#undef UNIQUE_PREFIX
#undef UNIQUE_PREFIX_OFF
}

static uint32_t most_significant_bit_u32(uint32_t n)
{
  n |= n >> (1u << 0);
  n |= n >> (1u << 1);
  n |= n >> (1u << 2);
  n |= n >> (1u << 3);
  n |= n >> (1u << 4);

  return ((n + 1) >> 1);
}

static size_t(*const most_significant_bit_size_t)(size_t) = 
  (size_t(*const)(size_t))most_significant_bit_u32;

static int is_only_one_bit_set(uint64_t b)
{
  return b && !(b & (b-1));
}

void vector_reserve_bytes(Vector *vec, size_t minimum_capacity, size_t type_size)
{
  char *new;
  size_t new_capacity;

  /*
    if only one of bits of min_capacity is set then min_capacity is power of 2 - capacity that we want
    otherwise we have to compute it
  */
  if (is_only_one_bit_set(minimum_capacity))
    new_capacity = minimum_capacity;
  else
    new_capacity = most_significant_bit_size_t(minimum_capacity) << 1;
  
  new = calloc(new_capacity, type_size);
  memmove(new, vec->data, vec->capacity * type_size);
  free(vec->data);

  vec->data = new;
  vec->capacity = new_capacity;
}

void* access_vector_element(Vector *vec, size_t n, size_t type_size)
{
  if (n >= vec->size)
    vec->size = n + 1;
  
  if (n >= vec->capacity)
    vector_reserve_bytes(vec, n+1, type_size);

  return vec->data + type_size * n;
}

void vector_destroy(Vector *vec)
{
  if (vec->data)
    free(vec->data);
}

StringView readline(FILE *in)
{
  StringView result;
  Vector buffer;
  char c = '\0';

  fill(buffer, 0);
  fill(result, 0);

  if (feof(in))
    return result;

  while (!feof(in) && (c != '\n' && c != '\r'))
    vector(char, &buffer, buffer.size) = c = fgetc(in);

  /* shrink to fit size */
  buffer.data = realloc(buffer.data, buffer.size);
  buffer.data[buffer.size-1] = '\0';
  result.begin = buffer.data;
  result.end = buffer.data + buffer.size;

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

char* strview_to_cstr(StringView sv)
{
  char *str = malloc(strviewlen(sv) + 1);
  str[strviewlen(sv)] = '\0';
  memcpy(str, sv.begin, strviewlen(sv));
  return str;
}


int run_command(StringView command);

int builtin_exit(int argc, char **argv);
int builtin_cd(int argc, char **argv);
int builtin_help(int argc, char **argv);

int builtin_history();
int builtin_goto(int argc, char **argv);
int builtin_history_clear();

int builtin_replace_part_of_command(int argc, char **argv);

int builtin_save(int argc, char **argv);
int builtin_append(int argc, char **argv);

int builtin_ps1(int argc, char **argv);

int builtin_add_history_entry(int argc, char **argv);

char const *default_ps1 = "\\e[32;1m\\u\\e[0m[\\e[34;1m\\w\\e[0m]{\\!}\\P ";

typedef int(*Program)(int argc, char **argv);

Vector history;
Vector path_dirs;
InterprocessSharedMemoryAllocator isma;
int is_child = 0;

pid_t marked_fork()
{
  pid_t v = fork();
  if (!is_child) is_child = v == 0;
  if (is_child) signal(SIGINT, SIG_DFL);
  return v;
}

struct GlobalState
{
  int exit_code;
  char cwd[PATH_MAX];
  int clear_history;
  char ps1[PATH_MAX];
  char new_history_entry[PATH_MAX];
  size_t new_history_index;
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
  },
  {
    "+",
    "syntax: '+ [index] [command]",
    builtin_add_history_entry,
    ""
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
      " |  __ \\|  _ \\|  \\/  |/ ____|""   " "microshell by Robert Bendun" "\n"
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

  printf(BRIGHT_WHITE "goto: " BRIGHT_RED "cannot find history entry at index: %d\n", n + 1);
  return EXIT_FAILURE;
}

int builtin_history_clear()
{
  globals->clear_history = 1;
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
  strcpy(globals->ps1, argv[1]);
  return EXIT_SUCCESS;
}

int builtin_add_history_entry(int argc, char **argv)
{
  char buffer[1024];
  char const *input;

  if (argc == 2) {
    fgets(buffer, 1024, stdin);
    *strchr(buffer, '\n') = '\0';
    input = buffer;
  }
  else
    input = argv[2];

  strcpy(globals->new_history_entry, input);
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
  for (i = 0; i < history.size; ++i)
    free((vector(HistoryEntry, &history, i)).command.begin);
  vector_destroy(&history);
  fill(history, 0);
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

  signal(SIGINT, return_to_main_loop);

  set_ps1();
  atexit(clear_interprocess_memory);

  
  if (sigsetjmp(jump_env, 1) == (int)0xdeadbeef)
    putchar('\n');

  for (;;) {
    while (wait(NULL) > 0)
      ;

    if (globals->clear_history) {
      clear_history();
      globals->clear_history = false;
    } else if (globals->new_history_index > 0) {
      printf("%d: %s\n", globals->new_history_index, globals->new_history_entry);
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
