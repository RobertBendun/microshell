#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* printf example: progress bar */
/* by unveres, 2018 */
/* more at gist.github.com/unveres */
/* look at that 25th line of code, it shows the true power of printf */

#define BARSIZE (50)

int main()
{
  char   bar[BARSIZE + 1];
  int    counter,
         denom;
  float  progress;

  memset(bar, '-', BARSIZE);
  scanf("%d", &denom);   /* here should be some calculation of data size */
  printf("x1b[?25l");

  for (counter = 0; counter <= denom; ++counter) {
    progress = 100.0 * counter / denom;
    printf("\r %5.1f%%    %c <%-*.*s>", /* !!! */
           progress, "-\\|/"[counter / 2048 % 4],
           BARSIZE, (int)progress * BARSIZE / 100, bar);

    usleep(5000); /* here should be some piping instead */
  }

  printf("\x1b[?25h\n");

  return 0;
}
