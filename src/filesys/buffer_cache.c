// buffer_cache.c

#include "filesys/buffer_cache.h"
#include "filesys/filesys.h"
#include <string.h>		// memcpy
#include "threads/malloc.h"

#define BUFFER_CACHE_ENTRY_NB 64 	// buffer cache의 엔트리 개수는 64개

void *p_buffer_cache;			// buffer cache를 가리키는 포인터. 동적 할당용
struct buffer_head buffer_head[BUFFER_CACHE_ENTRY_NB];	// buffer head 
int clock_hand;		// victim을 가리키는 시계바늘


// sector_idx를 검색, 데이터를 buffer에 저장
bool bc_read (block_sector_t sector_idx, void* buffer, 
							off_t bytes_read, int chunk_size, int sector_ofs) {
	bool success = false;			// 반환 용 값. 성공, 실패를 반환
	// sector_idx를 먼저 검색한다. 
	struct buffer_head *target = bc_lookup(sector_idx);
	// 결과가 없으면 버퍼 캐시에 해당 블록을 넣어야함.
	if(NULL == target) {
		// 블록을 넣을 곳을 마련함
		target = bc_select_victim();
		target->valid = true;
		target->dirty = false;
		target->sector = sector_idx;
		// 아래 block_read의 용법은 write->file_write->inode_write_at을 볼것
		// data를 버퍼에 읽음
		block_read(fs_device, sector_idx, target->data);
	}
	// 버퍼 캐시에 넣은 데이터를(이 함수에서 넣었든 원래 있었든) 함수의 두
	// 번째 인자인 buffer에 넣음
	memcpy(buffer + bytes_read, target->data + sector_ofs, chunk_size);
	// clock_bit세팅
	target->clock_bit = 1;
	success = true;
	return success;
}

// 위에 구현한 bc_read와 똑같음
bool bc_write (block_sector_t sector_idx, void* buffer, 
							 off_t bytes_written, int chunk_size, int sector_ofs) {
	bool success = false;			// 반환 용 값. 성공, 실패를 반환
	// sector_idx를 먼저 검색한다. 
	struct buffer_head *target = bc_lookup(sector_idx);
	// 결과가 없으면 버퍼 캐시에 해당 블록을 넣어야함.
	if(NULL == target) {
		// 블록을 넣을 곳을 마련함
		target = bc_select_victim();
		target->valid = true;
		target->dirty = false;
		target->sector = sector_idx;
		// 아래 block_read의 용법은 write->file_write->inode_write_at을 볼것
		// data를 버퍼에 읽음
		block_read(fs_device, sector_idx, target->data);
	}
	// 버퍼 캐시에 넣은 데이터를(이 함수에서 넣었든 원래 있었든) 함수의 두
	// 번째 인자인 buffer에 넣음
	memcpy(target->data + sector_ofs, buffer + bytes_written, chunk_size);
	// clock_bit세팅
	target->clock_bit = 1;
	// 이 함수를 불렀다는 것은 곧 dirty_bit가 true가 된다는 뜻
	target->dirty = true;		
	success = true;
	return success;
}

// buffer cache 초기화
void bc_init(void) {

	int i;			// for loop
	// buffer_cache 동적 할당 block하나는 512bytes
	p_buffer_cache = malloc(512 * BUFFER_CACHE_ENTRY_NB);

	// buffer_head를 초기화
	for(i = 0; i < BUFFER_CACHE_ENTRY_NB; i++) {
		lock_init(&buffer_head[i].lock);
		buffer_head[i].valid = false;
		buffer_head[i].dirty = false;
		buffer_head[i].clock_bit = 0;
		buffer_head[i].data = p_buffer_cache + i * 512;
	}
	// clock_hand 초기화. 0번 원소를 가리킴
	clock_hand = 0;
}


// 모든 dirty entry flush && buffer cache 해제
void bc_term(void) {
	bc_flush_all_entries();
	free(p_buffer_cache);
}

// victim선정후 victim의buffer_head를 반납
// victim은 dirty이면 flush
struct buffer_head* bc_select_victim(void) {
	// 아래는 clock_hand가 victim을 가리키게 함.
	// 안사용중인 친구가 있으면 그것을,
	// 전부 가득 차있으면 clock_bit가 0인 친구를
	while(true) {
		// 안사용중인 친구를 발견, 당첨!
		if(buffer_head[clock_hand].valid == false) {
			return &buffer_head[clock_hand];
		}
		// clock_bit가 1이면 0으로
		if(buffer_head[clock_hand].clock_bit == 1) {
			buffer_head[clock_hand].clock_bit = 0;
		}
		// clock_bit가 0이면 당첨!
		else {
			break;
		}
		clock_hand = (clock_hand + 1) % BUFFER_CACHE_ENTRY_NB;
	}

	// 찾은 victim을 flush 할 수도 있고, 안 할 수도 있습니다.
	if(buffer_head[clock_hand].dirty == true) {
		bc_flush_entry(&buffer_head[clock_hand]);
	}
	buffer_head[clock_hand].valid = false;
	buffer_head[clock_hand].dirty = false;

	return &buffer_head[clock_hand];
}



// 버퍼 캐시에 해당 섹터가 존재 하는지 검사
// 없으면 NULL, 있으면 해당 buffer_head의 주소값
struct buffer_head* bc_lookup(block_sector_t sector) {
	int i = 0;			// for loop
	struct buffer_head* ret = NULL;

	for(i = 0; i < BUFFER_CACHE_ENTRY_NB; i++) {
		// valid고, sector와 같으면
		if((sector == buffer_head[i].sector) && (true == buffer_head[i].valid)) {
			ret = &buffer_head[i];
			break;
		}
	}

	return ret;
}

// 해당 entry의 dirty를 false로세팅 후 disc로 flush
void bc_flush_entry(struct buffer_head *p_flush_entry) {
	lock_acquire(&p_flush_entry->lock);		// 락을 일단 걸음. 공유 자원이므로
	p_flush_entry->dirty = false;
	block_write(fs_device, p_flush_entry->sector, p_flush_entry->data);
	lock_release(&p_flush_entry->lock);
}

// dirty == true인 친구들 모두 flush
void bc_flush_all_entries(void)	{
	int i = 0;
	// 순회하면서 dirty면 flush
	for(i = 0; i < BUFFER_CACHE_ENTRY_NB; i++) {
		if(true == buffer_head[i].dirty) {
			lock_acquire(&buffer_head[i].lock);
			buffer_head[i].dirty = false;
			block_write(fs_device, buffer_head[i].sector, buffer_head[i].data);
			lock_release(&buffer_head[i].lock);
		}
	}
}
