// page.h
#ifndef _VM_PAGE_H_
#define _VM_PAGE_H_

#include <hash.h>
#include <list.h>
#include <debug.h>
#include "filesys/file.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

// 가상메모리 entry
struct vm_entry {
	uint8_t type;									// VM_BIN, VM_FILE, VM_ANON의 타입
	void *vaddr;									// vm_entry의 가상페이지 번호
	bool writable;								// 해당 주소에 write가능 여부
	bool is_loaded;								// 물리메모리 탑재 여부
	struct file* file;						// 가상 주소와 사상된 파일
	struct list_elem mmap_elem;		// mmap 리스트 elem
	size_t offset;								// 파일 오프셋
	size_t read_bytes;						// 가상 페이지에 쓰여진 데이터 크기
	size_t zero_bytes;						// 0으로 채울 남은 페이지의 바이트
	size_t swap_slot;							// 스왑 슬롯
	struct hash_elem elem;				// 해시 테이블 elem
};

// mmaping 파일
struct mmap_file {
	int mapid;										// 맵핑된 아이디
	struct file* file;						// 맵핑된 파일
	struct list_elem elem;				// struct thread의 mmap_list의 원소
	struct list vme_list;					// mmap_file에 해당하는 vme원소들
};

// page 구조체
struct page {
	void *kaddr;									// 물리페이지의 시작주소
	struct vm_entry *vme;					// 물리페이지에 사상된 가상 주소의 vme
	struct thread *thread;				// 페이지를 사용중인 thread
	struct list_elem lru;					// 페이지를 관리하는 list의 원소로서
};

void vm_init(struct hash *vm);
bool insert_vme(struct hash *vm, struct vm_entry *vme); 
bool delete_vme(struct hash *vm, struct vm_entry *vme);
struct vm_entry *find_vme(void *vaddr);
void vm_destroy(struct hash *vm);
void vm_destroy_func(struct hash_elem *e, void *aux UNUSED);
bool load_file(void *kaddr, struct vm_entry *vme); 
struct page* alloc_page(enum palloc_flags flags);
void free_page(void *kaddr);
void __free_page(struct page *page);

#endif // _VM_PAGE_H_
