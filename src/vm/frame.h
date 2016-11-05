// frame.h
#ifndef _VM_FRAME_H
#define _VM_FRAME_H

#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/syscall.h"
#include <debug.h>
#include <list.h>

void lru_list_init(void);
void add_page_to_lru_list(struct page *page);
void del_page_from_lru_list(struct page *page);
void *try_to_free_pages(enum palloc_flags flag);
struct list lru_list;							// page를 관리하는 list
struct lock lru_list_lock;				// lru_list를 위한 lock
struct list_elem *lru_clock;			// lru_list의 elem을 가리키는 포인터



#endif //_VM_FRAME_H
