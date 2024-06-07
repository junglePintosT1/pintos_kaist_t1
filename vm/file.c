/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

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

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* NOTE: initializer 호출 시 page의 vm_type은 uninit */
	struct page_load_info *page_load_info = page->uninit.aux;

	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	file_page->offset = page_load_info->offset;
	file_page->length = page_load_info->read_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

static bool
lazy_load_file(struct page *page, void *aux)
{
	struct page_load_info *page_load_info = (struct page_load_info *)aux;

	struct file *file = page_load_info->file;
	off_t offset = page_load_info->offset;
	size_t read_bytes = page_load_info->read_bytes;
	size_t zero_bytes = page_load_info->zero_bytes;

	file_seek(file, offset);
	if (file_read(file, page->frame->kva, read_bytes) != (off_t)read_bytes)
	{
		palloc_free_page(page->frame->kva);
		return false;
	}
	memset(page->frame->kva + read_bytes, 0, zero_bytes);

	return true;
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	/**
	 * TODO: GITBOOK
	 * fd로 열린 파일의 offset 바이트에서 시작하여 length 바이트를 프로세스의 가상 주소 공간의 addr에서 매핑한다.
	 * 파일 전체가 addr에서 시작하는 연속적인 가상 페이지로 매핑된다.
	 * 파일 길이가 PGSIZE의 배수가 아닌 경우, 마지막 매핑된 페이지의 일부 바이트는 0으로 채워진다.
	 * 페이지가 디스크에 다시 쓰여질 때 0으로 채워진 부분은 폐기해야 한다.
	 *
	 * TODO: mmap 호출이 실패하는 경우
	 * 1. fd가 열린 파일의 크기가 0바이트인 경우
	 * 2. addr이 페이지 정렬이 되지 않은 경우
	 * 3. 매핑된 페이지 범위가 스택이나 실행 파일 로드 시 매핑된 페이지와 겹치는 경우
	 */

	/* FIXME: anonymous랑 file_backed랑 어떤 부분에서 다른 처리를 해줘야 하는걸까? */

	/* 예외처리 1: 파일의 크기가 0바이트인 경우 */
	if (file_length(file) <= 0)
		return;
	/* 예외처리 2: addr이 페이지 정렬이 되지 않은 경우 */
	/* FIXME: 🚨 do_mmap에서 페이지 정렬을 시켜줘야 하는건가? 아니면 정렬 안 된 값 들어오면 틀렸다고 해야하나? */
	if (pg_round_down(addr) != addr)
		return;

	/* 예외처리 3-1: 매핑된 페이지 범위가 스택과 겹치는 경우 */
	if (USER_STACK - MAX_STACK_SIZE <= addr)
		return;

	while (length > 0)
	{
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct page_load_info *page_load_info = malloc(sizeof(struct page_load_info));
		page_load_info->file = file;
		page_load_info->offset = offset;
		page_load_info->read_bytes = page_read_bytes;
		page_load_info->zero_bytes = page_zero_bytes;

		/* 예외처리 3-2 : 매핑된 페이지 범위가 실행 파일 로드 시 매핑된 페이지와 겹치는 경우 */
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_do_mmap, (void *)page_load_info))
		{
			free(page_load_info);
			return false;
		}

		/* Advance. */
		addr += PGSIZE;
		offset += page_read_bytes;
		length -= page_read_bytes;
	}
	return true;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	/* TODO: 지정된 주소 범위 addr에 대한 매핑 해제 */
	/* TODO: 프로세스가 종료될 때 모든 매핑이 암시적으로 해제된다. (via exit) */
	/* TODO: 프로세스에 의해 작성된 모든 페이지는 파일에 다시 쓰여야 하고, 작성되지 않은 페이지는 다시 쓰여지지 않아야 한다. */
	/* TODO: 그런 다음 페이지는 프로세스의 가상 페이지 목록에서 제거된다. */

	struct page *page = spt_find_page(&thread_current()->spt, addr);
	if (page == NULL)
		return;
	if (pml4_is_dirty(thread_current()->pml4, page->va))
	{
		file_write_at(page->file.file, page->frame->kva, page->file.length, page->file.offset);
	}
	spt_remove_page(&thread_current()->spt, page);
}
