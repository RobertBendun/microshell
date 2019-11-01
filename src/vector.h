#include "core.h"

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
