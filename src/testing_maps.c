#include <stdio.h>
#include "vector.h"
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <time.h>
#include <stdlib.h>

typedef ptrdiff_t KeyValue;

#define fst(kv) ((kv)[0])
#define snd(kv) ((kv)[1])

ptrdiff_t* map_insert(Vector *dict, ptrdiff_t key, ptrdiff_t value)
{
  size_t i, j;
  size_t const vec_size = veclen(*dict);

  if (vec_size == 0) {
    vector(ptrdiff_t, dict, 1) = value;
    vector(ptrdiff_t, dict, 0) = key;
    return dict->data;
  }

  for (i = 0; i < vec_size; i += 2)
    if (vector(ptrdiff_t, dict, i) >= key)
      break;

  for (j = vec_size; j > i; j -= 2) {
    vector(ptrdiff_t, dict, j + 1) = vector(ptrdiff_t, dict, j - 1);
    vector(ptrdiff_t, dict, j + 0) = vector(ptrdiff_t, dict, j - 2);
  }

  vector(ptrdiff_t, dict, i + 0) = key;
  vector(ptrdiff_t, dict, i + 1) = value;

  return &vector(ptrdiff_t, dict, i);
}

ptrdiff_t* map_search(Vector *dict, ptrdiff_t key)
{
  size_t l, r, m;
  ptrdiff_t *mid;

  if (veclen(*dict) == 0)
    return NULL;
  
  if (*(mid = &vector(ptrdiff_t, dict, 0)) == key || *(mid = &vector(ptrdiff_t, dict, veclen(*dict) - 2)) == key)
    return mid;

  for (l = 0, r = veclen(*dict) - 2; l <= r; ) {
    m = (l + r) / 2;
    m -= m & 1;
    mid = &vector(ptrdiff_t, dict, m);
    
    printf("pos: %zu %zu %zu\n", l, m, r);
    printf("val: %zd\n", *mid);

    if (*mid == key)
      return mid;
    else if (*mid < key)
      l = m + 2;
    else
      r = m - 2;
  }

  return NULL;
}

int random_value()
{
  return rand() % (1000 - 100) + 100;
}

void test()
{
  ptrdiff_t keys[100], values[100], *kv;
  size_t i, j;
  Vector dict;
  fill(dict, 0);

  srand(time(NULL));
 
  for (i = 0; i < 100; ++i) {
    make_new: keys[i] = random_value();
    for (j = 0; j < i; ++j)
      if (keys[j] == keys[i])
        goto make_new;

    values[i] = random_value();
  }

  for (i = 0; i < 100; ++i) {
    kv = map_insert(&dict, keys[i], values[i]);
    if (*kv != keys[i]) {
      printf("invalid insert: %zd\n", *kv);
      return;
    }
  }

  for (i = 0; i < veclen(dict); i += 2) {
    printf("%zd ", ((ptrdiff_t*)dict.data)[i]);
  }
  puts("\n");

  for (i = 0; i < 100; ++i) {
    if ((kv = map_search(&dict, keys[i])) != NULL) {
      assert(fst(kv) == keys[i]);
      assert(snd(kv) == values[i]);
    }
    else {
      fprintf(stderr, "Invalid key %zd\n", keys[i]);
      return;
    }
  }
}

int main()
{
  int i;
  Vector dict;
  KeyValue *kv;
  fill(dict, 0);

  test();
  return 0;

  map_insert(&dict, 11, 22);
  map_insert(&dict, 33, 44);
  map_insert(&dict, 22, 2);

  for (i = 0; i < (ptrdiff_t)veclen(dict); i += 2) {
    kv = &vector(ptrdiff_t, &dict, i);
    printf("%ld -> %ld\n", fst(kv), snd(kv));
  }


  assert(map_search(&dict, 11) != NULL);
  assert(map_search(&dict, 22) != NULL);
  assert(map_search(&dict, 33) != NULL);

  assert(map_search(&dict, 11)[1] == 22);
  assert(map_search(&dict, 22)[1] == 2);
  assert(map_search(&dict, 33)[1] == 44);

  return 0;
}
