// page.c
#include "page.h"
#include "frame.h"

static unsigned vm_hash_func (const struct hash_elem *e, void *aux) {
	// hash_elem인 e를 이용해 vm_entry를 찾고, 해당 vm_entry의 가상
	// 페이지 번호를 이용해 해시 값을 리턴
	if(NULL != e)
		return hash_int(hash_entry(e, struct vm_entry, elem)->vaddr);
}

static bool vm_less_func(const struct hash_elem *a, 
												 const struct hash_elem *b) {
	// a, b의 vm_entry의 가상 페이지 번호를 비교.
	// a < b이면 true
	if(hash_entry(a, struct vm_entry, elem)->vaddr <
		 hash_entry(b, struct vm_entry, elem)->vaddr)
		return true;

	return false;
}

void vm_init(struct hash *vm) {
	if(NULL != vm)
		hash_init(vm, vm_hash_func, vm_less_func, NULL);
}

bool insert_vme(struct hash *vm, struct vm_entry *vme) {
	if(NULL == vm || NULL == vme)
		return false;

	// hash_insert는 vme가 vm에 존재하면 NULL이 아닌 값을 리턴함
	// 그러므로 insert성공 => hash_insert가 NULL을 리턴
	if(NULL == hash_insert(vm, &vme->elem))
		return true;

	return false;
}

bool delete_vme(struct hash *vm, struct vm_entry *vme) {
	if(NULL == vm || NULL == vme)
		return false;

	// hash_delete는 vme를 vm에서 찾고, 있으면 삭제 후 NULL이
	// 아닌 값을 리턴함
	if(NULL != hash_delete(vm, &vme->elem))
		return true;

	return false;
}

struct vm_entry *find_vme(void *vaddr) {
	struct hash_elem *elem;
	struct vm_entry vme;

	vme.vaddr = pg_round_down(vaddr);		// 인자 vaddr에서 vpn을 추출  

	elem = hash_find(&thread_current()->vm, &vme.elem);
	// 못찾았으면 NULL리턴
	if(NULL == elem)
		return NULL;
	
	return hash_entry(elem, struct vm_entry, elem);
}

void vm_destroy(struct hash *vm) {
	if(NULL != vm)
		hash_destroy(vm, vm_destroy_func);
}

void vm_destroy_func(struct hash_elem *e, void *aux UNUSED) {
	if(NULL != e) {
		struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);

		// 실제로 탑재 된 경우
		if(true == vme->is_loaded) {
			struct thread *t = thread_current();
			// pagedir에서 검색해서 물리 페이지를 해제함
			palloc_free_page(pagedir_get_page(t->pagedir, vme->vaddr));
			// pagedir에서 vaddr의 맵핑을 해제
			pagedir_clear_page(t->pagedir, vme->vaddr);
		}

		// load_segment에서 malloc을 사용하기 때문에 free로 해제함
		free(vme);
	}
}

struct page* alloc_page(enum palloc_flags flags) {
	struct page *page;
	page = (struct page *)malloc(sizeof(struct page));			// page구조체를 할당
	if(NULL == page)																				// malloc실패시
		return NULL;
	page->thread = thread_current();												// page구조체 초기화
	page->kaddr = NULL;
	page->vme = NULL;
	lock_acquire(&lru_list_lock);
	add_page_to_lru_list(page);
	lock_release(&lru_list_lock);

	// palloc_get_page로 물리 페이지 할당
	page->kaddr = palloc_get_page(flags);
	if(NULL == page->kaddr) {											// 아래는 메모리가 부족할 경우임
		page->kaddr = try_to_free_pages(flags);			// 우선 메모리를 확보
	}
	
	return page;
}
	
// 시작 주소가 kaddr인 물리 페이지를 삭제
// 그런거 lru_list에서 못찾으면 아무것도 안함
void free_page(void *kaddr) {
	struct list_elem *e;
	lock_acquire(&lru_list_lock);		// 아래에서 list_remove를 하므로 lock
	for(e = list_begin(&lru_list); e != list_end(&lru_list); e = list_next(e)) {
		struct page *page = list_entry(e, struct page, lru);
		// 찾으면 _free_page하고 for문 퇴장
		if(kaddr == page->kaddr) {
			__free_page(page);
			break;
		}

	}

	lock_release(&lru_list_lock);
}

// 물리 페이지 page를 해제
void __free_page(struct page *page) {
	del_page_from_lru_list(page);		// lru_list에서 삭제
	palloc_free_page(page->kaddr);	// alloc_page에서 할당한 것 삭제
	pagedir_clear_page(page->thread->pagedir, page->vme->vaddr);	// pagedir해제
	free(page);											// 역시 해제
}



