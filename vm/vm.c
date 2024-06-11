/* vm.c: Generic interface for virtual memory objects. */
#include <string.h>
#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/mmu.h"
#include "userprog/syscall.h"

static struct frame_table frame_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
	/* NOTE: frame table의 frame_list 초기화 */
	list_init(&frame_table.frame_list);
	frame_table.curr_frame = list_tail(&frame_table.frame_list);
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
	ASSERT(VM_TYPE(type) != VM_UNINIT);
	ASSERT(pg_ofs(upage) == 0); /* upage 주소가 경계값이 맞는지 확인 */

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* NOTE: [VM] vm_alloc_page_with_initializer 구현 */
		struct page *page = (struct page *)malloc(sizeof(struct page)); /* page 구조체 할당 */
		if (page == NULL)
			goto err;

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

		page->writable = writable;		/* page에 쓰기 가능 여부 설정 */
		page->owner = thread_current(); /* page owner thread 설정 */
		/* 현재 프로세스의 보조 페이지 테이블에 생성한 페이지 추가 */
		if (!spt_insert_page(&thread_current()->spt, page))
		{
			free(page);
			goto err;
		}
		return true;
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
		PANIC("spt find page");
	page->va = pg_round_down(va);				 /* 할당된 page 구조체의 가상 주소 설정 */
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
	return hash_insert(&spt->hash, &page->hash_elem) == NULL ? true : false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	/* NOTE: [VM] spt의 hash에서 해당 페이지 삭제 */
	hash_delete(&spt->hash, &page->hash_elem);
	vm_dealloc_page(page);
	return true;
}

static bool vm_find_victim(struct frame *frame)
{
	bool is_victim = true;
	for (struct list_elem *pe = list_begin(&frame->page_list); pe != list_end(&frame->page_list); pe = list_next(pe))
	{
		struct page *page = list_entry(pe, struct page, f_elem);
		if (pml4_is_accessed(page->owner->pml4, page->va))
		{
			pml4_set_accessed(page->owner->pml4, page->va, 0);
			is_victim = false;
		}
		uint64_t *pte = pml4e_walk(page->owner->pml4, page->va, 0);
		if (is_kern_pte(pte))
			is_victim = false;
	}
	return is_victim;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	/* NOTE: The policy for eviction is up to you. */
	struct list_elem *start_elem = frame_table.curr_frame;
	for (struct list_elem *e = start_elem; e != list_end(&frame_table.frame_list); e = list_next(e))
	{
		// frame의 page list 순회하며 accessed 확인
		struct frame *frame = list_entry(e, struct frame, ft_elem);

		if (vm_find_victim(frame))
			return frame;
	}
	for (struct list_elem *e = list_begin(&frame_table.frame_list); e != list_end(&frame_table.frame_list); e = list_next(e))
	{
		// frame의 page list 순회하며 accessed 확인
		struct frame *frame = list_entry(e, struct frame, ft_elem);
		if (vm_find_victim(frame))
			return frame;
	}
	return list_entry(list_begin(&frame_table.frame_list), struct frame, ft_elem);
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	/* clock pointer 위치 갱신 */
	frame_table.curr_frame = list_next(&victim->ft_elem);

	for (struct list_elem *pe = list_begin(&victim->page_list); pe != list_end(&victim->page_list); pe = list_next(pe))
	{
		struct page *page = list_entry(pe, struct page, f_elem);
		swap_out(page);
		page->frame = NULL;
		list_remove(&page->f_elem);
	}
	list_remove(&victim->ft_elem);

	return victim;
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
	struct frame *frame = malloc(sizeof(struct frame));
	list_init(&frame->page_list);

	/* NOTE: [VM] 모든 유저 페이지를 위한 프레임은 PAL_USER을 통해 할당해야 함 */
	frame->kva = palloc_get_page(PAL_USER);

	/* NOTE: 페이지 할당 실패 시 swap out 구현 */
	if (frame->kva == NULL)
	{
		frame = vm_evict_frame();
	}

	/* NOTE: swap in 시 frame table에 추가 및 curr_frame 갱신 */
	list_push_back(&frame_table.frame_list, &frame->ft_elem);

	/* NOTE: frame의 page_list 초기화 */
	ASSERT(frame != NULL);
	ASSERT(list_empty(&frame->page_list));

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr)
{
	/** TODO: [VM] 스택 증가 함수 구현
	 * - 하나 이상의 anonymous 페이지를 할당하여 스택 크기를 늘림
	 * - 스택의 크기가 증가함에 따라, `addr`은 faulted 주소에서 유효한 주소가 된다.
	 * - 페이지를 할당할 때는 주소를 페이지 경계로 내림해야 한다.
	 * - 스택 크기 최대 1MB로 제한해야 한다.
	 */
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), true);
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
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr,
						 bool user UNUSED, bool write, bool not_present)
{
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page = NULL;

	if (addr == NULL)
		return false;

	if (is_kernel_vaddr(addr))
		return false;

	if (!not_present)
		return false;

	void *rsp = f->rsp;
	if (!user)
		rsp = thread_current()->user_rsp;
	/* 스택에 접근하는 경우 */
	// 스택 확장으로 처리할 수 있는 폴트인 경우, vm_stack_growth를 호출한다.
	if (USER_STACK - MAX_STACK_SIZE <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK)
		vm_stack_growth(addr);
	else if (USER_STACK - MAX_STACK_SIZE <= rsp && rsp <= addr && addr <= USER_STACK)
		vm_stack_growth(addr);

	page = spt_find_page(spt, addr);
	if (page == NULL)
		return false;

	if (write == 1 && page->writable == 0)
		return false;

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
	if (frame == NULL)
		return false;

	/* Set links */
	// frame->page = page;
	/* NOTE: frame의 page_list에 page 추가 */
	list_push_back(&frame->page_list, &page->f_elem);
	page->frame = frame;

	/* NOTE: 페이지 테이블에 페이지의 VA와 프레임의 PA를 삽입 - install_page 참고 */
	if (pml4_get_page(thread_current()->pml4, page->va) == NULL && pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
		return swap_in(page, frame->kva);
	return false;
}

/**
 * @brief
 *
 * @param p_
 * @param UNUSED
 * @return unsigned
 */
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

/**
 * @brief
 *
 * @param a_
 * @param b_
 * @param UNUSED
 * @return true
 * @return false
 */
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
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
	/* TODO: [VM] src부터 dst까지 spt 복사 구현 */
	/* TODO: spt를 순회하면서 정확한 복사본을 만들어라. */
	/* TODO: uninit 페이지를 할당하고 이 함수를 바로 요청할 필요가 있을 것이다. */
	struct hash_iterator i;
	hash_first(&i, &src->hash);
	while (hash_next(&i))
	{
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type type = src_page->operations->type;
		void *upage = src_page->va;
		bool writable = src_page->writable;
		if (type == VM_UNINIT)
		{
			vm_initializer *init = src_page->uninit.init;
			void *aux = src_page->uninit.aux;
			vm_alloc_page_with_initializer(page_get_type(src_page), upage, writable, init, aux);
			continue;
		}

		if (type == VM_FILE)
		{
			struct page_load_info *file_aux = malloc(sizeof(struct page_load_info));
			file_aux->file = src_page->file.file;
			file_aux->offset = src_page->file.offset;
			file_aux->read_bytes = src_page->file.read_bytes;
			// file_aux->zero_bytes = src_page->file.zero_bytes;

			if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux))
				return false;
			struct page *file_page = spt_find_page(dst, upage);
			file_backed_initializer(file_page, type, NULL);
			file_page->frame = src_page->frame;
			pml4_set_page(thread_current()->pml4, file_page->va, src_page->frame->kva, src_page->writable);
			continue;
		}

		if (!vm_alloc_page(type, upage, writable))
			return false;

		if (!vm_claim_page(upage))
			return false;

		struct page *dst_page = spt_find_page(dst, upage);
		if (dst_page == NULL)
			return false;

		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	}
	return true;
}

void hash_action_destroy(struct hash_elem *e, void *aux)
{
	struct page *page = hash_entry(e, struct page, hash_elem);
	vm_dealloc_page(page);
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	/**
	 * @brief GITBOOK
	 * spt에 의해 유지되던 모든 자원들을 free한다.
	 * process_exit()에서 호출된다.
	 * 페이지 엔트리를 순회하면서 테이블의 페이지에 destroy(page)를 호출해야 한다.
	 * 실제 페이지 테이블(pml4)와 물리 주소(palloc된 메모리)에 대해선 고려하지 않아도 된다. (호출자가 그것들을 정리할 것이다.)
	 */
	hash_clear(&spt->hash, hash_action_destroy); /* 🚨 왜 hash_destroy를 사용하면 PANIC이 뜰까?! */
}
