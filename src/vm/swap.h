// swap.h
#ifndef _VM_SWAP_H_
#define _VM_SWAP_H_

#include "threads/synch.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include <bitmap.h>
#include <debug.h>


void swap_init(size_t size);
void swap_in(size_t used_index, void *kaddr);
size_t swap_out(void *kaddr);

// 아래는 전역변수들
struct lock swap_lock;				// 아래를 위한 lock
struct bitmap *swap_bitmap;   // 비트맵 배열
struct block *swap_block;			// 스왑 블럭을 가리킬 포인터


#endif // _VM_SWAP_H_ 

