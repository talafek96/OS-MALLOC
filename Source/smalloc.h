#ifndef _MALLOC_2_H
#define _MALLOC_2_H
#include <unistd.h>
#include <cstring>

void* smalloc(size_t size);
void* scalloc(size_t num, size_t size);
void* sfree(void* p);
void* srealloc(void* oldp, size_t size);

#endif