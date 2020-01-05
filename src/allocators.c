#include "allocators.h"

#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

void* malloc_allocator(void *allocator_data, size_t new_size, void *old_ptr)
{
  if (old_ptr == NULL)
    return malloc(new_size);
  
  if (new_size == 0) {
    free(old_ptr);
    return NULL;
  }

  return realloc(old_ptr, new_size);
}

static void write_serialized_id(char *mem, int id)
{
  const int range = '9' - '0' + 'z' - 'a' + 'Z' - 'A' + 3;
  int c, p;
  for (mem += 3; id != 0; --mem) {
    c = id % range;
    id /= range;

    *mem = 
      (p = c, c -= ('9' - '0' + 1)) < 0 ? p + '0' : 
      (p = c, c -= ('z' - 'a' + 1)) < 0 ? p + 'a' : 
      (p = c, c -= ('Z' - 'A' + 1)) < 0 ? p + 'A' : -1;
  }
}

/*
this allocator should be object specific - each allocated object should have own copy of InterprocessSharedMemoryAllocator struct
 hovewer you can reuse it after original user free it.

if reallocation fails NULL is returned and pointed memory is unchanged
*/
void* interprocess_shared_memory_allocator(InterprocessSharedMemoryAllocator *data, size_t new_size, void *old_ptr)
{
#if defined(__func__)
  #define UNIQUE_PREFIX "/" __func__ "0000"
  #define UNIQUE_PREFIX_OFF (sizeof("/" __func__) - 1)
#elif defined(__FUNCTION__)
  #define UNIQUE_PREFIX "/" __FUNCTION__ "0000"
  #define UNIQUE_PREFIX_OFF (sizeof("/" __FUNCTION__) - 1)
#elif defined(__PRETTY_FUNCTION__)
  #define UNIQUE_PREFIX "/" __PRETTY_FUNCTION__ "0000"
  #define UNIQUE_PREFIX_OFF (sizeof("/" __PRETTY_FUNCTION__) - 1)
#else
  #define UNIQUE_PREFIX "/" "bendun_interprocess_shared_memory_allocator" "0000"
  #define UNIQUE_PREFIX_OFF (sizeof("/" "bendun_interprocess_shared_memory_allocator") - 1)
#endif

  static int id = 0;
  char buffer[] = UNIQUE_PREFIX;
  void *remaped;

 
  if (old_ptr == NULL) {
    open_with_unique_name:
    data->id = ++id;
    write_serialized_id(buffer + UNIQUE_PREFIX_OFF, data->id);
    if ((data->shared_memory_fd = shm_open(buffer, O_RDWR | O_CREAT | O_EXCL, 0666)) < 0) {
      if (errno == EEXIST)
        goto open_with_unique_name;
      return NULL;
    }

    if (ftruncate(data->shared_memory_fd, new_size) < 0) {
      shm_unlink(buffer);
      return NULL;
    }

    data->allocated_memory_length = new_size;
    data->allocated_memory = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, data->shared_memory_fd, 0);
    if (data->allocated_memory == MAP_FAILED) {
      shm_unlink(buffer);
      return NULL;
    }
    
    return data->allocated_memory;
  }

  if (new_size == 0) {
    write_serialized_id(buffer + UNIQUE_PREFIX_OFF, data->id);
    munmap(data->allocated_memory, data->allocated_memory_length);
    shm_unlink(buffer);
    return NULL;
  }

  if (ftruncate(data->shared_memory_fd, new_size) < 0)
    return NULL;
  
  data->allocated_memory_length = new_size;
  if ((remaped = mremap(old_ptr, data->allocated_memory_length, new_size, MREMAP_MAYMOVE | MAP_SHARED)) == MAP_FAILED)
    return NULL;
  return data->allocated_memory = remaped;

#undef UNIQUE_PREFIX
#undef UNIQUE_PREFIX_OFF
}