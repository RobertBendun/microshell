#include <stdio.h>

#include "vector.h"
#include "terminal.h"

int main()
{
  size_t i = 0;
  Vector vec;
  fill(vec, 0);

  for (i = 0; i < 32; ++i)
    vector(int, &vec, i) = i + 1;

  for (i = 0; i < vec.size; ++i)
    printf(BLUE "[%2ld]:" COLOR_RESET " %d\n", i, vector(int, &vec, i));
  
  vector_destroy(&vec);
}
