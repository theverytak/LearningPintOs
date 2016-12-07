// buffer_cache.h
#ifndef _BUFFER_CACHE_H_
#define _BUFFER_CACHE_H_

#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/synch.h"  // lock을 위하여

bool bc_read (block_sector_t, void*, off_t, int, int);
bool bc_write (block_sector_t, void*, off_t, int, int);
struct buffer_head* bc_lookup(block_sector_t); // 버퍼 캐시에 해당 섹터가 
																							 // 있는지 검사
struct buffer_head* bc_select_victim(void); // victim선정후 victim의
																						// buffer_head를 반납
void bc_flush_entry(struct buffer_head *);  // 해당 entry의 dirty를 false로
																						// 세팅 후 disc로 flush
void bc_flush_all_entries(void);	// dirty == true인 친구들 모두 flush
void bc_init(void); // buffer cache 초기화
void bc_term(void); // 모든 dirty entry flush && buffer cache 해제

struct buffer_head {
	bool dirty;							// 변경 여부
	bool valid;							// 사용 여부
	block_sector_t sector;	// disc의 sector 번호
	bool clock_bit;					// clock알고리즘을 위해
	struct lock lock;				// 공유 자원을 위한 lock
	void* data;							// 내가 담당하는 buffer_cache의 주소가 들어있음
};

#endif //_BUFFER_CACHE_H_
