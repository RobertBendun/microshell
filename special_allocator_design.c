#include <stdint.h>

void *fixed_memory_allocator(void *data, size_t old_size, size_t new_size, void *old_ptr)
{

}

typedef struct {
  
} FixedMemoryAllocator;

typedef struct {
  void *allocator_data;
  void*(*allocator)(void*, size_t, size_t, void*);
} Allocator;

int main()
{

}
