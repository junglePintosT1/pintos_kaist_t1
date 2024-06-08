/* vm.c: 가상 메모리 객체에 대한 일반적인 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/threads/vaddr.h"

void hash_action_copy (struct hash_elem *e, void *hash_aux);

/* 각 서브시스템의 초기화 코드를 호출하여 가상 메모리 서브시스템을 초기화합니다. */
void
vm_init (void) {
    vm_anon_init ();
    vm_file_init ();
#ifdef EFILESYS  /* 프로젝트 4를 위해 */
    pagecache_init ();
#endif
    register_inspect_intr ();
    /* 상단의 라인은 수정하지 마세요. */
    /* TODO: 여기에 코드를 작성하세요. */
}

/* 페이지의 유형을 가져옵니다. 이 함수는 페이지가 초기화된 후의 유형을 알고 싶을 때 유용합니다.
 * 이 함수는 현재 완전히 구현되어 있습니다. */
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

/* PDG 해쉬용 함수 */
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

/* 초기화기를 사용하여 보류 중인 페이지 객체를 만듭니다. 
페이지를 만들려면 직접 만들지 말고 이 함수나 'vm_alloc_page'를 통해 만드세요. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
        vm_initializer *init, void *aux) {
    ASSERT (VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current ()->spt;

    /* upage가 이미 사용 중인지 확인합니다. */
    if (spt_find_page (spt, upage) == NULL) {
        /* TODO: 페이지를 만들고 VM 유형에 따라 초기화기를 가져오고,
         * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 만듭니다.
         * TODO: uninit_new를 호출한 후에 필드를 수정해야 합니다. */
        struct page *page = malloc(sizeof(struct page));
        if (page == NULL) {
            goto err; // 페이지 할당 실패
        }
        switch(VM_TYPE(type)){
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
        
        page->writable = writable;
        
        /* TODO: 페이지를 spt에 삽입합니다. */
        if (!spt_insert_page(spt, page)) {
            free(page);// spt 삽입 실패 시 메모리 해제
            goto err;
        }

        return true;

    }
err:
    return false;
}

/* SPT에서 VA를 찾아서 페이지를 반환합니다. 오류 시 NULL을 반환합니다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
    /* TODO: 이 함수를 채워주세요. */
    /* PDG 임시 페이지 할당 : 페이지구조 크기가 클 경우 
       지역변수로 생성시 스택오버플로우 가능성 있기에 malloc 동적 할당 */
    struct page *p = malloc(sizeof(struct page));
    /* PDG 임시 엘리먼트 생성 */
    struct hash_elem *e;
    /* PDG 임시 페이지에 찾기 위한 va 설정, 
     * 오프셋 문제로 pg_round_down 사용
     * 해쉬함수에 오프셋 없이 va가 들어가기때문인 것으로 추정
     */
    p->va = pg_round_down(va);
    /* PDG 임시 엘리먼트에 hash에서 찾은 엘리먼트를 넣기, 없으면 NULL
     * 임시 엘리먼트에 셋팅한 elem으로 찾는것이 가능한 이유? 
     * => 해쉬 함수도 va만을 이용하고, 비교함수도 va 만을 사용하기 때문에 */
    e = hash_find(&spt->spt_hash, &p->hash_elem);
    /* PDG 임시 페이지 FREE */
    free(p);
    return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* 페이지를 유효성 검사하면서 spt에 삽입합니다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
        struct page *page UNUSED) {
    bool succ = false;
    /* TODO: 이 함수를 채워주세요. */
    /* PDG SPT insert */
    return hash_insert(&spt->spt_hash, &page->hash_elem) == NULL ? true : false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
    /* PDG SPT delete */
    bool succ = false;
    if(hash_delete(&spt->spt_hash, &page->hash_elem) != NULL){
        succ = true;
        vm_dealloc_page (page);
    }
    return succ;
}

/* 희생자를 가져옵니다. */
static struct frame *
vm_get_victim (void) {
    struct frame *victim = NULL;
     /* TODO: 퇴거 정책은 여러분에게 달려 있습니다. */

    return victim;
}

/* 페이지를 하나 퇴거하고 해당하는 프레임을 반환합니다.
 * 오류 시 NULL을 반환합니다. */
static struct frame *
vm_evict_frame (void) {
    struct frame *victim UNUSED = vm_get_victim ();
    /* TODO: 희생자를 스왑아웃하고 퇴거된 프레임을 반환합니다. */

    return NULL;
}

/* palloc()을 사용하여 프레임을 가져옵니다. 
사용 가능한 페이지가 없으면 페이지를 퇴거하고 반환합니다. 
이 함수는 항상 유효한 주소를 반환합니다. 
즉, 사용자 풀 메모리가 가득 찬 경우 
사용 가능한 메모리 공간을 얻기 위해 프레임을 퇴거합니다. */
static struct frame *
vm_get_frame (void) {
    struct frame *frame = NULL;
    /* TODO: 이 함수를 채워주세요. */
    frame = malloc(sizeof(struct frame));
    frame->kva = palloc_get_page(PAL_USER);
    frame->page = NULL;
    
    ASSERT (frame != NULL);
    ASSERT (frame->page == NULL);
    return frame;
}

/* 스택을 확장합니다. */
static void
vm_stack_growth (void *addr UNUSED) {
    bool success;
    addr = pg_round_down(addr);
    while(spt_find_page(&thread_current()->spt, addr) == NULL){
        if (vm_alloc_page(VM_ANON|VM_MARKER_0, addr, true))
        {
            success = vm_claim_page(addr);
        }
        if(!success){
            palloc_free_page(addr);
        }else{
            addr += PGSIZE;
        }
    }
}

/* write_protected 페이지에 대한 오류 처리 */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* 성공 시 true를 반환합니다. */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
        bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
    
    struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
    struct page *page;

    /* TODO: fault를 유효성 검사합니다. */
    /* TODO: 여기에 코드를 작성하세요 */
    if (!is_user_vaddr(addr) || addr == NULL) {
        return false; // 주소가 유효하지 않은 경우 false를 반환합니다.
    }
    if(USER_STACK - (1 << 20) <= f->rsp - 8 && f->rsp - 8 == addr && addr <= USER_STACK){
        if(!user){
            return false;
        }
        vm_stack_growth(addr);
        return true;
    }
    else if(USER_STACK - (1 << 20) <= f->rsp && f->rsp <= addr && addr <= USER_STACK){
        vm_stack_growth(addr);
        return true;
    }


    page = spt_find_page (spt, addr);

	if (page == NULL) {
		return false;
    }
    


    return vm_do_claim_page (page);
}

/* 페이지를 해제합니다. */
void
vm_dealloc_page (struct page *page) {
    destroy (page);
    free (page);
}

/* VA에서 할당된 페이지를 가져옵니다. */
bool
vm_claim_page (void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: 이 함수를 채워주세요 */
    
    page = spt_find_page(&thread_current()->spt, va);
    
    if (page == NULL)
		return false;
        
    return vm_do_claim_page (page);
}

/* 페이지를 요청하고 mmu를 설정합니다. */
static bool
vm_do_claim_page (struct page *page) {
    struct frame *frame = vm_get_frame ();

    /* 링크 설정 */
    frame->page = page;
    page->frame = frame;

    /* TODO: 페이지 테이블 항목을 삽입하여 페이지의 VA를 프레임의 PA에 매핑합니다. */
  	struct thread *current = thread_current();

    if(pml4_get_page(thread_current()->pml4, page->va) == NULL && pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
        return swap_in (page, frame->kva);
    return false;
}

/* 새 보조 페이지 테이블을 초기화합니다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
    hash_init(&spt->spt_hash, page_hash, page_less, NULL);
    lock_init(&spt->lock);
}

/* src에서 dst로 보조 페이지 테이블을 복사합니다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED) {
    lock_acquire(&src->lock);
    //보조 데이터 셋팅
    src->spt_hash.aux = dst;
    //엘리먼트 전체 순회 하면서 hash_action_copy 동작 수행 
    hash_apply(&src->spt_hash, hash_action_copy);
    lock_release(&src->lock);
    return true;
}

/* 보충 페이지 테이블로 부터 자원을 FREE 하십시오 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: 스레드에 의해 보유되고 있는 모든 보조 페이지 테이블을 소멸시키고
 	   TODO: 수정된 모든 내용을 스토리지에 기록합니다. */
    hash_clear(&spt->spt_hash, NULL);
}

/* PDG 페이지의 주소로 해시 키 생성 */
unsigned 
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);
  
  return a->va < b->va;
}

void hash_action_copy (struct hash_elem *e, void *hash_aux){
	struct thread *t = thread_current();

	struct page *page = hash_entry(e, struct page, hash_elem);
	enum vm_type type = page->operations->type; 

	if(VM_TYPE(type) == VM_UNINIT){
		vm_alloc_page_with_initializer(page->uninit.type, page->va, page->writable, page->uninit.init, page->uninit.aux);
	}
	if(VM_TYPE(type) == VM_ANON){
		vm_alloc_page(type, page->va, page->writable);
		struct page *newpage = spt_find_page(&t->spt, page->va); 
		vm_do_claim_page(newpage);
		memcpy(newpage->frame->kva, page->frame->kva, PGSIZE);
	}
	if(VM_TYPE(type) == VM_FILE){
		vm_alloc_page(type, page->va, page->writable);
		struct page *newpage = spt_find_page(&t->spt, page->va); 
		vm_do_claim_page(newpage);
		memcpy(newpage->frame->kva, page->frame->kva, PGSIZE);
	}
}

