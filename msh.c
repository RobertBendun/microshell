/* Microshell, Aleksander Kiryk, 2018 */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#define CMDLEN 100
#define ARGLEN 10

struct cmd {
	int type;
	union {
		char **argv;
		struct cmd *op[2];
		struct {
			struct cmd *cmd;
			char *file;
		};
	};
};

struct cmd *freecmd(struct cmd *);

void runhelp(struct cmd *);
void runcd(struct cmd *);
void runexit(struct cmd *);
void runln(struct cmd *);
void runtee(struct cmd *);

int getbuiltin(char *);

struct builtin {
	char *name;
	char *info;
	void (*run)(struct cmd *);
	int forked;
} builtin[] = {
	"cd",   "cd   <path>       - change directory to <path>",             runcd,   0,
	"exit", "exit              - exit the shell",                         runexit, 0,
	"ln",   "ln   <file> <new> - create <new> hardlink to <file>",        runln,   1,
	"tee",  "tee  <files ...>  - pass stdin to given <files> and stdout", runtee,  1,
	"help", "help              - display this message",                   runhelp, 0,
	0,
};

void error(int status, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	if (status)
		exit(status);
}

/* lexer */
const char *special = "<>|;\n";

char *buf = 0;
int length = 0, type = '\n';

char bufput(int i, char ch)
{
	if (i >= length) {
		char *new = calloc(i*2+1, sizeof(char));

		memcpy(new, buf, length);
		free(buf);
		buf = new;
		length = i*2+1;
	}
	return buf[i] = ch;
}

void lex(void)
{
	int i = 0, ch;

	while (isspace(ch = getchar()) && ch != '\n')
		;
	if (ch < 0)
		type = -1;
	if (strchr(special, ch)) {
		type = ch;
	} else {
		type = ' ';
		bufput(i++, ch);
		while (!isspace(ch = getchar()) && !strchr(special, ch))
			bufput(i++, ch);
		ungetc(ch, stdin);
	}
	bufput(i, '\0');
}

int match(int wanted)
{
	if (type != wanted)
		return 0;
	lex();
	return wanted;
}

void panic(char *snc)
{
	while (!strchr(snc, type))
		lex();
}

/* parser */
char *parsename(void)
{
	char *name;

	if (type != ' ') {
		error(0, "syntax error: expected program or file name");
		return NULL;
	}
	name = calloc(strlen(buf)+1, sizeof(char));
	strcpy(name, buf);
	match(' ');
	return name;
}

struct cmd *parseexec(void)
{
	struct cmd *exec;
	int i = 0, len = 1;

	if (type != ' ') {
		error(0, "syntax error: expected program or file name");
		return NULL;
	}
	exec = calloc(1, sizeof(*exec));
	exec->type = ' ';
	exec->argv = calloc(2, sizeof(char*));
	while (type == ' ') {
		if (i >= len)
			exec->argv = realloc(exec->argv, ((len *= 2)+1)*sizeof(char*));
		if (!(exec->argv[i++] = parsename()))
			return freecmd(exec);
	}
	exec->argv[i] = NULL;
	return exec;
}

struct cmd *parseredir(void)
{
	struct cmd *cmd, *redir;
	int ch;

	if (!(cmd = parseexec()))
		return freecmd(cmd);
	while ((ch = match('>')) || (ch = match('<'))) {
		redir = calloc(1, sizeof(*redir));
		redir->type = ch;
		redir->cmd = cmd;
		if (!(redir->file = parsename()))
			return freecmd(redir);
		cmd = redir;
	}
	return cmd;
}

struct cmd *parsepipe(void)
{
	struct cmd *cmd, *pp;

	if (!(cmd = parseredir()))
		return freecmd(cmd);
	while (match('|')) {
		pp = calloc(1, sizeof(*pp));
		pp->type = '|';
		pp->op[0] = cmd;
		if (!(pp->op[1] = parseredir()))
			return freecmd(pp);
		cmd = pp;
	}
	return cmd;
}

struct cmd *parsecmd(void)
{
	struct cmd *cmd;

	if (match(-1))
		exit(0);
	if (!match('\n') && !match(';'))
		error(0, "syntax error: invalid type of data");
	if (type == '\n' || type == ';')
		return NULL;
	return parsepipe();
}

/* semantics */
struct cmd *freecmd(struct cmd *cmd)
{
	int i;

	if (!cmd)
		return 0;
	switch (cmd->type) {
	case '|':
		freecmd(cmd->op[0]);
		freecmd(cmd->op[1]);
		break;
	case '<':
	case '>':
		freecmd(cmd->cmd);
		free(cmd->file);
		break;
	case ' ':
		for (i = 0; cmd->argv[i]; i++)
			free(cmd->argv[i]);
		free(cmd->argv);
		break;
	default:
		break;
	}
	free(cmd);
	return 0;
}

void runcmd(struct cmd *cmd)
{
	int fd, p[2], n;

	switch (cmd->type) {
	case '|':
		pipe(p);
		if (fork() == 0) {
			close(1);
			dup(p[1]);
			close(p[0]);
			close(p[1]);
			runcmd(cmd->op[0]);
		}
		if (fork() == 0) {
			close(0);
			dup(p[0]);
			close(p[0]);
			close(p[1]);
			runcmd(cmd->op[1]);
		}
		close(p[0]);
		close(p[1]);
		wait(NULL);
		wait(NULL);
		break;
	case '>':
		if ((fd = open(cmd->file, O_CREAT|O_WRONLY|O_TRUNC, S_IWUSR|S_IRUSR)) < 0)
			error(1, "error: could not open file `%s`", cmd->file);
		close(1);
		dup(fd);
		close(fd);
		runcmd(cmd->cmd);
		break;
	case '<':
		if ((fd = open(cmd->file, O_RDONLY)) < 0)
			error(1, "error: could not open file `%s`", cmd->file);
		close(0);
		dup(fd);
		close(fd);
		runcmd(cmd->cmd);
		break;
	case ' ':
		n = getbuiltin(cmd->argv[0]);
		if (n >= 0) {
			builtin[n].run(cmd);
			break;
		}
		execvp(cmd->argv[0], cmd->argv);
		error(1, "error: could not execute `%s`", cmd->argv[0]);
		break;
	}
	exit(0);
}


void run(struct cmd *cmd)
{
	int n;

	if (cmd->type == ' ') {
		n = getbuiltin(cmd->argv[0]);
		if (n >= 0 && !builtin[n].forked)
			return builtin[n].run(cmd);
	}

	if (fork() == 0)
		runcmd(cmd);
	wait(NULL);
}

int getbuiltin(char *name)
{
	int i;

	for (i = 0; builtin[i].name; i++)
		if (strcmp(name, builtin[i].name) == 0)
			return i;
	return -1;
}

/* built-ins */
void runcd(struct cmd *cmd)
{
	char *path = cmd->argv[1]? cmd->argv[1] : getenv("HOME");

	if (chdir(path) < 0)
		error(0, "cd: could not open directory `%s`", path);
}

void runexit(struct cmd *cmd)
{
	exit(0);
}

void runln(struct cmd *cmd)
{
	if (!cmd->argv[1] || !cmd->argv[2] || cmd->argv[3])
		error(1, "ln: expected a filename followed by a linkname");
	if (link(cmd->argv[1], cmd->argv[2]) < 0)
		error(1, "ln: error has occured while linking the file");
	exit(0);
}

void runtee(struct cmd *cmd)
{
	char buf[100];
	int i, n, m, *fd;

	for (n = 0; cmd->argv[n+1]; n++)
		;
	if (n <= 0)
		error(1, "tee: expected at least one filename");

	fd = calloc(n, sizeof(int));
	for (i = 0; i < n; i++)
		if ((fd[i] = open(cmd->argv[i+1], O_CREAT|O_WRONLY|O_TRUNC, S_IWUSR|S_IRUSR)) < 0)
			error(1, "tee: could not open file `%s`", cmd->argv[i+1]);
	while ((m = read(0, buf, sizeof(buf))) > 0) {
		if (write(1, buf, m) < 0)
			error(1, "tee: stdandard output is closed");
		for (i = 0; i < n; i++)
			if (write(fd[i], buf, m) < 0)
				error(1, "tee: file `%s` is closed", cmd->argv[i+1]);
	}
	exit(0);
}

void runhelp(struct cmd *cmd)
{
	int i;

	puts("\n"
	  "Microshell, Aleksander Kiryk, 2018\n"
	  "\n"
	  "The program provides minimal functionality of a shell.\n"
	  "It understands pipe (|) and input/output redirection (<, >) operators.\n"
	  "\n"
	  "The programs are run with respect of user's PATH settings.\n"
	  "\n"
	  "The shell provides following built-in commands:");
	for (i = 0; builtin[i].name; i++)
		printf("* %s\n", builtin[i].info);
	putchar('\n');
}

int main()
{
	char cwd[100];
	struct cmd *cmd;

	for (;;) {
		printf("[%s] $ ", getcwd(cwd, sizeof(cwd)));
		if (cmd = parsecmd()) {
			run(cmd);
			freecmd(cmd);
		} else {
			panic("\n;");
		}
	}
}

