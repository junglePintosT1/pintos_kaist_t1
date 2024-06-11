#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "lib/kernel/bitmap.h"
struct page;
enum vm_type;

struct anon_page
{
    /* TODO: bitmap idx 저장?! */
    size_t swap_table_idx;
};

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
