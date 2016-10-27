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

void vm_init(struct hash *vm);
bool insert_vme(struct hash *vm, struct vm_entry *vme); 
bool delete_vme(struct hash *vm, struct vm_entry *vme);
struct vm_entry *find_vme(void *vaddr);
void vm_destroy(struct hash *vm);
void vm_destroy_func(struct hash_elem *e, void *aux UNUSED);
bool load_file(void *kaddr, struct vm_entry *vme); 

#endif // _VM_PAGE_H_
