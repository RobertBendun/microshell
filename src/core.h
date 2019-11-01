#define false (0)
#define true  (!false)

#define cast(type, value) ((type)(value))

#include <string.h>
#define fill(var, val) (memset(&(var), val, sizeof(var)))

#ifdef DEBUG
#  include <stdio.h>
#  define LOG(...) (fprintf(stderr, __VA_ARGS__))
#else
#  define LOG(...) ((void)0)
#endif

#include <stdint.h>
#include <assert.h>

typedef struct {
  char *begin;
  char *end;
} StringView;

#define strviewlen(strview) ((strview).end - (strview).begin + 1)
