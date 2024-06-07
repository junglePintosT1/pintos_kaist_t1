/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <hash.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/palloc.h" // palloc_get_page 함수를 사용하기 위한 헤더 파일
#include "threads/init.h" // PANIC 매크로를 사용하기 위한 헤더 파일
#include "threads/vaddr.h"
#include "threads/thread.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
* page, do not create it directly and make it through this function or
* `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* 메모리 주소에 이미 페이지가 있는지 확인 */
	if (spt_find_page (spt, upage) != NULL) {
		return false;
	}

	/*  페이지 구조체 생성 및 초기화 */
	struct page *new_page = (struct page *)malloc(sizeof(struct page));

	if (new_page == NULL)
		return false;

	switch (VM_TYPE(type)) {
		case VM_ANON:
			uninit_new(new_page, upage, init, type, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
			break;
	}

	// uninit_new 내부에 writable 관련 항목이 없기 때문에 호출 후에 수정
	new_page -> writable = writable;


	/* supplement page table에 삽입 */
	if(!spt_insert_page(spt, new_page)) {
		free(new_page);
		return false;
	}

	return true;

err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = (struct page *)malloc(sizeof(struct page));

	page -> va = pg_round_down(va);
	
	struct hash_elem *e = hash_find(&spt -> vm, &page -> elem);

	free(page);

	return e != NULL ? hash_entry(e, struct page, elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	return hash_insert(&spt -> vm, &page -> elem) == NULL ? true : false; // hase_insert의 결과 값이 NULL인 경우 true
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	void *kva = palloc_get_page(PAL_USER);

	if (kva == NULL)
		PANIC("TODO:");

	frame = (struct frame* )malloc(sizeof(struct frame));
	frame -> kva = kva;
	frame -> page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
  void *ordered_addr = pg_round_down(addr);
	vm_alloc_page(VM_ANON | VM_MARKER_0, ordered_addr, 1);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	// addr 주소 유효성 검사
	if (addr == NULL || is_kernel_vaddr(addr))
		return false;

	/* Page Fault 원인을 검사 */
	if ( not_present ) {
		struct thread *t = thread_current();

		void * rsp = !user ? t -> utok_rsp : f -> rsp;

		// 스택 확장 관련 Stack Growth 처리
		if (addr <= USER_STACK && rsp - 8 >= USER_STACK - MAX_STACK_SIZE && addr == rsp - 8) {
			vm_stack_growth(addr);
		}
		else if ( rsp >= USER_STACK - MAX_STACK_SIZE && addr >= rsp && addr <= USER_STACK ) {
			vm_stack_growth(addr);
		} 


		/* Supplemetal Page Table 참조 */
		struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;

		/* Page Fault 대상 page 컨텐츠 load */
		struct page *page = spt_find_page(spt, addr);
		
		if (page == NULL)
			return false;
		
		/* write 불가능한 페이지에 접근한 경우 */
		if (write == 1 && page -> writable == 0)
			return false;

		/* 페이지 테이블 갱신 */
		return vm_do_claim_page (page);
	}

	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = spt_find_page(&thread_current() -> spt, va);

	if (page == NULL)
		return false;

	return vm_do_claim_page (page); // 해당 페이지에 프레임 할당
}

/* Claim the PAGE and set up the mmu. *
/*  가상 메모리 주소를 물리 메모리 페이지에 매핑하는 실제 작업을 수행하는 함수 */
/* 
 * 구체적으로, vm_do_claim_page 함수는 다음과 같은 작업을 수행합니다:
 * 1. 물리 프레임 할당: vm_get_frame 함수를 호출하여 물리 프레임을 할당받습니다.
 * 2. 페이지와 프레임 연결: 페이지 구조체와 할당받은 프레임을 연결합니다.
 * 3. 페이지 테이블 설정: 가상 주소와 물리 주소의 매핑 정보를 페이지 테이블에 추가합니다.
 */
static bool
vm_do_claim_page (struct page *page) {
	/* vm_get_frame 함수를 호출함으로써 프레임 하나를 얻습니다. */
	struct frame *frame = vm_get_frame ();

	/* page와 frame을 연결한다. */
	frame->page = page;
	page->frame = frame;

	struct thread *t = thread_current();
	/* MMU를 세팅해야하는데, 이는 가상 주소와 물리 주소를 매핑한 정보를 페이지 테이블에 추가해야 한다는 것을 의미합니다. */
	/* 해당 주소에 페이지를 매핑합니다. */
	if (pml4_get_page(t->pml4, page -> va) == NULL && pml4_set_page(t->pml4, page -> va, frame -> kva, page -> writable))
		return swap_in (page, frame->kva);

	return false;
}

/* Initialize new supplemental page table */
void
 supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// initial hash table with hash_init()
	hash_init(&spt -> vm, supplement_hash_func, supplement_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
	struct supplemental_page_table *src UNUSED) {	

	// src의 해시 테이블을 순회하기 위한 구조체 선언
	struct hash_iterator i;

	// hash의 첫번째 요소를 hash_iterator에 설정
	hash_first (&i, &src -> vm);

	// hash table 내부를 순회 
	while (hash_next (&i))
   {
   	struct page *curr_page = hash_entry(hash_cur (&i), struct page, elem);
		if ( curr_page == NULL )
			return false;

		enum vm_type type = curr_page -> operations -> type;
		void *upage = curr_page -> va;
		bool writable = curr_page -> writable;

		// uninit 타입 페이지는 spt에 페이지만 로드 되어있을 뿐, 관련된 프레임과는 매핑되어 있지 않은 상태
		// src의 spt entry의 값을 통해 새로운 페이지를 생성
		if ( type == VM_UNINIT ) {
			vm_alloc_page_with_initializer(VM_ANON, upage, writable, curr_page -> uninit.init, curr_page -> uninit.aux);

			// uninit 페이지가 생성되었다면 다음 테이블 엔트리 처리
			continue;
		}

		// anonymous, file_mapped 타입인 경우 uninit 상태에서 pagefault로 컨텐츠가 로드되었거나, 직접 로드된 상태의 페이지를 의미
		// 해당 페이지는 lazy loading 없이 직접 페이지를 로드
		if ( !vm_alloc_page_with_initializer(type, upage, writable, NULL, NULL) )
			return false;


		// 페이지 로드 후 프레임과 매핑 ( 매핑만 )
		if ( !vm_claim_page(upage) )
			return false;


		// dst 해시 테이블 내부에 페이지가 들어있는지 확인
		struct page *new_p = spt_find_page(dst, upage);

		if (new_p == NULL || new_p->frame == NULL) {
					return false;
		}

		// 프레임 내부에 정보를 페이지 한 개만큼 복사
		memcpy(new_p -> frame -> kva, curr_page -> frame -> kva, PGSIZE);
   }

		return true;
}

void hash_page_destroy (struct hash_elem *e, void *aux UNUSED) {

	struct page *page = hash_entry(e, struct page, elem);
	destroy(page);
	free(page);	

}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* Destroy all the supplemental_page_table hold by thread
	 * -> 스레드가 가지고 있는 supplemental page table을 모두 destroy 한다.
	 * TODO: writeback all the modified contents to the storage.
	 * -> 수정된 컨텐츠에 대해서 모두 기록한다.
	 * */
	hash_clear(&spt -> vm, hash_page_destroy);

	// hash_destroy()
}

uint64_t supplement_hash_func ( const struct hash_elem *e, void *aux) {
  /* `hash_entry()로 element에 대한 vm_entry 구조체 검색 */
  struct page *p = hash_entry(e, struct page, elem);
  /* hash_int()를 이용해서 vm_entry의 멤버 vaddr에 대한 해시값을 구하고 반환 */
  return hash_bytes(&p -> va, sizeof p->va); 
}

bool supplement_less_func ( const struct hash_elem *a, const struct hash_elem *b) {
  /* hash_entry()로 각각의 element에 대한 vm_entry 구조체를 얻은 후  vaddr 비교(b가 크다면 True, a가 크다면 False) */

  struct page *p_a = hash_entry(a, struct page, elem);
  struct page *p_b = hash_entry(b, struct page, elem);

  return p_a -> va < p_b -> va;
}
