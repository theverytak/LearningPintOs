#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"   // thread_exit()
#include "devices/shutdown.h" // shutdown_power_off()
#include "devices/input.h"		// input_getc()
#include "filesys/filesys.h"  // filesys_create(), remove()
#include "filesys/file.h"			// file_close(), file_read(), file_write(), 
															//file_seek(), file_tell(), file_length()
#include "userprog/process.h" // process_execute(), process_wait()


static void syscall_handler (struct intr_frame *f UNUSED);

struct lock filesys_lock;		// 사용할 lock 전역변수로 설정

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
	void *esp = f->esp;		// esp 복사
	int sys_n = *(int *)(f->esp); 	  // system call number. esp가 가리키는 곳에 있음.
	int arg[4];						// 인자를 넣을 배열. 최대 인자 갯수는 4개임.

	check_address(esp);

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
				printf("halt() called\n");
				halt();
				break;
			}
		case SYS_EXIT :                  /* Terminate this process. */
			{
				printf("exit() called\n");
				get_argument(esp, arg, 1);
				exit(arg[0]);
				break;
			}
		case SYS_EXEC :                  /* Start another process. */
			{
				printf("exec() called\n");
				get_argument(esp, arg, 1);
				check_address((void *)arg[0]);		// arg[0]이 유효한 주소영역인지 검사.
				f->eax = exec((const char *)arg[0]);
				break;
			}
		case SYS_WAIT :                  /* Wait for a child process to die. */
			{
				printf("wait() called\n");
				get_argument(esp, arg, 1);
				f->eax = wait((tid_t)arg[0]);
				break;
			}
		case SYS_CREATE :                /* Create a file. */
			{
				printf("create() called\n");
				get_argument(esp, arg, 2);
				check_address((void *)arg[0]);		// arg[0]이 유효한 주소영역인지 검사.
				f->eax = create((const char *)arg[0], arg[1]);
				break;
			}
		case SYS_REMOVE :                /* Delete a file. */
			{
				printf("remove() called\n");
				get_argument(esp, arg, 1);
				check_address((void *)arg[0]);	//  arg[0]이 유효한 주소영역인지 검사.
				f->eax = remove((const char *)arg[0]);
				break;
			}
     case SYS_OPEN :                 /* Open a file. */
			{
				printf("open() called\n");
				get_argument(esp, arg, 1);
				check_address((void *)arg[0]);
				f->eax = open((const char *)arg[0]);
				break;
			}
     case SYS_FILESIZE :             /* Obtain a file's size. */
			{
				printf("filesize() called\n");
				get_argument(esp, arg, 1);
				f->eax  = filesize(arg[0]);
				break;
			}
     case SYS_READ :                 /* Read from a file. */
			{
				printf("read() called\n");
				get_argument(esp, arg, 3);
				check_address((void *)arg[1]);
				f->eax = read(arg[0], (void *)arg[1], arg[2]);
				break;
			}
     case SYS_WRITE :                /* Write to a file. */
			{
				printf("write() called\n");
				get_argument(esp, arg, 3);
				check_address((void *)arg[1]);
				f->eax = write(arg[0], (void *)arg[1], arg[2]);
				break;
			}
     case SYS_SEEK :                 /* Change position in a file. */
			{
				printf("seek() called\n");
				get_argument(esp, arg, 2);
				seek(arg[0], arg[1]);
				break;
			}
     case SYS_TELL :                 /* Report current position in a file. */
			{
				printf("tell() called\n");
				get_argument(esp, arg, 1);
				f->eax = tell(arg[0]); 
				break;
			}
     case SYS_CLOSE :                /* Close a file. */
			{
				printf("close() called\n");
				get_argument(esp, arg, 1);
				close(arg[0]);
				break;
			}
	}

  printf ("system call!\n");
  thread_exit ();
}

/* addr이 유효한 주소인지 확인.0x804800에서 0x0000000사이이면 유저영역임.
 경계는 제외.
 esp를 검사할 때 뿐만 아니라 예를들어 인자로 포인터를 받아온 경우에도
 아래 함수를 사용가능. */
void 
check_address(void *addr) 
{
	uint32_t address = (uint32_t)addr;
	if(address <= 0x8048000 || address >= 0xc0000000)
		exit(-1);
}


// 유저 스택에 저장된 인자값들을 커널에 복사.
// 참고로 핀토스 시스템콜의 최대 인자 수는 3개. read, write의 인자가 3개이다.
// 그러므로, count가 3보다 작거나 같다고  생각하고 만듬.
void 
get_argument(void *esp, int *arg, int count)
{
	int i;		// for loop;

	// esp가 유저영역인지 확인. 
	check_address(esp);		
												
	// 유저 스택에 저장된 인자값들을 커널에 복사
	// 인자가 저장된 위치가 유저영역인지 확인
	ASSERT(count >= 0 && count <= 3);   // count의 범위를 확인.

	for(i = 0; i < count; i++)
		arg[i] = *(int *)(esp + i + 1);   // esp는 현재 system call number을 가리키므로
													  	 // + 1한 값 부터 참조.
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
	thread_current()->status = status;
	printf ("%s: exit(%d)\n", thread_name(), status);
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
	struct file *open_target = filesys_open(file);

	// 실패시 -1리턴
	if(NULL == open_target)
		return -1;
		
	// 오픈 성공했으면 open_target을 fdt에 넣고 그 인덱스 리턴
	return process_add_file(open_target);
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

	/* 파일에 동시접근이 일어날 수 잇으므로 lock사용
		 파일 디스크립터를 이용하여 파일 객체 검색
		 파일 디스크립터가 0일 경우 키보드에 입력을 버퍼에
		 저장 후 버퍼에 저장한 크기를 리턴(input_getc이용
		 파일 디스크립터가 1일 경우 예외처리
		 파일 디스크립터가 0이 아닐 경우 파일의 데이터를 크기
		 만큼 저장 후 읽은 바이트 수를 리턴 */

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
	if(0 == fd) {
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

	/* 파일에 동시접근이 일어날 수 있으므로 lock사용
		 파일 디스크립터를 이용하여 파일 객체 검색
		 파일 디스크립터가 1일 경우 버퍼에 저장된 값을 화면에 출력
		 후 버퍼의 크기 리턴(putbuf() 이용)
		 파일 디스크립터가 1이 아닐 경우 버퍼에 저장된 데이터를
		 size만큼 파일에 기록후 그 기록한 바이트 수를 리턴 */

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
	if(1 == fd) {
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
