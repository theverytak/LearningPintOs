#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"   // thread_exit()
#include "devices/shutdown.h" // shutdown_power_off()
#include "filesys/filesys.h"  // filesys_create(), remove()


static void syscall_handler (struct intr_frame *f UNUSED);
static void halt(void);
static void exit(int status);
static int wait(tid_t tid);
static bool create(const char *file , unsigned initial_size);
static bool remove(const char *file);
pid_t exec(const char *cmd_line);
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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
				f->eax = create((const char *)arg[0], (unsigned)arg[1]);
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
    // case SYS_OPEN :                 /* Open a file. */
    // case SYS_FILESIZE :             /* Obtain a file's size. */
    // case SYS_READ :                 /* Read from a file. */
    // case SYS_WRITE :                /* Write to a file. */
    // case SYS_SEEK :                 /* Change position in a file. */
    // case SYS_TELL :                 /* Report current position in a file. */
    // case SYS_CLOSE :                /* Close a file. */
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

pid_t 
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




