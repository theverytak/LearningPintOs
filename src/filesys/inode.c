#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/buffer_cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"		// lock

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
// BLOCK_SECTOR_SIZE / sizeof(block_secotr_t) == 512 / 4
#define INDIRECT_BLOCK_ENTRIES 128
// inode_disk 구조체의 크기가 1블럭이 되도록 하는 값
#define DIRECT_BLOCK_ENTRIES 123

// inode가 블럭을 저장하는 방식
enum direct_t {
	NORMAL_DIRECT,				// 0단계
	INDIRECT,						// 1단계
	DOUBLE_INDIRECT,		// 2단계
	OUT_LIMIT 					// 잘못된 파일 오프셋
};

struct sector_location {
	int directness;			// 접근 방법, 직접, 1단계 2단계이런것
	int index1;					// index block용 인덱스
	int index2;					// 2번 index block용 인덱스
};

// index block을 위한 구조체
struct inode_indirect_block {
	block_sector_t map_table[INDIRECT_BLOCK_ENTRIES];
};


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
// 아래 자료구조는 extendible file과제를 수행하며 변경됨
// 자료 구조의 크기는 항상 블럭 사이즈이며
// 그를 위해 DIRECT_BLOCK_ENTRIES는 바뀔 수 있음
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
		uint32_t is_dir;										// dir이면 1 아니면 0
		block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];	// 0단계
		block_sector_t indirect_block_sec;											// 1단계
		block_sector_t double_indirect_block_sec;								// 2단계
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
		struct lock extend_lock;						// 아이노드 관련 데이터를 위한 자물쇠
  };


// 아래는 extendible files과제를 위해 추가한 함수들
static bool get_disk_inode(const struct inode *, struct inode_disk *);
static void locate_byte(off_t, struct sector_location *);
static bool register_sector(struct inode_disk *, block_sector_t, struct sector_location);
static bool inode_update_file_length(struct inode_disk *, off_t, off_t);
static void free_inode_sectors(struct inode_disk *);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
// pos로 inode_disk를 검색하여 sector번호를 리턴
// pos를 잘못 전달 받았으면 -1을 리턴함
// pos는 byte단위 offset
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  ASSERT (inode_disk != NULL);
	block_sector_t result_sec;			// 리턴할 sector번호

	if(pos < inode_disk->length) {
		struct inode_indirect_block index_block;		// 1차
		struct inode_indirect_block index_block2;		// 2차
		struct sector_location sec_loc;
		locate_byte(pos, &sec_loc);		// 인덱스 블록의 offset계산

		switch(sec_loc.directness) {
			// direct방식의 경우 그냥 얻어오면 됨
			case NORMAL_DIRECT :
				result_sec = inode_disk->direct_map_table[sec_loc.index1];
				break;

			case INDIRECT :
				// ind_block에 index block의 값을 읽고, 거기서 찾아서 리턴
				bc_read(inode_disk->indirect_block_sec,
							 &index_block, 0, BLOCK_SECTOR_SIZE, 0);
				result_sec = index_block.map_table[sec_loc.index1];
				break;

			case DOUBLE_INDIRECT :
				// ind_block에 index_block을 읽음
				bc_read(inode_disk->indirect_block_sec,
							 &index_block, 0, BLOCK_SECTOR_SIZE, 0);
				// ind_block2에 2번째 index_block을 읽음
				bc_read(index_block.map_table[sec_loc.index1],
							 &index_block2, 0, BLOCK_SECTOR_SIZE, 0);
				result_sec = index_block2.map_table[sec_loc.index2];
				break;
			default :
				return -1;		// directness값의 오류
		}		// end of switch
	} //  end of if
	else {	// 잘못된 pos입력
		return -1;
	}
	return result_sec;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      //size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
			disk_inode->is_dir = is_dir;		// is_dir초기화
			// lenght만큼 하나 만들고, bc_write로 기록
			if(length > 0) {
				// 아래 함수의 2, 3번째 인자는 항상 [,]임이 보장되어야함
				// 그러므로 length - 1을 전달해줌
				inode_update_file_length(disk_inode, 0, length - 1);
			}
			bc_write(sector, (void*)disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
			success = true;
    }
	free(disk_inode);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
	lock_init(&inode->extend_lock);
  //block_read (fs_device, inode->sector, &inode->data); extendedfile하며 주석
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
					struct inode_disk disk_inode;
					// on disk inode를 읽어옴
					get_disk_inode(inode, &disk_inode);
					//on disk inode에할당된 모든 블럭을 해제 
					free_inode_sectors(&disk_inode);			
					// on disk inode 그 자체를 해제
					free_map_release(inode->sector, 1);
        }
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
	// 인자로 받은 inode에 대한 on disk inode를 읽어옴
	struct inode_disk disk_inode;
	get_disk_inode(inode, &disk_inode);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

			bc_read(sector_idx, (void *)buffer, bytes_read, chunk_size, sector_ofs);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
	// 잠시 빌려서 읽었으므로, 원래 값도 업데이트 해줌
	bc_write(inode->sector, (void *)&disk_inode, 0, BLOCK_SECTOR_SIZE, 0);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
*/
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
	struct inode_disk disk_inode;
	disk_inode.length = 0;

  if (inode->deny_write_cnt)
    return 0;

	// inode lock 획득
	lock_acquire(&inode->extend_lock);
	// 인자로 받은 inode에 대한 on disk inode를 읽어옴
	get_disk_inode(inode, &disk_inode);

	int old_length = disk_inode.length;			// on disk inode에 저장된 기존 파일 길이
	int write_end = offset + size - 1;			// 새로운 파일의 끝(파일의 길이 - 1)
	if(write_end > old_length - 1) {				// 파일의 길이가 길어진다면
		// inode업데이트
		inode_update_file_length(&disk_inode, old_length, write_end);
		bc_write(inode->sector, (void *)&disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
	}
	// lock 해제
	lock_release(&inode->extend_lock);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode.length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

			bc_write(sector_idx, (void *)buffer, bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
// extendible file이후 inode에는 data 필드가 없어져서 아래처럼 바꿈
off_t
inode_length (const struct inode *inode)
{
	struct inode_disk disk_inode;
	bc_read(inode->sector, &disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
  return disk_inode.length;
}

// 디스크 아이노드를 읽어주는 함수. 
// 첫번째 인자로 받은 in memory inode의 sector필드가 가리키는 곳에
// 있는 inode_disk를 읽음
static bool get_disk_inode(const struct inode *inode, 
													 struct inode_disk *inode_disk) {
	return bc_read(inode->sector, inode_disk, 0, 
								 sizeof(struct inode_disk), 0);
}

// 디스크 블록 접근 방법(direct냐 indirect냐 double-indirect냐)결정
// 그 결과를 sec_loc에 저장(접근 방법, 접근 index들)
static void locate_byte(off_t pos, struct sector_location *sec_loc) {
	off_t pos_sector = pos / BLOCK_SECTOR_SIZE;	// 바이트 단위 -> 블럭 단위

	// 0단계에서 끝날 지 검사
	if(pos_sector < DIRECT_BLOCK_ENTRIES) {
		// 0단계라고 기록, index1에 저장
		sec_loc->directness = NORMAL_DIRECT;
		sec_loc->index1 = pos_sector;
	}
	// 1단계 이면...
	else if(pos_sector < (off_t)DIRECT_BLOCK_ENTRIES + 
											  INDIRECT_BLOCK_ENTRIES) {
		// 1단계라고 기록, index1에 저장
		sec_loc->directness = INDIRECT;
		sec_loc->index1 = pos_sector - DIRECT_BLOCK_ENTRIES;
	}
	// 2단계 이면...
	else if(pos_sector < (off_t)DIRECT_BLOCK_ENTRIES + 
											  INDIRECT_BLOCK_ENTRIES * (INDIRECT_BLOCK_ENTRIES + 1)) {
		// 1단계라고 기록, index1, index2에 모두 저장
		sec_loc->directness = DOUBLE_INDIRECT;
		sec_loc->index1 = (pos_sector - (DIRECT_BLOCK_ENTRIES +
																		 INDIRECT_BLOCK_ENTRIES)) /
																		 INDIRECT_BLOCK_ENTRIES;
		sec_loc->index2 = (pos_sector - (DIRECT_BLOCK_ENTRIES +
																		 INDIRECT_BLOCK_ENTRIES)) %
																		 INDIRECT_BLOCK_ENTRIES;
	}
	// 잘못된 pos의 입력...
	else {
		sec_loc->directness = OUT_LIMIT;
	}
}

// 인자로 받은 inode_disk에 new_sector를 update
// indirect block을 처음 사용하는 경우 블럭을 만들기도 함
static bool register_sector(struct inode_disk *inode_disk, 
														block_sector_t new_sector, 
														struct sector_location sec_loc) {
	block_sector_t index_table_sec;			// 생성한 index_table의 섹터 번호
	block_sector_t index_table_sec2;		// double_indirect block을 위해 하나 더
	struct inode_indirect_block block1, block2;	// index_table 1, 2

	// directness에 따라 블럭을 배정
	switch(sec_loc.directness) {
		// 그냥 해당 블럭에 넣으면 끝
		case NORMAL_DIRECT :
			inode_disk->direct_map_table[sec_loc.index1] = new_sector;
			break;

		case INDIRECT :
			// 아직 한 번도 indirect 블럭을 사용한 적이 없으면 만들어 줘야함
			if(inode_disk->indirect_block_sec == 0) {
				// indirect index block을 하나 할당
				// 그 섹터 번호는 index_table에 들어감
				if(true == free_map_allocate (1, &index_table_sec)) {
					inode_disk->indirect_block_sec = index_table_sec;
				}
				else {
					// 실패하면 리턴 false;
					return false;
				}
			}
			// indirect_index_block은 만들어진 상태.
			// 아래는 해당 블럭을 반영하는 과정인데, 블럭 단위로 읽고 씀
			// 해당 인덱스 블럭의 값을 읽고
			bc_read(inode_disk->indirect_block_sec,  
						 &block1, 0, BLOCK_SECTOR_SIZE, 0);
			// on disk inode에 그 값을 반영
			block1.map_table[sec_loc.index1] = new_sector;
			bc_write(inode_disk->indirect_block_sec,
							&block1, 0, BLOCK_SECTOR_SIZE, 0);
			break;

		case DOUBLE_INDIRECT :
			// 아직 한 번도 double indirect를 쓴 적이 없으면 우선 만들어야함
			if(inode_disk->double_indirect_block_sec == 0) {
				if(false == free_map_allocate(1, &index_table_sec)) {
					inode_disk->double_indirect_block_sec = index_table_sec;
				}
				else {
					return false;
				}
			}
			// 1st index block은 만들어짐
			// 1st index block값을 읽음
			bc_read(inode_disk->double_indirect_block_sec, 
						 &block1, 0, BLOCK_SECTOR_SIZE, 0);

			// 2nd index block이 없으면 만들어야함
			if(block1.map_table[sec_loc.index1] == 0) {
				if(false == free_map_allocate(1, &index_table_sec2)) {
					block1.map_table[sec_loc.index1] = index_table_sec2;
					bc_write(index_table_sec2, &block1, 0, BLOCK_SECTOR_SIZE, 0);
				}
				else {
					return false;
				}
			}
			// 두 개의 index block이 모두 만들어 졌음

			// 2nd index block값을 읽음
			bc_read(block1.map_table[sec_loc.index1],
						 &block2, 0, BLOCK_SECTOR_SIZE, 0);

			// 갱신하고(sec_loc.index2에 new_sector를 넣고)
			block2.map_table[sec_loc.index2] = new_sector;
			// 다시 쓰면 됨. 
			bc_write(block1.map_table[sec_loc.index1], 
							&block2, 0, BLOCK_SECTOR_SIZE, 0);
			break;

		default :
			return false;		// sec_loc.directness가 이상한 경우
	}

	return true;
}

// 기존 파일크기보다 커질 경우 파일을 위해 새로운 디스크 블럭을 할당받음.
// 파일의 inode, 커져야하는 시작 오프셋, 커지고 그 끝의 오프셋
// 이렇게 인자로 받음.
// [start_pos, end_pos]인 구간임
// 위의 닫힌 구간에 대한 조건이 지켜지지 않으면 우울해 질 수 있음
static bool inode_update_file_length(struct inode_disk *inode_disk, 
																		 off_t start_pos, off_t end_pos) {
	block_sector_t sector_index;		// 현재 작업중인 블럭의 sector 번호
	// 새로 할당한 블럭은 0으로 채워야 하기 때문에
	char *zeroes = calloc(BLOCK_SECTOR_SIZE, sizeof(char)); 
	off_t size = end_pos - start_pos + 1;	// 증가할 사이즈
	off_t offset = start_pos;		// 파일의 현재 offset 
	struct sector_location sec_loc;
	sec_loc.index1 = sec_loc.index2 = 0;	// 명시적으로 일단 초기화 디버깅과정
	inode_disk->length = end_pos + 1;					// inode_disk의 길이 업데이트

	// 아래 while문에서는 size를 블럭 단위로 할당을 하며 줄인다
	while(size > 0) {
		// 지금 작업중인 블럭의 offset
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		// 아래의 chunk_size는 size, offset변수의 업데이트를 위한 것
		// 예를들어, 현재 블럭에 500 만큼 쓰여있으면, chunk_size는
		// 12가 됨.
		// 예를 들어, 현재 블럭에 0만큼 쓰여있으면(새로 할당해야함),
		// chunk_size는 512가 됨.
		int chunk_size = BLOCK_SECTOR_SIZE - sector_ofs;
		if(sector_ofs > 0) {
			// 이미 할당된 블럭. 아무 일도 하지 않음.
			// 밑에서 while문 관련 index는 증가시킬 것임
		}
		else {	// 새로운 블럭을 할당해야할 필요가 있음. sector_ofs이 0이기 때문에
			if(true == free_map_allocate(1, &sector_index)) {
				// 블럭은 만들었는데 이제 이것을 on disk inode에 넣어야함
				locate_byte(offset, &sec_loc);
				register_sector(inode_disk, sector_index, sec_loc);
			}
			else {	// 위의 free_map_allocate에서 실패한 경우
				free(zeroes);
				return false;
			}
			// 여기 까지 왔다면, 새로운 블럭을 만들었고, inode_disk에
			// 그 블럭 번호도 들어갔다. 이제 해당 블럭을 0으로 초기화 해주면 됨.
			bc_write(sector_index, zeroes, 0, BLOCK_SECTOR_SIZE, 0);
		}
		// 변수 update
		size -= chunk_size;
		offset += chunk_size;
	} // end of while

	free(zeroes);		// 잘 쓴 zeores를 이제 놓아줍니다
	return true;
}


// 인자로 받은 inode_disk에 할당된 모든 블럭을 해제
static void free_inode_sectors(struct inode_disk *inode_disk) {
	int i, j;			// 반복 제어 변수
	// double indirect부터
	if(inode_disk->double_indirect_block_sec > 0) {	// 0보다 크면 존재
		struct inode_indirect_block block1;		// 1st index block
		struct inode_indirect_block block2;		// 2st index block
		// 1st index block을 읽음
		bc_read(inode_disk->double_indirect_block_sec,
					 &block1, 0, BLOCK_SECTOR_SIZE, 0);
		for(i = 0; i < INDIRECT_BLOCK_ENTRIES; i++) {
			// 사용된 적이 없는 엔트리가 발견되면, 그 이후로도 쭉 사용된 적 없으니
			// 바로 break
			if(block1.map_table[i] == 0)		
				break;
			// 1st index block의 각 엔트리 마다의 2nd index블럭에 접근해야함
			bc_read(block1.map_table[i],
					   &block2, 0, BLOCK_SECTOR_SIZE, 0);
			// 2nd index블럭에 연결된 실제 블럭들을 해제
			for(j = 0; j < INDIRECT_BLOCK_ENTRIES; j++) {
				// 역시 사용된 적이 없는 엔트리 발견시 바로 break; 
				if(block2.map_table[j] == 0)
					break;
				// 실제 disk block 해제
				free_map_release(block2.map_table[j], 1);
			}
			// 2nd index_block제거
			free_map_release(block1.map_table[i], 1);
		}
		// 1st index_block제거
		free_map_release(inode_disk->double_indirect_block_sec, 1);
	}

	// indirect 해제
	if(inode_disk->indirect_block_sec > 0) { // 0보다 크면 존재
		struct inode_indirect_block block1;
		// 1st index block을 읽음
		bc_read(inode_disk->indirect_block_sec,
					 &block1, 0, BLOCK_SECTOR_SIZE, 0);
		for(i = 0; i < INDIRECT_BLOCK_ENTRIES; i++) {
			// 사용된 적이 없는 엔트리 발견시 바로 break
			if(block1.map_table[i] == 0)
				break;
			// 실제 disc block제거
			free_map_release(block1.map_table[i], 1);
		}
		// 1st index_block 제거
		free_map_release(inode_disk->indirect_block_sec, 1);
	}
	
	// direct해제
	// 123개의 direct block을 순회하며 해제
	for(i = 0; i < DIRECT_BLOCK_ENTRIES; i++) {
		// 이것도 역시 0인거 발견하는 순간 break;
		if(inode_disk->direct_map_table[i] == 0)
			break;
		free_map_release(inode_disk->direct_map_table[i], 1);
	}
}

// 인자로 받은 inode에 해당하는 파일이 dir인가 아닌가
bool inode_is_dir (const struct inode *inode) {
	bool result;
	struct inode_disk disk_inode;

	// on disk inode를 읽어옴
	get_disk_inode(inode, &disk_inode);
	result = disk_inode.is_dir;

	return result;
}
