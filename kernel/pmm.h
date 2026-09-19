#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

void  pmm_init(uint32_t mbi_addr);
void *pmm_alloc_page(void);
void  pmm_free_page(void *page);
size_t pmm_free_pages(void);
size_t pmm_total_pages(void);

#endif
