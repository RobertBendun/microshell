#include <stdlib.h>
#include "vector.h"

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
