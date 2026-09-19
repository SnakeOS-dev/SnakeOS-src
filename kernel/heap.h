#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

void  heap_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);

void  heap_stats(size_t *used, size_t *free_bytes, size_t *total);

#endif
