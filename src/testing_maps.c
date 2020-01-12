#include <stdio.h>
#include "vector.h"
#include <stdint.h>
#include <stddef.h>

typedef ptrdiff_t KeyValue;

#define fst(kv) ((kv)[-1])
#define snd(kv) ((kv)[0])

void map_insert(Vector *dict, ptrdiff_t key, ptrdiff_t val)
{
  ptrdiff_t l, r, m, i;
  KeyValue *kv, *kvnext;
  r = veclen(*dict) - 1;
  
  if (veclen(*dict) == 0 || fst((kv = &vector(ptrdiff_t, dict, r))) < key) {
    kv = &vector(ptrdiff_t, dict, veclen(*dict) + 1);
    fst(kv) = key;
    snd(kv) = val;
    
    printf("kv: %ld %ld\n", fst(kv), snd(kv));
    printf("last or empty\n");

    return;
  }
  
  l = 0;
  r = veclen(*dict) / 2 - 1;

  for (;;) {
    m = (l + r) / 2;
    kv = &vector(ptrdiff_t, dict, 2 * m + 1);
    kvnext = &vector(ptrdiff_t, dict, 2 * (m + 1) + 1);
    if (m == 0 || (fst(kv) <= key && fst(kvnext) > key))
      break;
    else if (key < fst(kv))
      r = m;
    else
      l = m;
    
    printf("%ld\n", m);
  }

  if (fst(kv) == key)
    snd(kv) = val;
  else {
    for (i = veclen(*dict) - 1; i >= 2 * m; i -= 2) {
      printf("1mv %ld to %ld\n", i,     i + 2);
      printf("2mv %ld to %ld\n", i - 1, i + 1);
      vector(ptrdiff_t, dict, i + 2) = vector(ptrdiff_t, dict, i);
      vector(ptrdiff_t, dict, i + 1) = vector(ptrdiff_t, dict, i - 1);
    }
    kv = &vector(ptrdiff_t, dict, 2 * m + 1);
    fst(kv) = key;
    snd(kv) = val;
  }
}

int main()
{
  int i;
  Vector dict;
  KeyValue *kv;
  fill(dict, 0);

  map_insert(&dict, 11, 22);
  map_insert(&dict, 33, 44);
  map_insert(&dict, 22, 2);

  for (i = 1; i < (ptrdiff_t)veclen(dict); i += 2) {
    kv = &vector(ptrdiff_t, &dict, i);
    printf("%ld -> %ld\n", fst(kv), snd(kv));
  }

  return 0;
}
