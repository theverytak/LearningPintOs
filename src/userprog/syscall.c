#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/thread.h"   // thread_exit()
#include "devices/shutdown.h" // shutdown_power_off()
#include "devices/input.h"		// input_getc()
#include "filesys/filesys.h"  // filesys_create(), remove()
#include "filesys/file.h"			// file_close(), file_read(), file_write(), 
															//file_seek(), file_tell(), file_length()
#include "userprog/process.h" // process_execute(), process_wait()
#include "vm/page.h"


static void syscall_handler (struct intr_frame *f UNUSED);


// 아래는 구현한 시스템컬의 목록임.
void halt(void);
void exit(int status);
int wait(tid_t tid);
bool create(const char *file , unsigned initial_size);
bool remove(const char *file);
tid_t exec(const char *cmd_line);
int open(const char *file); 
int filesize(int fd); 
int read(int fd, void *buffer, unsigned size); 
int write(int fd, void *buffer, unsigned size); 
void seek(int fd, unsigned position); 
unsigned tell(int fd);
void close(int fd);
int mmap(int fd, void *addr);
void munmap(int mapid);
void do_munmap(struct mmap_file *mmp_f); 

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	// lock 초기화
	lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	/* 
	 유저 스택에 저장되어 있는 시스템 콜 넘버를 이용해 시스템 콜 핸들러 구현
	 스택 포인터가 유저 영역인지 확인
	 저장된 인자 값이 포인터일 경우 유저 영역의 주소인지 확인
	*/
	uint32_t *esp = f->esp;		// esp 복사
	int sys_n = *(int *)(f->esp); 	  // system call number. esp가 가리키는 곳에 있음.
	int arg[4];						// 인자를 넣을 배열. 최대 인자 갯수는 4개임.

	check_address(esp, esp);

	/*  ****************************************************************
		아래는 시스템컬 핸들러 테이블 없이 시스템컬 넘버에 따라 알맞은 시스템
		콜을 불러오는 코드입니다.
		어떤 시스템컬이 호출되었는지 잘 알 수 있도록 printf구문을 모든 case의 
		시작에 넣었습니다.
		대부분 메커니즘은 비슷합니다. 
		check_address는 인자로 넘어온 포인터의 주소값이 유효한지 검사하며,
		시스템컬에 리턴값이 있다면 그 리턴값은 f->eax에 넣어줍니다.
		 **************************************************************** */
	switch(sys_n)
	{
		case SYS_HALT :                   /* Halt the operating system. */
			{
				//printf("halt() called\n");
				halt();
			}
		case SYS_EXIT :                  /* Terminate this process. */
			{
				//printf("exit() called\n");
				get_argument(esp, arg, 1);
				exit(arg[0]);
				break;
			}
		case SYS_EXEC :                  /* Start another process. */
			{
				//printf("exec() called\n");
				get_argument(esp, arg, 1);
				//아래는 주석처리됨
				//check_address((void *)arg[0]);		// arg[0]이 유효한 주소영역인지 검사.
				check_valid_string((void *)arg[0], esp);
				f->eax = exec((const char *)arg[0]);
				break;
			}
		case SYS_WAIT :                  /* Wait for a child process to die. */
			{
				//printf("wait() called\n");
				get_argument(esp, arg, 1);
				f->eax = wait((tid_t)arg[0]);
				break;
			}
		case SYS_CREATE :                /* Create a file. */
			{
				//printf("create() called\n");
				get_argument(esp, arg, 2);
				//check_address((void *)arg[0]);		// arg[0]이 유효한 주소영역인지 검사.
				check_valid_string((void *)arg[0], esp);
				f->eax = create((const char *)arg[0], arg[1]);
				break;
			}
		case SYS_REMOVE :                /* Delete a file. */
			{
				//printf("remove() called\n");
				get_argument(esp, arg, 1);
				//check_address((void *)arg[0]);	//  arg[0]이 유효한 주소영역인지 검사.
				check_valid_string((void *)arg[0], esp);
				f->eax = remove((const char *)arg[0]);
				break;
			}
     case SYS_OPEN :                 /* Open a file. */
			{
				//printf("open() called\n");
				get_argument(esp, arg, 1);
				//check_address((void *)arg[0]);
				check_valid_string((void *)arg[0], esp);
				f->eax = open((const char *)arg[0]);
				break;
			}
     case SYS_FILESIZE :             /* Obtain a file's size. */
			{
				//printf("filesize() called\n");
				get_argument(esp, arg, 1);
				f->eax  = filesize(arg[0]);
				break;
			}
     case SYS_READ :                 /* Read from a file. */
			{
				//printf("read() called\n");
				get_argument(esp, arg, 3);
				//check_address((void *)arg[1]);  아래 함수로 대체
				check_valid_buffer((void *)arg[1], arg[2], esp, true); 
				f->eax = read(arg[0], (void *)arg[1], arg[2]);
				break;
			}
     case SYS_WRITE :                /* Write to a file. */
			{
				//printf("write() called\n");
				get_argument(esp, arg, 3);
				//check_address((void *)arg[1]);
				check_valid_buffer((void *)arg[1], arg[2], esp, false); 
				f->eax = write(arg[0], (void *)arg[1], arg[2]);
				break;
			}
     case SYS_SEEK :                 /* Change position in a file. */
			{
				//printf("seek() called\n");
				get_argument(esp, arg, 2);
				seek(arg[0], arg[1]);
				break;
			}
     case SYS_TELL :                 /* Report current position in a file. */
			{
				//printf("tell() called\n");
				get_argument(esp, arg, 1);
				f->eax = tell(arg[0]); 
				break;
			}
     case SYS_CLOSE :                /* Close a file. */
			{
				//printf("close() called\n");
				get_argument(esp, arg, 1);
				close(arg[0]);
				break;
			}
		 case SYS_MMAP :
			{
				get_argument(esp, arg, 2);
				f->eax = mmap(arg[0], (void *)arg[1]);
				break;
			}
		 case SYS_MUNMAP :
			{
				get_argument(esp, arg, 1);
				munmap(arg[0]);
				break;
			}
	}

//  printf ("system call!\n");
//  thread_exit ();
}

/* addr이 유효한 주소인지 확인.0x804800에서 0x0000000사이이면 유저영역임.
 경계는 제외.
 esp를 검사할 때 뿐만 아니라 예를들어 인자로 포인터를 받아온 경우에도
 아래 함수를 사용가능. */
// + vm_entry를 리턴하도록 변경됨.
 
struct vm_entry *check_address(void *addr, void* esp /*Unused*/) 
{
	if((uint32_t)addr < 0x8048000 || (uint32_t)addr >= 0xc0000000) 
		exit(-1);

	return find_vme(addr);
}


// 유저 스택에 저장된 인자값들을 arg에 복사.
// 참고로 핀토스 시스템콜의 최대 인자 수는 3개. read, write의 인자가 3개이다.
// 그러므로, count가 3보다 작거나 같다고  생각하고 만듬.
void 
get_argument(void *esp, int *arg, int count)
{
	int i;		// for loop;
	// 스택에 있는 인자들을 가리킴. 현재 esp는 system call number이므로
	// +4을 해서 다음 주소를 가리키게 한다.
	/* *****************************************
		 잠깐 여기서 팁
		 void *는 다음 주소를 +4하는 이유는 우리가 int배열인 arg를 가리키
		 게 하고 싶기 때문이다. 만약 여기서 그냥 +4대신 ++해버리면 이상해진다
		 *********************************************************** */
	void *ptr = esp + 4;		
	
	ASSERT(count >= 0 && count <= 3);   // count의 범위를 확인.

	// 유저 스택에 저장된 인자값들을 커널에 복사
	// 인자가 저장된 위치가 유저영역인지 확인
	for(i = 0; i < count; i++) {
		check_address(ptr, ptr);
		arg[i] = *(int *)(ptr);   
		//printf("arg[%d] = %d\n", i, arg[i]);
		ptr += 4;
	}
}


/* ***************************************
 여기서부터는 구현된 시스템컬을 목록입니다. 
 **************************************** */
void 
halt()
{
	shutdown_power_off();
}

void 
exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf ("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

bool 
create(const char *file , unsigned initial_size)
{
	return filesys_create(file, initial_size);
}

bool
remove(const char *file)
{
	return filesys_remove(file);
}

tid_t 
exec(const char *cmd_line)
{
	/* cmd_line을 수행하는 새로운 프로세스를 생성한다.
		 해당 프로세스는 자식 프로세스이며, thread* t는
		 그 스레드를 가리킨다. */
  struct thread *t = get_child_process(process_execute(cmd_line));
  // 자식이 load할 때 까지 기다림.
	// sema_up은 자식의 load함수 수행 후 할 예정임.
  sema_down(&t->sema_load); 

	// 자식이 load에 성공했다면, 자식의 tid를 리턴
	if(t->is_loaded)
		return t->tid;
	// 실패했다면 슬프지만 -1리턴.
	else
		return -1;
}

int 
wait(tid_t tid)
{
	return process_wait(tid);
}

// 파일을 열 때 사용하는 시스템 컬
int 
open(const char *file) {
	// open_target은 file이라는 이름으로 열고 싶은 파일
	struct file *open_target;
	int result;
	lock_acquire(&filesys_lock);
	open_target = filesys_open(file);

	// 실패시 -1리턴
	if(NULL == open_target) {
		lock_release(&filesys_lock);
		return -1;
	}

	result = process_add_file(open_target);
	lock_release(&filesys_lock);
		
	// 오픈 성공했으면 open_target을 fdt에 넣고 그 인덱스 리턴
	return result;
}

// 파일의 크기를 리턴. 대부분의 역할은 file_length가 하므로
// 자세한 사항을 알고싶다면 file_length를 읽으십시오.
int
filesize(int fd) {
	// fd번째 파일객체를 가져옴
	struct file *f = process_get_file(fd);
	// 파일 가져오기 실패시 -1리턴
	if(NULL == f)
		return -1;

	// 성공했으면 그 파일의 크기를 리턴
	return file_length(f);
}

// fd번째 파일객체를 열어서 buffer에 size만큼 저장함.
int
read(int fd, void *buffer, unsigned size) {

	struct file *read_target;		// read할 파일 객체
	off_t bytes_read;						// 읽어들인 바이트 수.
	unsigned i;											// 반복제어변수
	
	lock_acquire(&filesys_lock);	// 동시 접근을 막기 위해 lock사용

	// fd가 1이거나 0보다 작으면 헛소리이기 때문에 -1반환
	if(1 == fd || fd < 0) {
		lock_release(&filesys_lock);
		return -1;
	}

	// fd가 0이면 키보드의 입력을 받는다. 일반적 경우와 달리 처리해주어야함.
	// 여기서는 input_getc함수를 사용합니다.
	if(STDIN_FILENO == fd) {
		for(i = 0; i < size; i++) {
			((char *)buffer)[i] = input_getc();
		}
		
		lock_release(&filesys_lock);
		return i;			// i번 만큼 읽었기 때문에 i를 리턴
	}
	// fd가 0, 1모두 아니면 파일객체 탐색 후 실패했으면 -1리턴
	read_target = process_get_file(fd);
	if(NULL == read_target) {
		lock_release(&filesys_lock);
		return -1;
	}

	// 성공했으면 file_read를 이용해 파일을 읽고, 읽은 바이트 수를 리턴
	bytes_read = file_read(read_target, buffer, size);
	lock_release(&filesys_lock);
	return bytes_read;
}

int
write(int fd, void *buffer, unsigned size) {

	struct file *write_target;		// write할 파일 객체
	off_t bytes_written;						// 기록한 바이트 수.
	
	lock_acquire(&filesys_lock);	// 동시 접근을 막기 위해 lock사용

	// fd가 0보다 작거나 같으면면 헛소리이기 때문에 -1반환
	if(0 >= fd) {
		lock_release(&filesys_lock);
		return -1;
	}

	// fd가 1이면 모니터에 출력한다. 일반적 경우와 달리 처리해주어야함.
	// 여기서는 putbuf함수를 사용합니다.
	if(STDOUT_FILENO == fd) {
		putbuf (buffer, size); 
		lock_release(&filesys_lock);
		return size;
	}

	// 0, 1모두 아니면 파일객체 탐색 후 실패했으면 -1리턴
	write_target = process_get_file(fd);
	if(NULL == write_target) {
		lock_release(&filesys_lock);
		return -1;
	}

	// 성공했으면 file_write를 이용해 파일에 기록하고, 기록한 바이트 수를 리턴
	bytes_written = file_write(write_target, buffer, size);
	lock_release(&filesys_lock);
	return bytes_written;
}

// fd번째 파일객체의 offset을 position으로 이동.
void
seek(int fd, unsigned position) {
	struct file *target_file = process_get_file(fd);

	// 파일객체를 찾았을 경우에만 position으로 이동함.
	if(NULL != target_file) 
		file_seek(target_file, position);
}

// fd번째 파일객체의 offset을(여기선 pos)를 리턴함.
unsigned
tell(int fd) {
	struct file *target_file = process_get_file(fd);
	
	// 파일 객체를 못찾으면 꾀꼬리 대신 -1리턴
	if(NULL == target_file)
		return -1;

	return file_tell(target_file);
}

// fd번째 파일객체를 닫음
void
close(int fd) {
	process_close_file(fd);
}

// 인자로 받은 buffer의 주소가 유효한지 검사
void check_valid_buffer(void *buffer, unsigned size, void *esp, bool to_write) {
	struct vm_entry *vme;
	unsigned i;		// for loop을 위해. size가 unsigned이므로 그에 맞게 자료형 설정
	void *addr = buffer;

	// check_address로 주소 유효성 검사. 쓰기 가능 여부 판단
	for(i = 0; i < size; i++) {
		vme = check_address(addr, esp);

		if(NULL != vme && to_write) 
			if(false == vme->writable)
				exit(-1);

		addr++;
	}
}	

// str의 모든 문자 하나하나에 대해서 주소 검사를 함
void check_valid_string(const void *str, void *esp) {
	check_address((void *)str, esp);
	// 널 문자를 만날 때까지 검사
	while(*(char *)str != '\0') {
		str += 1;
		check_address((void *)str, esp);
	}
}


int mmap(int fd, void *addr) {
	struct mmap_file *mmp_f;
	size_t offset = 0;						// addr(file의 시작주소)로 부터의 offset
	static int new_mapid = 0;			// mapid를 위한 변수
	// addr의 4KB allign여부를 검사. pg_ofs가 0이면 페이지의 시작이므로
	// 4KB의 시작이라는 뜻
	if(0 != pg_ofs(addr))
		return -1;
	// for mmap-null test :(
	if(0 == addr)
		return -1;
	if(false == is_user_vaddr(addr))
		return -1;

	// free는 munmap()에서 함
	mmp_f = (struct mmap_file *)malloc(sizeof(struct mmap_file));
	if(NULL == mmp_f)
		return -1;			// 본 함수에서의 에러코드는 -1

	// 아래는 생성 후 초기화하는 과정임
	memset(mmp_f, 0, sizeof(struct mmap_file));
	list_init(&mmp_f->vme_list);
	mmp_f->file = file_reopen(process_get_file(fd));
	// fd번째 file을 reopen하는 과정에서 에러가 생겼으면 -1
	if(NULL == mmp_f->file)
		return -1;
	//mapid 부여
	mmp_f->mapid = new_mapid++;
	// 현재 스레드의 mmap_list에 삽입
	list_push_back(&thread_current()->mmap_list, &mmp_f->elem);

	// 아래는 vme를 생성하고 초기화하는 과정임
	// load_segment()에서 따옴
	// 우선 디스크의 file의 길이를 얻어서 그 길이만큼 돌며 vme를 생성
	int mmp_f_length = file_length(mmp_f->file);
	while (mmp_f_length > 0) {
		if(find_vme(addr))		// 이미 존재하면 -1
			return -1;

		// vm_entry를 생성, 멤버변수(오프셋, 사이즈,...) 설정
		struct vm_entry* vme = (struct vm_entry *)malloc(sizeof(struct vm_entry));
		vme->type = VM_FILE;
		vme->vaddr = addr;
		vme->offset = offset;
		vme->read_bytes = mmp_f_length < PGSIZE ? mmp_f_length : PGSIZE;	// 최대 PGSIZE
		vme->zero_bytes = 0;
		vme->writable = true;
		vme->file = mmp_f->file;
		vme->is_loaded = false;

		// 생성한 vm_entry를 해시 테이블에 추가
		insert_vme(&thread_current()->vm, vme);
		// mmap_file list의 원소로 추가
		list_push_back(&mmp_f->vme_list, &vme->mmap_elem);

		// while 문을 위한 변수들 업데이트, 한 페이지 단위로 맵핑하기 때문에 아래와 같다
		mmp_f_length -= PGSIZE;
		addr += PGSIZE;
		offset += PGSIZE;
	}

	return mmp_f->mapid;
}

void munmap(int mapid) {
	struct mmap_file *mmp_f = NULL;
	struct list_elem *e;
	// mapid가 -1이면 mmap_file전부 삭제
	if(-1 == mapid) {
		for(e = list_begin(&thread_current()->mmap_list);
				e != list_end(&thread_current()->mmap_list);
				/*e = list_next(e)*/) {
			mmp_f = list_entry(e, struct mmap_file, elem);
			if(NULL != mmp_f) {
				do_munmap(mmp_f);
				e = list_remove(&mmp_f->elem);
				free(mmp_f);
			}
			else
				break;
		}
	}	// if ends here
	else {		// 특정 mapid의 mmap_file를 삭제
		for(e = list_begin(&thread_current()->mmap_list);
				e != list_end(&thread_current()->mmap_list);
				e = list_next(e)) {
			struct mmap_file *mf = list_entry(e, struct mmap_file, elem);
			if(mapid == mf->mapid) {
				mmp_f = mf;
				break;
			}
		}
		// 못찾았으면 함수 종료
		if(NULL == mmp_f)
			return;

		do_munmap(mmp_f);
		list_remove(&mmp_f->elem);
		free(mmp_f);
	}	// else ends here
}	// end of munmap

void do_munmap(struct mmap_file *mmp_f) {
	struct list_elem *e;
	struct thread *t = thread_current();
	// e = list_next(e)를 안하는 이유는 for loop 내부에서 list_remove를 하기 때문
	for(e = list_begin(&mmp_f->vme_list); e != list_end(&mmp_f->vme_list);) {
		struct vm_entry *vme = list_entry(e, struct vm_entry, mmap_elem);
		if(vme->is_loaded) {
			// dirty면 file 동기화
			if(pagedir_is_dirty(t->pagedir, vme->vaddr)) {
				lock_acquire(&filesys_lock);
				file_write_at(vme->file, vme->vaddr, vme->read_bytes, vme->offset);
				lock_release(&filesys_lock);
			}
			// page 해제
			free_page(pagedir_get_page(t->pagedir, vme->vaddr));
			pagedir_clear_page(t->pagedir, vme->vaddr);
		} // end of outer if
		e = list_remove(e);
		delete_vme(&t->vm, vme);
		free(vme);
	} // end of for....
}


