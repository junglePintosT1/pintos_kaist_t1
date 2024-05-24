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
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* NOTE: [Part1] 상태가 THREAD_BLOCKED인 쓰레드들의 리스트 */
static struct list sleep_list;

/* NOTE: [Improve] 모든 쓰레드를 담는 리스트 */
static struct list all_list;

/* 쓰레드들의 wakeup_tick 중 최소값을 저장하는 전역변수 */
static int64_t global_tick;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* NOTE: [Part3] 시스템 부하 */
fixed_point load_avg;

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

static int64_t get_min_tick(void);
static int set_global_tick(int64_t tick);
static bool wakeup_less(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0,
						  0x00af9a000000ffff,
						  0x00cf92000000ffff};

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
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&sleep_list); /* sleep list 초기화 */
	list_init(&all_list);	/* NOTE: [Improve] all list 초기화 */
	list_init(&destruction_req);

	global_tick = INT64_MAX; /* global tick 초기화 */
	load_avg = int_to_fp(0); /* NOTE: [Part3] load_avg 초기화 */

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
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
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* NOTE: [Improve] 모든 쓰레드 생성 시 all_list에 추가 */
	// list_push_back(&all_list, &t->all_elem);

	/* Add to run queue. */
	thread_unblock(t);

	/**
	 * NOTE: current 쓰레드와 새롭게 생성된 쓰레드의 우선순위 비교하여, 필요 시 yield
	 * part: priority-insert-ordered
	 */
	thread_compare_yield();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);

	/**
	 * NOTE: ready_list에 우선순위 순으로 삽입
	 * part: priority-insert-ordered
	 */
	list_insert_ordered(&ready_list, &t->elem, compare_priority, NULL);
	t->status = THREAD_READY;
	intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

void thread_compare_yield(void)
{
	if (thread_current() == idle_thread)
	{
		return;
	}

	if (list_empty(&ready_list))
	{
		return;
	}

	if (thread_current()->priority < list_entry(list_begin(&ready_list), struct thread, elem)->priority)
		thread_yield();
}

/**
 * @brief CPU를 다른 쓰레드에게 양보하는 함수
 *
 * CPU를 양보하고 다른 스레드에게 실행 기회를 줍니다.
 * 현재 스레드는 잠들지 않고, 스케줄러의 판단에 따라 다시 스케줄링될 수 있습니다.
 * 이 함수는 인터럽트가 비활성화된 상태에서 호출되어야 합니다.
 */
void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();

	/**
	 * NOTE: ready_list에 우선순위 순으로 삽입
	 * part: priority-insert-ordered
	 */
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, compare_priority, NULL);
	do_schedule(THREAD_READY);
	intr_set_level(old_level);
}

/**
 * @brief 현재 쓰레드를 잠재우고, 주어진 틱 시간에 깨어나도록 설정하는 함수
 *
 * @param wakeup_tick 쓰레드가 깨어나야 하는 시간을 나타내는 틱 값
 */
void thread_sleep(int64_t wakeup_tick)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable(); /* 인터럽트 비활성화 */

	if (curr != idle_thread)
	{
		curr->wakeup_tick = wakeup_tick; /* local tick 설정 */
		if (wakeup_tick < global_tick)	 /* 필요시 global_tick 갱신 */
			set_global_tick(wakeup_tick);
		list_push_back(&sleep_list, &curr->elem); /* sleep_list에 쓰레드 삽입 */
	}
	do_schedule(THREAD_BLOCKED); /* 현재 쓰레드를 blocked 상태로 스케줄링 */
	intr_set_level(old_level);	 /* 이전 인터럽트 복원 */
}

/**
 * @brief 주어진 틱 시간에 깨어날 쓰레드를 깨우는 함수
 *
 * @param curr_tick 현재 시간을 나타내는 틱 값
 */
void thread_wakeup(int64_t curr_tick)
{
	if (global_tick > curr_tick) /* 현재 tick이 global tick보다 작은 경우 함수 종료 */
		return;

	if (list_empty(&sleep_list)) /* sleep_list가 비어있는 경우 함수 종료 */
		return;

	struct list_elem *e;
	struct thread *t;

	e = list_begin(&sleep_list);
	while (e != list_end(&sleep_list)) /* sleep_list를 순회하며 깨어날 쓰레드 처리 */
	{
		t = list_entry(e, struct thread, elem); /* 해당 elem와 매핑된 thread */

		if (t->wakeup_tick <= curr_tick) /* wakeup 필요 */
		{
			e = list_remove(e); /* sleep_list에서 제거 */
			thread_unblock(t);	/* 쓰레드 block 해제 후 */
			// thread_compare_yield();
			set_global_tick(get_min_tick()); /* global_tick 갱신 */
		}
		else
			e = e->next;
	}
	set_global_tick(get_min_tick()); /* global_tick 갱신 */
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{

	if (thread_mlfqs)
		return;

	/* NOTE: donation 고려하여 우선순위 설정 */
	if (thread_current()->origin_priority == thread_current()->priority)
		thread_current()
			->priority = new_priority;
	thread_current()->origin_priority = new_priority;

	/**
	 * NOTE: Reorder the ready_list
	 * part: priority-insert-ordered
	 */
	// if(thread_current()->wait_on_lock != NULL){
	// 	update_donate_priority(&thread_current()->wait_on_lock);
	// }
	update_donate_priority();
	if (!list_empty(&ready_list))
	{
		struct thread *t = list_entry(list_front(&ready_list), struct thread, elem);
		thread_compare_yield();
	}
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/** NOTE: [Part3]
 * @brief 현재 실행 중인 쓰레드의 nice 값을 설정하는 함수
 */
void thread_set_nice(int new_nice)
{
	enum intr_level old_level = intr_disable();
	if (thread_current() != idle_thread)
		thread_current()->nice = new_nice;
	thread_calc_priority(thread_current());
	thread_compare_yield();
	intr_set_level(old_level);
}

/** NOTE: [Part3]
 * @brief 현재 실행 중인 쓰레드의 nice 값을 반환하는 함수
 *
 * @return int 현재 쓰레드의 nice 값
 */
int thread_get_nice(void)
{
	enum intr_level old_level = intr_disable();
	int nice = thread_current()->nice;
	intr_set_level(old_level);
	return nice;
}

/** NOTE: [Part3]
 * @brief 시스템의 평균 부하(load average)를 반환하는 함수
 * 시스템의 평균 부하를 100배하여 정수로 반환
 *
 * @return int 시스템의 평균 부하 값에 100을 곱한 후 버림한 정수 값
 */
int thread_get_load_avg(void)
{
	enum intr_level old_level = intr_disable();
	fixed_point load_avg_100_times = mul_fp(load_avg, int_to_fp(100)); /* 100배 */
	int load_avg = fp_to_int_round_zero(load_avg_100_times);		   /* 정수로 변환 */
	intr_set_level(old_level);

	return load_avg;
}

/** NOTE: [Part3]
 * @brief 현재 실행 중인 쓰레드의 최근 CPU 사용량을 반환하는 함수
 * 현재 실행 중인 쓰레드의 최근 CPU 사용량을 100배하여 정수로 반환
 *
 * @return int 현재 쓰레드의 최근 CPU 사용량에 100을 곱한 후 버림한 정수 값
 */
int thread_get_recent_cpu(void)
{
	enum intr_level old_level = intr_disable();
	fixed_point recent_cpu_100_times = mul_fp(thread_current()->recent_cpu, int_to_fp(100)); /* 100배 */
	int recent_cpu = fp_to_int_round_zero(recent_cpu_100_times);							 /* 정수로 변환 */
	intr_set_level(old_level);

	return recent_cpu;
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
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

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
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	/* NOTE: donation을 위한 데이터 초기화 */
	list_init(&t->donations);
	t->origin_priority = priority;

	/* NOTE: [Part3] MLFQ를 위한 데이터 초기화 */
	t->nice = 0;
	t->recent_cpu = 0;

	/* NOTE: [Improve] 모든 쓰레드 생성 시 all_list에 추가 */
	list_push_back(&all_list, &t->all_elem);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		list_remove(&victim->all_elem); /* NOTE: [Improve] 쓰레드가 죽을 때 all_list에서 제거 */
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

static int64_t get_min_tick(void)
{
	if (list_empty(&sleep_list))
		return INT64_MAX;
	return list_entry(list_min(&sleep_list, wakeup_less, NULL), struct thread, elem)->wakeup_tick;
}

static int set_global_tick(int64_t tick)
{
	if (global_tick <= tick) /* 입력받은 tick이 global_tick보다 크면 예외처리 */
		return 0;

	/* global_tick 갱신 */
	global_tick = tick;
	return 1;
}

/**
 * @brief 두 쓰레드의 깨어날 시간을 비교하는 함수
 *
 * @param a_ 첫 번째 쓰레드의 리스트 요소
 * @param b_ 두 번째 쓰레드의 리스트 요소
 * @param UNUSED 사용되지 않는 매개변수
 * @return true 첫 번째 쓰레드의 깨어날 시간이 두 번째 쓰레드의 깨어날 시간보다 이전인 경우
 * @return false 그렇지 않은 경우
 */
static bool wakeup_less(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->wakeup_tick < b->wakeup_tick;
}

/* NOTE: priority-insert-ordered
- priority 비교 함수 구현
*/
// static cmp_priority(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
// {
// 	const struct thread *a = list_entry(a_, struct thread, elem);
// 	const struct thread *b = list_entry(b_, struct thread, elem);

// 	return a->priority > b->priority;
// }

bool compare_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED)
{
	return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
	//++ 우선순위 비교해주는 함수 (list_insert_ordered에 인자로 넣어줌)
	// a가 더 크면 true, b가 더 크면 false 반환
}

/* NOTE: [Part3] recent_cpu와 nice를 이용해 priority를 계산하는 함수 구현 */
void thread_calc_priority(struct thread *t)
{
	fixed_point quarter_cpu = div_fp(t->recent_cpu, int_to_fp(4));
	int cpu_to_priority = fp_to_int_round_zero(quarter_cpu);
	int nice_to_priority = t->nice * 2;

	t->priority = PRI_MAX - cpu_to_priority - nice_to_priority;
}

/* NOTE: [Part3] recent_cpu를 계산하는 함수 구현 */
void thread_calc_recent_cpu(struct thread *t)
{
	/* 계산에 필요한 정수를 고정 소수점 값으로 변경 */
	fixed_point one = int_to_fp(1);
	fixed_point two = int_to_fp(2);

	/* decay 계산 */
	fixed_point double_load_avg = mul_fp(two, load_avg);
	fixed_point double_load_avg_plus_one = add_fp(double_load_avg, one);
	fixed_point decay = div_fp(double_load_avg, double_load_avg_plus_one);

	/* 감쇄된 recent_cpu 및 고정 소수점 값으로 변환한 nice */
	fixed_point decayed_recent_cpu = mul_fp(decay, t->recent_cpu);
	fixed_point nice_fp = int_to_fp(t->nice);

	t->recent_cpu = add_fp(decayed_recent_cpu, nice_fp);
}

/* NOTE: [Part3] load_avg를 계산하는 함수 구현 */
void calc_load_avg()
{
	/* 계산에 필요한 가중치 계산 */
	fixed_point weight_59 = div_fp(int_to_fp(59), int_to_fp(60));
	fixed_point weight_1 = div_fp(int_to_fp(1), int_to_fp(60));

	/* read_thread 계산: ready_list에 담긴 쓰레드의 개수 + 실행 중인 쓰레드의 개수 (idle 제외) */
	fixed_point count_ready_threads = int_to_fp(list_size(&ready_list));
	if (thread_current() != idle_thread)
		count_ready_threads = add_fp(count_ready_threads, int_to_fp(1));

	/* 가중치 적용 */
	fixed_point weighted_avg = mul_fp(weight_59, load_avg);
	fixed_point weighted_ready_threads = mul_fp(weight_1, count_ready_threads);

	load_avg = add_fp(weighted_avg, weighted_ready_threads);
}

/* NOTE: [Part3] recent_cpu를 1씩 증가시키는 함수 구현 */
void thread_incr_recent_cpu()
{
	struct thread *curr = thread_current();

	if (curr != idle_thread)
		curr->recent_cpu = add_fp(curr->recent_cpu, int_to_fp(1));
}

/* NOTE: [Part3/Improve] `모든` 쓰레드의 우선순위를 재계산하는 함수 구현 */
void thread_all_calc_priority()
{
	struct list_elem *e;
	struct thread *t;

	e = list_begin(&all_list);
	while (e != list_end(&all_list))
	{
		t = list_entry(e, struct thread, all_elem);
		thread_calc_priority(t);
		e = list_next(e);
	}
}

/* NOTE: [Part3/Improve] `모든` 쓰레드의 recent_cpu를 재계산하는 함수 구현 */
void thread_all_calc_recent_cpu()
{
	struct list_elem *e;
	struct thread *t;

	e = list_begin(&all_list);
	while (e != list_end(&all_list))
	{
		t = list_entry(e, struct thread, all_elem);
		thread_calc_recent_cpu(t);
		e = list_next(e);
	}
}