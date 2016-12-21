#include "threads/thread.h"		// thread_current()
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);


/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

	bc_init();
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();

	// filesys초기화 시 현재 디렉터리를 루트로 설정
	thread_current()->cur_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
	bc_term();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
	// cp_name, file_name(실제 생성할 것)할당과 에러 체크
	char *cp_name = palloc_get_page(0);
	char *file_name = palloc_get_page(0);
	if(NULL == cp_name || NULL == file_name)
		return false;

	// 이제 cp_name은 경로를 담고있음
	strlcpy(cp_name, name, PGSIZE);
	// 경로 분석
  struct dir *dir = parse_path(cp_name, file_name);
	bool success = false;		// 본 함수의 반환값
  success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

	palloc_free_page(cp_name);
	palloc_free_page(file_name);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
	// cp_name, file_name(실제 생성할 것)할당과 에러 체크
	char *cp_name = palloc_get_page(0);
	char *file_name = palloc_get_page(0);
	if(NULL == cp_name || NULL == file_name)
		return false;

	strlcpy(cp_name, name, PGSIZE);		// 이제 cp_name은 경로를 담음
	// 경로 분석
  struct dir *dir = parse_path(cp_name, file_name);
	if(NULL == dir)
		return NULL;
  struct inode *inode = NULL;
	dir_lookup (dir, file_name, &inode);
  dir_close (dir);
	palloc_free_page(cp_name);
	palloc_free_page(file_name);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
	// cp_name, file_name(실제 생성할 것)할당과 에러 체크
	char *cp_name = palloc_get_page(0);
	char *file_name = palloc_get_page(0);
	if(NULL == cp_name || NULL == file_name)
		return false;

	strlcpy(cp_name, name, PGSIZE);		// 이제 cp_name은 경로를 담음
  struct dir *dir = parse_path(cp_name, file_name);
	
	bool success = false;		// 본 함수의 반환값
	struct inode *inode;

	// dir에서 file_name을 찾음 그리고 그 파일의 inode는 inode에 저장
	if(NULL != file_name && dir_lookup(dir, file_name, &inode)) {
		if(false == inode_is_dir(inode)) {		// 파일을 삭제
			// 아래 dir_remove는 file_name이 dir에 존재한다면 지움
			success = dir_remove(dir, file_name);
		}
		else { // 디렉터리를 삭제
			struct dir *target_dir = dir_open(inode);
			// target이 비어있는 경우에만 지울 수 있음
			if(false == dir_readdir(target_dir, file_name))
				success = dir_remove(dir, file_name);
		}
	}

  dir_close (dir); 
	palloc_free_page(cp_name);
	palloc_free_page(file_name);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
	// root열고, . .. 추가하고 닫고
	struct dir *root = dir_open_root();
	dir_add(root, ".", ROOT_DIR_SECTOR);
	dir_add(root, "..", ROOT_DIR_SECTOR);
	dir_close(root);
  printf ("done.\n");
}

// path_name은 경로 file_name은 파싱 후 저장될 파일, 혹은 디렉터리의 이름
// path_name이 "/"로 시작하는지에 따라 절대 / 상대 경로 파악
// dir을 리턴함
// 아래에 달린 주석 중 상당수는 수업자료에 있는 것을 그대로 적었음
struct dir *parse_path (char *path_name, char *file_name) {
	struct inode *inode = NULL;
	// 디렉터리 정보를 가리킬 포인터
	struct dir *dir = NULL;
	// 아래 예외처리는 수업 자료에 나와있는 것을 그대로 적은 것임
	if(NULL == path_name || NULL == file_name)
		return NULL;
	if(0 == strlen(path_name))
		return NULL;

	// 절대 경로인지 상대 경로인지에 따라 dir이 달라짐
	if('/' == path_name[0])
		dir = dir_open_root();
	else
		dir = dir_reopen(thread_current()->cur_dir);
	// 위 dir 대입이 실패한 경우
	if(NULL == dir)
		return NULL;

	
	char *token, *nextToken, *saveptr;
	token = strtok_r(path_name, "/", &saveptr);
	nextToken = strtok_r(NULL, "/", &saveptr);

	if(NULL == token) {		// path_name이 "/"인 경우
		strlcpy(file_name, ".", 2);
		return dir;
	}

	while(NULL != token && NULL != nextToken) {
		// dir에서 token이름을 검색하여 inode정보를 저장
		// 검색 실패하면 NULL리턴
		if(false == dir_lookup(dir, token, &inode)) {
			dir_close(dir);
			return NULL;
		}
		// inode가 파일인 경우 NULL 반환
		if(false == inode_is_dir(inode)) {
			dir_close(dir);
			return NULL;
		}
		// dir의 디렉터리 정보를 메모리에서 해지
		dir_close(dir);
		// inode의 디렉터리 정보를 dir에 저장
		dir = dir_open(inode);
		// token에 검색할 경로 이름을 저장
		token = nextToken;
		nextToken = strtok_r(NULL, "/", &saveptr);
	}

	// token의 파일 이름을 file_name에 저장(인자로 받은 것)
	strlcpy(file_name, token, strlen(token) + 1);
	// dir정보를 반환
	return dir;
}

// 해당 경로에 디렉터리를 하나 만듬.
bool filesys_create_dir (const char *name) {
	block_sector_t inode_sector = 0;
	// cp_name, file_name(실제 생성할 것)할당과 에러 체크
	char *cp_name = palloc_get_page(0);
	char *file_name = palloc_get_page(0);
	if(NULL == cp_name || NULL == file_name)
		return false;

	// 이제 cp_name은 경로를 담고있음
	strlcpy(cp_name, name, PGSIZE);
	struct dir *dir = parse_path(cp_name, file_name);

	// 아래는 filesys_create와 같음
	// inode_create대신 dir_create를 사용함
	bool success = (dir != NULL
									&& free_map_allocate (1, &inode_sector)
									&& dir_create (inode_sector, 16)
									&& dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

	// 성공했다면 이제 .와 ..를 엔트리에 추가
	if(success) {
		struct dir *new_dir = dir_open(inode_open(inode_sector));
		dir_add(new_dir, ".", inode_sector);
		dir_add(new_dir, "..", inode_get_inumber(dir_get_inode(dir)));
		dir_close(new_dir);
	}

	dir_close(dir);
	palloc_free_page(cp_name);
	palloc_free_page(file_name);

	return success;
}
