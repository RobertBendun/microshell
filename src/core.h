#ifndef CORE_H
#define CORE_H

#define false (!!0)
#define true  (!false)

#define cast(type, value) ((type)(value))

#include <string.h>
#define fill(var, val) (memset(&(var), val, sizeof(var)))

#define arraylen(a) (sizeof(a) / sizeof(*a))

#include <stdint.h>
#include <assert.h>

#endif