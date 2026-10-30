#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#ifndef heap_caps_calloc
#define heap_caps_calloc(n,s,caps) calloc(n,s)
#endif
#define heap_caps_free(ptr) free(ptr)
