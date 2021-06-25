#include <unistd.h>

#define MAX_ALLOC_SIZE 100000000

void* smalloc(size_t size);

void* smalloc(size_t size)
{
    if(size == 0 || size > MAX_ALLOC_SIZE)
    {
        return NULL;
    }
    void* prev_brk = sbrk(size);
    if(prev_brk == (void*)-1)
    {
        return NULL;
    }
    return prev_brk;
}
