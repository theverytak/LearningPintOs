// swap.c

#include "vm/swap.h"

void swap_init (size_t size) {
	swap_block = block_get_role(BLOCK_SWAP);		// 스왑 블럭을 가져옴
	lock_init(&swap_lock);											// lock 초기화
	swap_bitmap = bitmap_create(size);					// bitmap생성
	bitmap_set_all(swap_bitmap, 0);							// 0으로 초기화
}

// swap block -> memory
void swap_in (size_t used_index, void *kaddr) {
	ASSERT(NULL != swap_block && NULL != swap_bitmap);
	int i;					// for loop
	lock_acquire(&swap_lock);
	// used_index의 bitmap이 0(미사용)이면 swapin을 할 수 없음
	if(0 == bitmap_test(swap_bitmap, used_index))
		exit(-1);
	bitmap_flip(swap_bitmap, used_index); // used_index의 bitmap을 0으로 변경

	// page당 블럭 수. 사실 4000/500해서 8임
	int blocks_in_a_page = PGSIZE / BLOCK_SECTOR_SIZE;
	// 이제 disc의 스왑 블럭을 메모리로 읽음
	for(i = 0; i < blocks_in_a_page; i++) {
		block_read(swap_block, used_index * blocks_in_a_page + i,
							 kaddr + BLOCK_SECTOR_SIZE * i);
	}

	lock_release(&swap_lock);
}

// memory -> swap block
// swap slot의 인덱스를 리턴
size_t swap_out (void *kaddr) {
	ASSERT(NULL != swap_block && NULL != swap_bitmap);
	int i;					// for loop
	lock_acquire(&swap_lock);
	// 안사용중인 bitmap 1개 얻음
	size_t swap_index = bitmap_scan_and_flip(swap_bitmap, 0, 1, 0);
	if(BITMAP_ERROR == swap_index) // bitmap_scan_and_flip중 에러발생시
		return BITMAP_ERROR;

	// page당 블럭 수. 사실 4000/500해서 8임
	int blocks_in_a_page = PGSIZE / BLOCK_SECTOR_SIZE;
	// 메모리에서 스왑 블럭으로
	for(i = 0; i < blocks_in_a_page; i++) {
		block_write(swap_block, swap_index * blocks_in_a_page + i,
								kaddr + BLOCK_SECTOR_SIZE * i);
	}

	lock_release(&swap_lock);
	return swap_index;
}
	



