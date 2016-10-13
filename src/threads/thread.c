#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

// 아래는 mlfq를 위한 매크로 상수들
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* 잠자는 스레드들의 리스트 */
static struct list sleep_list;

static int64_t next_tick_to_awake = INT64_MAX;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

int load_avg;		// mlfq를 위한 전역변수

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
	list_init (&sleep_list); // sleep list를 초기화. 위의 코드를 따라함.

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);
	load_avg = LOAD_AVG_DEFAULT;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);
		
	// 나의 부모를 찾아 저장
	t->parent = thread_current();
	// 메모리 탑재안됨, 프로세스 종료 안됨.
	t->is_loaded = false;
	t->is_ended = false;
	// semaphore모두 초기화
	sema_init(&t->sema_exit, 0);
	sema_init(&t->sema_load, 0);

	// 파일 디스크립터 관련 부분. 
	// 파일 디스크립터 테이블을 할당 받고
	// fdt이 그 처음을 가리키게 하고, next_fd가 2가 되게 한다.
	t->fdt = palloc_get_page(PAL_ZERO);
	t->next_fd = 2;

	// 나를 나의 부모의 자식리스트에 넣는다. 
	list_push_back(&thread_current()->childs, &t->me_as_child);

  /* Add to run queue. */
  thread_unblock (t);

	// 우선순위를 따진다. 새로 생성된 thread와 current_thread의 대결
	if(t->priority > thread_current()->priority)
		thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  //list_push_back (&ready_list, &t->elem);
	// list에 넣으면서 우선순위대로 넣도록 함.
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

	struct thread *t = thread_current();
  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&t->allelem);

	// 프로세스 디스크립터에 프로세스 종료를 알림
	t->is_ended = true;
		
	/* 여기서 혹시 종료하려는 스레드가 이니셜 스레드인 것은 아닌지
	검사한다. 왜냐하면 이니셜 스레드도 종료하기 위해서 본 함수에 들
		어오기 때문이다. */
	if(t != initial_thread) {
		// 부모프로세스는 대기상태를 이탈(세마업) 
		sema_up(&t->sema_exit);
	}

	// 나의 상태를 죽음으로 변경
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    //list_push_back (&ready_list, &cur->elem);
		// yield후 다시 ready_list에 들어갈 때, 우선순위정렬로 들어가게함.
		list_insert_ordered(&ready_list, &cur->elem, cmp_priority, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
	// mlfqs면 안함
	if(true == thread_mlfqs)
		return ;
	intr_disable();		// TODO 왜 이거 막아야하는지 알아볼 것

  thread_current ()->priority = new_priority;

	intr_enable();

	test_max_priority();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
// nice변경후에는 우선순위를 다시 계산하고 다시 스케쥴링함.
void
thread_set_nice (int nice UNUSED) 
{
  intr_disable();

	struct thread *cur = thread_current();
	cur->nice = nice;						//nice 변경
	mlfqs_priority(cur);				//priority변경
	test_max_priority();				//스케쥴링

  intr_enable();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
	int result;
	intr_disable();
	result = thread_current()->nice;
	intr_enable();

  return result;
}

/* Returns 100 times the system load average. */
// 해당 과정 중에 인터럽트를 막고 100 곱해서 리턴함.
int
thread_get_load_avg (void) 
{
	int load_avg_return;
	intr_disable();  // to disable intr;
	load_avg_return = mult_mixed(load_avg, 100);
	load_avg_return = fp_to_int_round(load_avg_return);
 	intr_enable();   // to enable intr;

  return load_avg_return;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
	int recent_cpu_return;
	struct thread *cur = thread_current();

	intr_disable();  // to disable intr;
	recent_cpu_return = mult_mixed(cur->recent_cpu, 100);
	recent_cpu_return = fp_to_int_round(recent_cpu_return);
 	intr_enable();   // to enable intr;

  return recent_cpu_return;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);

	t->elem.prev = NULL;
	t->elem.next = NULL;

	t->me_as_child.prev = NULL;
	t->me_as_child.next = NULL;

	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;

	list_init(&t->childs);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      //palloc_free_page (prev);
			// 자식을 죽이는 것은 remove_child_process가 대신함.
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);


struct
thread* get_child_process(tid_t tid)
{
	// 임의의 struct thread를 가리키는 포인터 하나 선언.
	struct thread* t;
	// for문을 돌면서 내가 원하는 자식 스레드를 검색할 놈을 선언.
	struct list_elem *e;
	// thread_current() 가 반복되므로 하나 선언.
	struct thread* cur = thread_current();
	// 나의 자식 리스트를 검색함. pid를 가지는 놈 선별.
	// 아래 코드는 list_entry의 예제를 그대로 사용함. 
	for(e = list_begin(&cur->childs); e!= list_end(&cur->childs); e = list_next(e))
		{
			t = list_entry(e, struct thread, me_as_child);
			if(t->tid == tid)
				return t;
		}

	// not found a thread w/ tid
	return NULL;
}


// 현재 thread를 ticks만큼 재우고, 스케쥴링함.
void thread_sleep(int64_t ticks) {
	struct thread* cur = thread_current();

	/* 현재 스레드가 idle스레드가 아니면 재운다.
		 우선 상태를 blocked로 바꾸고, 깨어날 시간을 저장. */
	if(cur != idle_thread) {
		intr_disable();  // to disable intr;
		cur->status = THREAD_BLOCKED;
		cur->wakeup_tick = ticks;

		/* sleep list에 넣고, next_tick_to_awake업데이트함
			 혹시 새로들어온 내가 최소일 수 있으니까 */
		list_push_back(&sleep_list, &cur->elem);
		update_next_tick_to_awake(ticks);
		// 그 후에 스케쥴해서 새로운 current_thread가 나올 수 있게
		schedule();
  	intr_enable();   // to enable intr;
	}

}

// ticks보다 남은 시간이 적은 thread를 깨우는 함수
void thread_awake(int64_t ticks) {
	struct list_elem* e;	
	struct thread *t;	
	/* sleep list를 순회하면서 깨워야할 아이들은 깨우고, 
		 아직 남아있는 아이들을 토대로 next_tick_to_awake를 최신화 */
	// TODO : 아래 for문에서 문제가 있었던 것을 보고서에 쓸 것.
	// 지혜를 남겨 놓자면 예전엔 for문 안에서 list_next를 함
	// 근데 그렇게 하면 list_remove를 하는 순간 e는 외톨이가 됨. 
	for(e = list_begin(&sleep_list); e != list_end(&sleep_list); ) {
		t = list_entry (e, struct thread, elem);	
		if(t->wakeup_tick <= ticks) {
			e = list_remove(&t->elem);
			thread_unblock(t);
    }
		else {
			update_next_tick_to_awake(t->wakeup_tick);
			e = list_next(e);
		}
	}
}

// ticks가 next_tick_to_awake보다 작으면 최신화
void update_next_tick_to_awake(int64_t ticks) {
	if(ticks < next_tick_to_awake)
		next_tick_to_awake = ticks;
}

//간단한 getter
int64_t get_next_tick_to_awake(void) {
	return next_tick_to_awake;
}


void test_max_priority(void) {
	// ready_list가 비어있지 않은 경우에 대하여
	if(!list_empty(&ready_list)) {
		struct list_elem *e = list_begin(&ready_list);
		// ready_list의 첫 번째 스레드. ready_list에서 가장 우선순위가 높음
		struct thread *first_entry = list_entry(e, struct thread, elem);
		if(first_entry->priority > thread_current()->priority) 
			thread_yield();
	}
}

// a가 높으면 true, b가 높으면 false
bool cmp_priority(const struct list_elem *a, const struct list_elem *b,
									void *aux UNUSED) {
	return list_entry(a, struct thread, elem)->priority >
				 list_entry(b, struct thread, elem)->priority;
}

// thread t의 priority를 mlfq에 맞게 바꿈
void mlfqs_priority(struct thread *t) {
	if(idle_thread == t) 
		return;

//priority = PRI_MAX - (recent_cpu / 4) - (nice * 2);
//아래의 변수들은 위 식의 우변의 각 항을 나타냄.
	int term1, term2, term3;

	// 우변의 세 항을 일단 계산
	term1 = int_to_fp(PRI_MAX);
	term2 = div_mixed(t->recent_cpu, 4);
	term3 = int_to_fp(t->nice * 2);
	
	// 왼쪽에서 부터 차례로 뺄셈
	term1 = sub_fp(term1, term2);
	term1 = sub_fp(term1, term3);

	// 다시 int로 바꾸어서 저장
	t->priority = fp_to_int(term1);
}

// thread t의 recent_cpu를 mlfq용으로 바꿈
void mlfqs_recent_cpu(struct thread *t) {
	if(idle_thread == t)
		return;
//recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice;
//아래의 변수들을 위 식 우변의 네 개의 항을 각각 나타냄.
	int term1, term2, term3, term4;
	term1 = mult_mixed(load_avg, 2);
	term2 = add_mixed(term1, 1);		// 겹치는 부분이 있어서 term1을 활용함
	term3 = t->recent_cpu;
	term4 = t->nice;

	//왼쪽부터 차례로 계산
	term1 = div_fp(term1, term2);
	term1 = mult_fp(term1, term3);
	term1 = add_mixed(term1, term4);

	//결과값 저장
	t->recent_cpu = term1;
}

// load_avg를 바꿈
void mlfqs_load_avg(void) {

	//load_avg = (59 / 60) * load_avg + (1 / 60) * ready_threads
	//아래의 변수들은 위 식 우변의 네개의 항을 각각 나타냄.
	int term1, term2, term3, term4;
	term1 = div_mixed(int_to_fp(59), 60);
	term2 = load_avg;
	term3 = div_mixed(int_to_fp(1), 60);
	// term4는 ready_list에 있는 thread의 갯수와 현재스레드의 수를 저장하는데
	// 아래의 탐색을 통해 찾는다. 다만 현재idle_thread라면, 더하지 않는다.
	term4 = 1;
	struct list_elem *e;
	for(e = list_begin(&ready_list); e != list_end(&ready_list);
			e = list_next(e))
		term4++;

	if(thread_current() == idle_thread)
		term4--;
	// 각 항을 초기화 했으므로 load_avg를 계산한다
	term1 = mult_fp(term1, term2);
	term3 = mult_mixed(term3, term4);
	term1 = add_fp(term1, term3);

	load_avg = term1;
	
	//load_avg는 0보다 작아질 수 없다.
	if(load_avg < 0)
		load_avg = 0;
}

// 현재 스레드의 recent_cpu를 1증가.
void mlfqs_increment(void) {
	struct thread *cur = thread_current();
	if(idle_thread == cur)
		return ;

	cur->recent_cpu = add_mixed(cur->recent_cpu, 1);
}

// 모든 스레드의 recent_cpu와 priority를 재계산
void mlfqs_recalc(void) {
	struct list_elem *e;
	struct thread* t;
	for(e = list_begin(&all_list); e != list_end(&all_list);
			e = list_next(e)) {
		t = list_entry(e, struct thread, allelem);
		mlfqs_recent_cpu(t);
		mlfqs_priority(t);
	}
}
