/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "userprog/syscall.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/**
 * @brief file backed 페이지를 초기화하는 함수
 *
 * @param page 초기화할 페이지의 포인터
 * @param type 페이지의 가상 메모리 타입
 * @param kva 커널 가상 주소
 * @return true 초기화가 성공적으로 완료되면 true 반환
 * @return false 초기화가 실패하면 false 반환
 */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	/* NOTE: initializer 호출 시 page의 vm_type은 uninit */
	struct page_load_info *page_load_info = page->uninit.aux;

	/* 파일 페이지 정보를 페이지 로드 정보를 이용해 초기화 */
	file_page->file = page_load_info->file;
	file_page->offset = page_load_info->offset;
	file_page->read_bytes = page_load_info->read_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	/* NOTE: swap in 구현 */
	struct file_page *file_page = &page->file;

	struct file *file = file_page->file;
	off_t offset = file_page->offset;
	size_t read_bytes = file_page->read_bytes;
	size_t zero_bytes = PGSIZE - read_bytes;

	int flag = false;
	if (!lock_held_by_current_thread(&filesys_lock))
	{
		lock_acquire(&filesys_lock);
		flag = true;
	}

	file_seek(file, offset);
	if (file_read(file, page->frame->kva, read_bytes) != (off_t)read_bytes)
	{
		palloc_free_page(page->frame->kva);
		if (flag)
			lock_release(&filesys_lock);
		return false;
	}
	if (flag)
		lock_release(&filesys_lock);
	memset(page->frame->kva + read_bytes, 0, zero_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	/* NOTE: swap out 구현 */
	struct file_page *file_page = &page->file;
	uint64_t *pml4 = thread_current()->pml4;
	void *upage = page->va;

	/* 페이지가 변경되었는지 확인 */
	if (pml4_is_dirty(pml4, page->va))
	{
		lock_acquire(&filesys_lock);
		/* 페이지가 변경되었다면, 변경된 내용을 파일에 쓰고, 페이지의 dirty를 false로 변경 */
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->offset);
		lock_release(&filesys_lock);
		pml4_set_dirty(pml4, upage, false);
	}
	/* pml4에서 페이지 제거 */
	pml4_clear_page(pml4, upage);
}

/**
 * @brief file backed 페이지를 파괴하는 함수
 * 페이지 free는 호출자가 수행함.
 *
 * @param page 파괴할 페이지의 포인터
 */
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page = &page->file;
	uint64_t *pml4 = thread_current()->pml4;
	void *upage = page->va;

	/* 페이지가 변경되었는지 확인 */
	if (pml4_is_dirty(pml4, page->va))
	{
		lock_acquire(&filesys_lock);
		/* 페이지가 변경되었다면, 변경된 내용을 파일에 쓰고, 페이지의 dirty를 false로 변경 */
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->offset);
		lock_release(&filesys_lock);
		pml4_set_dirty(pml4, upage, false);
	}
	/* pml4에서 페이지 제거 */
	pml4_clear_page(pml4, upage);
}

/**
 * @brief 메모리에 파일을 매핑하는 함수
 *
 * @param addr 매핑을 시작할 메모리 주소
 * @param length 매핑할 바이트 수
 * @param writable 매핑된 메모리 영역이 쓰기 가능한지 여부를 나타내는 플래그
 * @param file 매핑할 파일의 포인터
 * @param offset 파일 내에서 매핑을 시작할 바이트 위치
 * @return void* 매핑이 시작된 메모리 주소. 매핑에 실패하면 NULL 반환
 */
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	/* file_reopen을 이용해 각 매핑이 파일에 대해 독립적인 참조를 가지도록 함 */
	struct file *f = file_reopen(file);
	/* 매핑 시작 주소 저장 */
	void *start_addr = addr;
	/* 매핑에 필요한 총 페이지 수 계산 */
	int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1
																  : length / PGSIZE;

	/* 매핑할 바이트 수가 파일의 길이보다 큰 경우, 파일의 길이로 제한 */
	size_t read_bytes = file_length(file) < read_bytes ? file_length(file) : read_bytes;

	while (read_bytes > 0)
	{
		/* 한 페이지에 읽을 바이트 수와 0으로 채울 바이트 수 계산 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* 페이지 로드 정보를 위한 구조체 할당 및 초기화 */
		struct page_load_info *page_load_info = malloc(sizeof(struct page_load_info));
		page_load_info->file = f;
		page_load_info->offset = offset;
		page_load_info->read_bytes = page_read_bytes;
		page_load_info->zero_bytes = page_zero_bytes;

		/* 페이지 할당 및 초기화. 실패 시 NULL 반환 */
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, (void *)page_load_info))
		{
			free(page_load_info);
			return NULL;
		}

		/* 생성된 페이지에 총 페이지 개수 저장 */
		spt_find_page(&thread_current()->spt, addr)->mapped_page_count = total_page_count;

		/* Advance. */
		addr += PGSIZE;
		offset += page_read_bytes;
		read_bytes -= page_read_bytes;
	}
	/* 매핑이 시작된 주소 반환 */
	return start_addr;
}

/**
 * @brief 주어진 주소에서 시작하는 매모리 매핑을 해제하는 함수
 *
 * @param addr 해제를 시작할 메모리 주소
 */
void do_munmap(void *addr)
{
	/* spt에서 addr에 해당하는 페이지를 가져옴 */
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page = spt_find_page(spt, addr);

	/* 매핑된 총 페이지 수 */
	int count = page->mapped_page_count;

	/* 모든 매핑된 페이지 해제*/
	for (int i = 0; i < count; i++)
	{
		if (page)
			spt_remove_page(spt, page);

		/* 다음 페이지로 이동 */
		addr += PGSIZE;
		page = spt_find_page(spt, addr);
	}
}
