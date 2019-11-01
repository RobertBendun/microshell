#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include "StringView.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

StringView readline(FILE *in)
{
  StringView result;
  size_t len = 0;
  ssize_t length = 0;

  result.begin = result.end = NULL;
  memset(&result, 0, sizeof(result));
  if ((length = getline(&(result.begin), &len, in)) < 0) {
    perror("getline");
    free(result.begin);
    result.begin = result.end = NULL;
    return result;
  }

  result.end = result.begin + length;
  return result;
}

StringView trim(StringView sv)
{
  while (sv.begin != sv.end && (*sv.begin == ' ' || *sv.begin == '\t'))
    ++sv.begin;
  
  while (sv.begin != sv.end && (*(sv.end - 1) == ' ' || *(sv.end - 1) == '\t'))
    --sv.end;

  return sv;
}

int strview_str_cmp(StringView sv, char const* str)
{
  while (sv.begin != sv.end) {
    if (*sv.begin != *str)
      break;
    ++sv.begin;
    ++str;
  }

  if (sv.begin == sv.end)
    return  *str == '\0' ? 0 : -1;

  return ((unsigned char)*sv.begin) - ((unsigned char)*str);
}

int try_match(StringView sv, char const *str)
{
  int i = 0;

  size_t count = 0;
  size_t len = strlen(str);

  for (i = 0; sv.begin != sv.end && str[i] != '\0'; ++i)
    if (sv.begin[i] == str[i])
      ++count;
    else {
      count = 0;
      break;
    }

  return count == len;
}

char* strview_to_cstr(StringView sv)
{
  char *str = malloc(strviewlen(sv) + 1);
  str[strviewlen(sv)] = '\0';
  memcpy(str, sv.begin, strviewlen(sv));
  return str;
}
