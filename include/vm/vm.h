#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include "lib/kernel/hash.h"

enum vm_type
{
	/* page not initialized */
	/* 초기화되지 않은 페이지 */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	/* anonymous page */
	VM_ANON = 1,
	/* page that realated to the file */
	/* file-backed page */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
struct page
{
	const struct page_operations *operations;
	void *va;			 /* Address in terms of user space */
	struct frame *frame; /* Back reference for frame */

	/* Your implementation */

	/* NOTE: [VM] 추가적인 정보 추가 */
	struct hash_elem hash_elem; /* SPT에 넣을 hash_elem */
	bool writable;				/* 쓰기 가능 여부 */
	int mapped_page_count;		/* 매핑된 총 페이지의 개수 */

	/* NOTE: frame에 넣을 elem 추가 */
	struct list_elem f_elem;

	/* NOTE: page owner thread 추가 */
	struct thread *owner;

	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	union
	{
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
struct frame
{
	void *kva;
	/* NOTE: frame 구조체 변경 - page_list & frame_table에 넣을 elem */
	// struct page *page;
	struct list page_list;
	struct list_elem ft_elem;
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
struct page_operations
{
	bool (*swap_in)(struct page *, void *);
	bool (*swap_out)(struct page *);
	void (*destroy)(struct page *);
	enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in((page), v)
#define swap_out(page) (page)->operations->swap_out(page)
#define destroy(page)                \
	if ((page)->operations->destroy) \
	(page)->operations->destroy(page)

/* NOTE: [VM] supplemental_page_table 구조체 선언 */
struct supplemental_page_table
{
	struct hash hash; /* hash 자료구조로 구현 */
};

/* NOTE: frame table 구조체 선언 */
struct frame_table
{
	struct list_elem *curr_frame;
	struct list frame_list;
};

#include "threads/thread.h"
void supplemental_page_table_init(struct supplemental_page_table *spt);
bool supplemental_page_table_copy(struct supplemental_page_table *dst,
								  struct supplemental_page_table *src);
void supplemental_page_table_kill(struct supplemental_page_table *spt);
struct page *spt_find_page(struct supplemental_page_table *spt,
						   void *va);
bool spt_insert_page(struct supplemental_page_table *spt, struct page *page);
void spt_remove_page(struct supplemental_page_table *spt, struct page *page);

void vm_init(void);
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user,
						 bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
									bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page(struct page *page);
bool vm_claim_page(void *va);
enum vm_type page_get_type(struct page *page);

struct page_load_info
{
	struct file *file;
	off_t offset;
	size_t read_bytes;
	size_t zero_bytes;
};

#endif /* VM_VM_H */
