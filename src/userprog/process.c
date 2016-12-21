#include "userprog/process.h"
#include "userprog/syscall.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
	//아래의 변수들은 parsing하는데 사용되는 것들임. 
	char *temp_fn_copy, *save_ptr;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

	// temp_fn_copy에 복사
  temp_fn_copy = palloc_get_page (0);
  if (temp_fn_copy == NULL)
    return TID_ERROR;
  strlcpy (temp_fn_copy, file_name, PGSIZE);

// 여기서 파싱을 했음. 위에서 선언된 temp_fn_copy, save_ptr을 이용했음
// 첫 번째 토큰인 프로그램 이름(ex. "echo")을 뽑는다. 
	temp_fn_copy = strtok_r(temp_fn_copy, " ", &save_ptr);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (temp_fn_copy, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
	palloc_free_page(temp_fn_copy);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
// file_name_안에 는 예를 들어 echo x y z가 들어있음.
// 아래 함수에서는 그 것들을 쪼개서 스택위에 인자들을 올리고 <- load
// 실제로 그 프로그램을 실행함.
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
	char *token, *save_ptr;
	char *parsed_arg[LOADER_ARGS_LEN / 2 + 1];	//	loader.h참조
  struct intr_frame if_;
	struct thread *t;
  bool success;
	int count = 0; // 인자의 갯수를 저장

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

	for(token = strtok_r(file_name, " ", &save_ptr);
			token != NULL;
			token = strtok_r(NULL, " ", &save_ptr)) {
			parsed_arg[count] = token;
			count++;
	}
	
  success = load (file_name, &if_.eip, &if_.esp);

	// load성공 여부에 따라 스레드 디스크립터 내의 load플래그 전환.
	t = thread_current();
	t->is_loaded = success ? true : false ;
	
	// 부모를 깨워도 됩니다. load함수가 끝났고, load플래그도 잘 섰기 때문에.
	// cf) 부모는 exec()에서 잠듬
	sema_up(&t->sema_load);

	// 프로세스 실행을 위해 필요한 인자들을 스택에 쌓는다.
	if(success) {
		argument_stack(parsed_arg, count, &if_.esp);
	}
	//hex_dump(if_.esp, if_.esp, PHYS_BASE - if_.esp, true);

	// palloc 했던 아이들을 삭제합니다.
  palloc_free_page (file_name_);

  /* If load failed, quit. */
  if (!success) { 
		// t->exit_status = -1;
    thread_exit ();
	}

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{

	// 자식 프로세스를 tid를 이용해 찾음.
	struct thread *child = get_child_process(child_tid);
	// 원래 아래와 같은 변수를 선언하지 않고 그냥
	// return child->exit_status를 했었는데 그러면
	// remove_child_process에서 thread가 전부 파괴되기 때문에
	// exit_status가 망가진다.
	int exit_status;				

	// 만약 tid로 자식 프로세스 검색에 실패했다면, -1리턴
	if(NULL == child)
		return -1;

	// 자식 프로세스의 종료를 기다림
	// 깨우는 것은 thread_exit
	sema_down(&child->sema_exit);	

	// 자식 프로세스가 정상적으로 종료됐는지 확인 후 비정상 종료면
	// -1을 리턴.
	if(false == child->is_ended) {
		// 아래의 remove_child_process는 원래 없었는데, 비정상 종료시에
		// child지우는 것을 수행하지 않는 것 같아서 추가함
		remove_child_process(child);
		return -1;
	}
	
	// 자식의 죽음. 죽기 전에 자신의 exit_status를 저장함.
	exit_status = child->exit_status;
	remove_child_process(child);

	// 자식 프로세스의 exit status 리턴
	return exit_status;
}

// cp(child process)를 지움. 우선 자식으로서의 cp를 지우고, cp를 지움.
void
remove_child_process(struct thread *cp)
{
  list_remove(&cp->me_as_child);
	palloc_free_page(cp);
}
/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
	int cur_fd_index = cur->next_fd;	// 현재 스레드의 fd인덱스를 저장.

	// 아래는 fdt관련 파일 닫는 부분임. 
	while(cur_fd_index > 2) { // 2까지만. 0, 1은 원래 있으니까.
		process_close_file(cur_fd_index - 1);
		cur_fd_index--;
	}
	palloc_free_page(cur->fdt);	// get_page한거 free

	// file_close여기서 한다.
	file_close(cur->run_file);
	// 현재 디렉터리를 닫음
	dir_close(cur->cur_dir);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
	{
		/* Correct ordering here is crucial.  We must set
			 cur->pagedir to NULL before switching page directories,
			 so that a timer interrupt can't switch back to the
			 process page directory.  We must activate the base page
			 directory before destroying the process's page
			 directory, or our active page directory will be one
			 that's been freed (and cleared). */
		cur->pagedir = NULL;
		pagedir_activate (NULL);
		pagedir_destroy (pd);
	}

}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

	//락을 건다. 로드 하는 동안 누가 건들면 안되기 때문에
	lock_acquire(&filesys_lock);
  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
			lock_release(&filesys_lock);		// 락 해제
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

	// 읽기 전에, run_file 설정 해주고, file_deny_write호출하자. 언락도 하고.
	t->run_file = file;
	file_deny_write(file);
	lock_release(&filesys_lock);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
	// 아래의 fild_close는 주석처리를 해야한다.
	// 왜냐하면 위에서 열었는데 여기서 닫아버리면 다른 곳에서 참조를 못한다.
	// close는 process_exit에서 한다.
  //file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


void
argument_stack(char **parse ,int count ,void **esp)
{
	int i = 0;				// for for loop;
	// int j = 0;				// 한 글자씩 스택에 넣는 경우에 필요함.
	void *top_of_command;
	int *token_distance = palloc_get_page(PAL_ZERO);
	int token_length_sum = -1; 		// 아래 token_distance에 값을 넣는 과정에서 사용됨.
	
	// memory allocation check
	if(token_distance == NULL)
    return TID_ERROR;

	// token들의 길이 만큼 esp를 뒤로 옮김. +1하는 이유는 공백 저장을 위하여
	// 그리고 token_distance도 값을 할당함. token_distance는 command의 시작부
	// 터 각 토큰까지의 거리를 나타냄.
	for(i = 0; i <count; i++) {
		*esp -= strlen(parse[i]) + 1;
		token_distance[i] = token_length_sum + 1;
		token_length_sum += strlen(parse[i]) + 1;
	}
	

	// 아래는 한 단어씩 넣는 방법을 사용함. 
  top_of_command = *esp;
	for(i = 0; i < count; i++) {
		strlcpy((char*)(*esp), parse[i], strlen(parse[i]) + 1);
		*esp += strlen(parse[i]) + 1;			// considering '\0'
	}

	/*참고로 아래는 한 글자씩 넣는 방법을 사용함. 

  top_of_command = *esp;
	for(i = 0; i < count; i++) {
		for(j = 0; j < strlen(token_array[i]); j++) {
			*(char*)(*esp) = token_array[i][j];
			*esp += 1;
			printf("%c\n", token_array[i][j]);
		}
		*esp += 1;		// considering '\0'
	}  */
	

	*esp = top_of_command;		// move esp back;

	// from PHYS_BASE to esp, it should be multiple of 4 
	if(0 != ((int)PHYS_BASE - (int)(*esp)) % 4)
		*esp -= 4 - (((int)PHYS_BASE - (int)(*esp)) % 4);

	// insert null
	*esp -= 4;
	*(int*)(*esp) = 0;

	// save the address of arguments on the stack which were already pushed in the
	// stack ex) in the case of 'echo x y z' (argv + 0) ~ (argv + 3)
	for(i = count - 1; i >= 0; i--) {
		*esp -= 4;		// size of pointer is 4
		*(char**)(*esp) = top_of_command + token_distance[i];
	}

	// **argv
  *esp -= 4;
	*(char**)(*esp) = (*esp +4);

	// argc
	*esp -= 4;
	*(int*)(*esp) = count;

	//insert fake address
	*esp -= 4;
	*(int*)(*esp) = 0;


	// memory free
	palloc_free_page(token_distance);
}


// 인자로 받은 f를 파일 디스크립터에 추가
// 새로 추가된 f의 인덱스를 리턴.
int process_add_file(struct file *f) {
  struct thread *cur = thread_current ();
	cur->fdt[cur->next_fd] = f;
	cur->next_fd++;
		
	return cur->next_fd - 1;
}

// fd번째 파일 객체의 주소를 리턴.
// 없으면 NULL 반환.
struct file *process_get_file(int fd) {
	struct thread *cur = thread_current();
	if(NULL == cur->fdt[fd] || fd >= cur->next_fd)
		return NULL;

	return cur->fdt[fd];
}

// fd번째 파일 객체를 닫음.
// fd번째는 NULL로 바꿈.
void process_close_file(int fd) {
	struct file *close_target = process_get_file(fd);
	if(NULL != close_target) {
		file_close(close_target);
		thread_current()->fdt[fd] = NULL;
	}
}
