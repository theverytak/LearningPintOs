// page.c
#include "page.h"

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
	// 그러므로 insert성공 == hash_insert가 NULL을 리턴
	if(NULL != hash_insert(vm, &vme->elem))
		return false;

	return true;
}

bool delete_vme(struct hash *vm, struct vm_entry *vme) {
	if(NULL == vm || NULL == vme)
		return false;

	// hash_delete는 vme를 vm에서 찾고, 있으면 삭제 후 NULL이
	// 아닌 값을 리턴함
	if(NULL == hash_delete(vm, &vme->elem))
		return false;

	return true;
}

struct vm_entry *find_vme(void *vaddr) {
	struct hash_elem *elem;
	struct vm_entry vme;

	vme.vaddr = pg_round_down(vaddr);		// 인자 vaddr에서 vpn을 추출  

	elem = hash_find(&thread_current()->vm, &vme.elem);
	if(NULL == elem)
		return NULL;
	
	return hash_entry(elem, struct vm_entry, elem);
}

void vm_destroy(struct hash *vm) {
	if(NULL != vm)
		hash_destory(vm, vm_destroy_func);
}

void vm_destroy_func(struct hash_elem *e, void *aux UNUSED) {
	if(NULL == e)
		return;
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


