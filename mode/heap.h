/* heap.h - 内核堆：kmalloc/kfree */
#ifndef HEAP_H
#define HEAP_H

void         heap_init(void);
void        *kmalloc(unsigned int size);
void         kfree(void *ptr);
unsigned int heap_used_bytes(void);
unsigned int heap_total_bytes(void);

#endif /* HEAP_H */
