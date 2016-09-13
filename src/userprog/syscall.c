#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) // 여기서 esp를 사용함. system call number 도 받아옴.
{
	// 유저 스택에 저장되어 있는 시스템 콜 넘버를 이용해 시스템 콜 핸들러 구현
	// ex) void *esp = f->esp;
	//		 int sys_n = *esp( esp에서 시스템 콜 넘버를 가져옴)
	//  	 switch(sys_n)
	// 			case SYS_WRITE : ...
	//					 get_argument
	//					 check_address(arg[1]);
	//					 f->eax = write(arg[0] .... ) eax레지스터에 write의 리턴 값을 저장함.
	//  				 break;
	// 스택 포인터가 유저 영역인지 확인
	// 저장된 인자 값이 포인터일 경우 유저 영역의 주소인지 확인
	// 0 : halt
	// 1 : exit
	// ...
  printf ("system call!\n");
  thread_exit ();
}

// addr이 유효한 주소인지 확인.0x804800에서 0x0000000사이이면 유저영역임.
// 경계는 제외.
void check_address(void *addr) //이번에 구현할 함수.
{
	if(addr <= 0x804800 || addr >= 0x0000000)
		exit(-1);
}


// 유저 스택에 저장된 인자값들을 커널에 복사.
// 참고로 핀토스 시스템콜의 최대 인자 수는 3개. read, write가 인자 3개이다.
// 그러므로, count가 4보다 작거나 같다고  생각하고 만듬.
void get_argument(void *esp, int *arg, int count)
{
	// 유저 스택에 저장된 인자값들을 커널에 복사
	// 인자가 저장된 위치가 유저영역인지 확인
	ASSERT(count >= 0 && count <= 4);   // count의 범위를 확인.

	esp++;			// 첫 인자는 system call number이기 때문에 esp++을 함. 

	for(i = 0; i < count; i++)
		arg[i] = (esp + i)   // 일단은 *(esp + i)가 아니라 이렇게 함. 주소값을 넣는게 맞아보임.

}


void exit(int status)
{
	// src/threads/thread.h
	// thread_current();     PCB와 비슷함.
	// 실행중인 스레드구조체(PCB)가져옴
	// 프로세스 이름: exit(status)”

