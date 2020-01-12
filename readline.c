#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

int main()
{
  while (1) {
    printf("entered: %s\n", readline("puci> "));
  }
}
