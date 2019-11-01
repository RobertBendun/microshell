#ifndef STRING_VIEW_H
#define STRING_VIEW_H

#include <stdio.h>

typedef struct {
  char *begin;
  char *end;
} StringView;

#define strviewlen(strview) ((strview).end - (strview).begin)

StringView readline(FILE *in);
int strview_str_cmp(StringView sv, char const* str);
int try_match(StringView sv, char const *str);
StringView trim(StringView sv);
char* strview_to_cstr(StringView sv);

#endif
