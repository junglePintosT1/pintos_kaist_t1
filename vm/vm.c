/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

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
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
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
	struct page *page = NULL;
	/* TODO: Fill this function. */

	return page;
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
	int succ = false;
	/* TODO: Fill this function. */

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
	/* TODO: Fill this function. */
	/* TODO: [VM] 모든 유저 페이지를 위한 프레임은 PAL_USER을 통해 할당해야 함 */
	/* TODO: [VM] 스와핑 구현 전에는 페이지 할당 실패 시 PANIC("todo")를 호출할 것 */

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

/* Claim the page that allocate on VA. */

/**
 * @brief 주어진 va에 페이지를 할당하고, 해당 페이지에 프레임을 할당하는 함수
 *
 * @param va
 * @return true
 * @return false
 */
bool vm_claim_page(void *va)
{
	struct page *page = NULL;
	/* TODO: Fill this function */
	/* TODO: va를 위한 페이지를 얻음 */
	/* TODO: 해당 페이지를 인자로 갖는 vm_do_claim_page 호출 */
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

	/* TODO: 페이지 테이블에 페이지의 VA와 프레임의 PA를 삽입해야 한다. (페이지와 프레임의 매핑을 위함) */

	return swap_in(page, frame->kva);
}

/**
 * @brief 보조 페이지 테이블을 초기화하는 함수
 * userprog/process.c의 initd 함수로 새로운 프로세스가 시작하거나
 * process.c의 __do_fork로 자식 프로세스가 생성될 때 함수가 호출된다.
 *
 * @param UNUSED
 */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: 보조 페이지 테이블 초기화 구현 */
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
