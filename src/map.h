#define fst(kv) ((kv)[0])
#define snd(kv) ((kv)[1])

ptrdiff_t* map_insert(Vector *dict, ptrdiff_t key, ptrdiff_t value)
{
  size_t i, j;
  ptrdiff_t *kv;
  size_t const vec_size = veclen(*dict);

  if (vec_size == 0) {
    vector(ptrdiff_t, dict, 1) = value;
    vector(ptrdiff_t, dict, 0) = key;
    return (ptrdiff_t*)dict->data;
  }

  for (i = 0; i < vec_size; i += 2)
    if (vector(ptrdiff_t, dict, i) >= key)
      break;

  if (*(kv = &vector(ptrdiff_t, dict, i)) == key) {
    snd(kv) = value;
    return kv;
  }

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


    if (*mid == key)
      return mid;
    else if (*mid < key)
      l = m + 2;
    else
      r = m - 2;
  }

  return NULL;
}
