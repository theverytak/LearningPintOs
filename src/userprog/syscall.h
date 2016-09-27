#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void check_address(void *addr);
void get_argument(void *esp, int *arg, int count);

struct lock filesys_lock;		// 사용할 lock 전역변수로 설정

#endif /* userprog/syscall.h */
