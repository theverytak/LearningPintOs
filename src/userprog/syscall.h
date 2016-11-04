#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "vm/page.h"

void syscall_init (void);
//void check_address(void *addr);
struct vm_entry *check_address(void *addr, void* esp /*Unused*/); 
void get_argument(void *esp, int *arg, int count);
void check_valid_string(const void *str, void *esp); 
void check_valid_buffer(void *buffer, unsigned size, void *esp, bool to_write);
struct lock filesys_lock;		// 사용할 lock 전역변수로 설정
int mmap(int fd, void *addr);
void munmap(int mapid);
void do_munmap(struct mmap_file *mmp_f); 


#endif /* userprog/syscall.h */
