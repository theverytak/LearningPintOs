// frame.c
#include "vm/frame.h"

static struct list_elem *get_next_lru_clock();

void lru_list_init(void) {						// lru_list, lru_list_lock, lru_clock초기화
	list_init(&lru_list);
	lock_init(&lru_list_lock);
	lru_clock = NULL;
}

// page를 lru뒤에 삽입
void add_page_to_lru_list(struct page *page) { 
  //lock_acquire (&lru_list_lock);	// 공유 list는 항상 lock
	list_push_back(&lru_list, &page->lru);
  //lock_release (&lru_list_lock);
}

// lru_list에서 page를 삭제
void del_page_from_lru_list(struct page *page) {
  //lock_acquire (&lru_list_lock);	// 공유 list는 항상 lock
	list_remove(&page->lru);
  //lock_release (&lru_list_lock);
}

// for debug..
// print all pages in lru_list
void print_lru_list(void) {
	printf("*****************begin************************\n");
	struct list_elem *e;
	int i = 0;
	for(e = list_begin(&lru_list); e != list_end(&lru_list); e = list_next(e)) {
		struct page *page = list_entry(e, struct page, lru);
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@\n");
		printf("number %d\n", i++);
		printf("owner : %s\n", page->thread->name);
		printf("page->kaddr : %p\n", page->kaddr);
		printf("page->vme->vaddr :%p\n", page->vme->vaddr);
		printf("@@@@@@@@@@@@@@@@@@@@@@@@@\n");
	}
	printf("********************end***********************\n");
}

static struct list_elem *get_next_lru_clock() {
	// lru_clock이 NULL이면 
	if(NULL == lru_clock)
		return NULL;
	// list원소가 하나도 없는거
	if(list_empty(&lru_list))
		return NULL;
	// 11시(마지막원소)이면 12시로 바꾸고 return
	if(lru_clock == list_end(&lru_list)) 
		return lru_clock = list_begin(&lru_list);
	
	// 정상 케이스
	lru_clock = list_next(lru_clock);
	return lru_clock;
}

// lru_list를 돌며 accessed bit 가 0인 친구를 페이지 해제
void *try_to_free_pages(enum palloc_flags flag) {
	void *kaddr;			// return용
	lock_acquire(&lru_list_lock);

	lru_clock = list_begin(&lru_list);
	while(true) {
		struct page *page = list_entry(lru_clock, struct page, lru);
		lru_clock = get_next_lru_clock();

		// accessed bit 가 1이면 0으로 바꿈
		if(pagedir_is_accessed(page->thread->pagedir, page->vme->vaddr))
			pagedir_set_accessed(page->thread->pagedir, page->vme->vaddr, false);
		// 0이면 해제
		else {
			//해제 시 타입별로 다름
			switch(page->vme->type) {
				// type을 anon으로 바꿈 그리고 swap out
				case VM_BIN :								
					page->vme->type = VM_ANON;
					page->vme->swap_slot = swap_out(page->kaddr);
					break;
				// type을 바꾸지는 않음. dirty만 보고서 file에 기록 or not
				case VM_FILE :
					if(pagedir_is_dirty(page->thread->pagedir, page->vme->vaddr)) {
						lock_acquire(&filesys_lock);
						file_write_at(page->vme->file, page->vme->vaddr,
													page->vme->read_bytes, page->vme->offset);
						lock_release(&filesys_lock);
					}
					break;
				// bin과 같음
				case VM_ANON :
					page->vme->type = VM_ANON;
					page->vme->swap_slot = swap_out(page->kaddr);
					break;
			}
			page->vme->is_loaded = false;
			__free_page(page);

			kaddr = palloc_get_page(flag);
			if(kaddr)
				break;
		} // end of else
	} // end of while
	lock_release(&lru_list_lock);

	return kaddr;
}


