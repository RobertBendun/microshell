#include <stddef.h>

typedef struct 
{
  int shared_memory_fd;
  void* allocated_memory;
  size_t allocated_memory_length;
  int id;
} InterprocessSharedMemoryAllocator;


void* malloc_allocator(void *allocator_data, size_t new_size, void *old_ptr);
void* interprocess_shared_memory_allocator(InterprocessSharedMemoryAllocator *data, size_t new_size, void *old_ptr);
