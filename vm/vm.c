/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/mmu.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{
	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* NOTE: [VM] vm_alloc_page_with_initializer 구현 */
		struct page *page = (struct page *)malloc(sizeof(struct page)); /* page 구조체 할당 */
		page->writable = writable;										/* page에 쓰기 가능 여부 설정 */

		switch (VM_TYPE(type)) /* vm_type에 따라 초기화 설정 분기처리 */
		{
		case VM_ANON:
			uninit_new(page, upage, init, type, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(page, upage, init, type, aux, file_backed_initializer);
			break;
		default: /* 예상치 못한 type이 들어온 경우 예외처리 */
			free(page);
			goto err;
		}

		spt_insert_page(&thread_current()->spt, page); /* 현제 프로세스의 보조 페이지 테이블에 생성한 페이지 추가 */
	}
err:
	return false;
}

/**
 * @brief 주어진 spt에서로부터 가상 주소(va)와 대응되는 페이지 구조체를 찾아서 반환하는 함수
 * 실패했을 경우 NULL을 반환
 *
 * @param spt
 * @param va
 * @return struct page*
 */
struct page *
spt_find_page(struct supplemental_page_table *spt, void *va)
{
	struct page *page = malloc(sizeof(struct page)); /* page 구조체 동적 할당 */
	struct hash_elem *e;

	/* page 동적 할당 실패 시 NULL 반환 */
	if (page == NULL)
		return NULL;

	page->va = va;								 /* 할당된 page 구조체의 가상 주소 설정 */
	e = hash_find(&spt->hash, &page->hash_elem); /* spt의 해시 테이블에서 page의 hash_elem 찾기  */
	free(page);									 /* page 구조체 할당 해제 */

	/* va와 대응하는 page 구조체 반환 */
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/**
 * @brief 인자로 주어진 보조 페이지 테이블에 페이지 구조체를 삽입하는 함수
 * 주어진 spt에서 가상 주소가 존재하지 않는지 검사해야 한다.
 *
 * @param spt
 * @param page
 * @return true
 * @return false
 */
bool spt_insert_page(struct supplemental_page_table *spt,
					 struct page *page)
{
	/* NOTE: [VM] 보조 페이지 테이블에 페이지 구조체 삽입 */
	int succ = false;
	/* 보조 페이지 테이블 해시에 page의 hash_elem 삽입 */
	/* hash_insert는 이전에 중복된 페이지가 없었을 경우 NULL을 반환한다. */
	struct hash_elem *old = hash_insert(&spt->hash, &page->hash_elem);

	/* 중복된 페이지가 없었을 경우 삽입 성공 여부를 true로 변경 */
	if (old == NULL)
		succ = true;

	return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/**
 * @brief palloc()을 호출하여 프레임을 가져오는 함수
 * 사용 가능한 프레임이 없다면, 페이지를 프레임에서 추방(evict)하고 해당 프레임을 반환합니다.
 * 항상 유효한 주소를 반환해야 합니다.
 *
 * @return struct frame*
 */
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* NOTE: [VM] 모든 유저 페이지를 위한 프레임은 PAL_USER을 통해 할당해야 함 */
	frame = palloc_get_page(PAL_USER | PAL_ZERO);

	/* TODO: 페이지 할당 실패 시 swap out 구현 */
	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/**
 * @brief GITBOOK
 * TODO: [VM] vm_try_handle_fault 함수 수정
 *
 * vm_try_handle_fault 함수를 수정하여 spt_find_page를 통해 페이지 폴트 주소에 해당하는 페이지 구조체를 찾아야 한다.
 */
/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/**
 * @brief 주어진 va에 대응하는 페이지를 찾고, 해당 페이지에 프레임을 할당하는 함수
 * Claim the page that allocate on VA.
 *
 * @param va
 * @return true
 * @return false
 */
bool vm_claim_page(void *va)
{
	/* NOTE: va를 위한 페이지를 찾기 - 페이지가 존재하지 않을 때에 대한 처리는 사용하는 곳에서! */
	struct page *page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
		return false;
	/* NOTE: 해당 페이지를 인자로 갖는 vm_do_claim_page 호출 */
	return vm_do_claim_page(page);
}

/**
 * @brief 주어진 page에 물리 메모리 프레임을 할당하는 함수
 *
 * @param page
 * @return true
 * @return false
 */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame(); /* NOTE: [VM] 페이지를 할당할 프레임을 얻음 */

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* NOTE: 페이지 테이블에 페이지의 VA와 프레임의 PA를 삽입 */
	pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);
	return swap_in(page, frame->kva);
}

unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

unsigned page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

/**
 * @brief 보조 페이지 테이블을 초기화하는 함수
 * userprog/process.c의 initd 함수로 새로운 프로세스가 시작하거나
 * process.c의 __do_fork로 자식 프로세스가 생성될 때 함수가 호출된다.
 *
 * @param UNUSED
 */
void supplemental_page_table_init(struct supplemental_page_table *spt)
{
	/* NOTE: 보조 페이지 테이블 초기화 */
	hash_init(&spt->hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
