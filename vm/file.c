/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "userprog/syscall.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	struct aux_struct *aux = (struct aux*)page -> uninit.aux;
	file_page -> file = aux -> file;
	file_page -> offset = aux -> offset;
	file_page -> read_bytes = aux -> read_bytes;
	file_page -> zero_bytes = aux -> zero_bytes;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct thread *curr = thread_current();

	struct file_page *file_page UNUSED = &page->file;

	if (pml4_is_dirty(curr -> pml4, page -> va)) { // 파일이 dirty한 상태이면 수정된 부분 기록

		lock_acquire(&filesys_lock);

		file_write_at(file_page -> file, page -> va, file_page -> read_bytes, file_page -> offset); // 수정된 파일 페이지 만큼 파일 부분 수정
		pml4_set_dirty(curr -> pml4, page -> va, 0); // 페이지의 dirty 인자 수정

		lock_release(&filesys_lock);
	}

	pml4_clear_page(curr -> pml4, page -> va); // 페이지 제거
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

 	// 동일한 매핑에 대해서 서로다른 프로세스의 결과물이 서로에게 영향을 주면 바람직하지 않다.
	// 사용할 때 `file_reopen`을 통해서 새로운 파일 디스크립터를 통해 파일을 생성하면 다른 매핑에 영향을 주지않고 독립적인 파일 핸들링이 가능해진다.
	struct file *reopen_f = file_reopen(file);

	off_t fl = file_length(reopen_f);
	
	size_t read_bytes = fl < length ? fl : length ; //매핑할 총 바이트 수로 초기화
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE; // 마지막에 0으로 채울 바이트의 크기

	int page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1 // 연속적으로 매핑된 페이지의 총 개수
                                                          : length / PGSIZE;    // 모든 페이지에 일괄적으로 동일한 페이지 개수를 전달하기 위해 미리 계산해서 넘겨줌

	printf("sys : 총 %d개 제거\n", page_count);


	void *start_addr = addr; // 시작 주소 리턴을 위해 저장

	ASSERT((read_bytes + zero_bytes) % PGSIZE  == 0)
	ASSERT(pg_ofs(addr) == 0)
	ASSERT(offset % PGSIZE == 0)

	// 총 길이 read_bytes(length)만큼 page 단위로 반복하면서 가상 페이지에 매핑 
	while ( read_bytes > 0 || zero_bytes > 0 ) {

		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct aux_struct *aux = (struct aux_struct *)malloc(sizeof(struct aux_struct)); // aux( lazy_load_segment를 위한 데이터 )를 저장할 공간을 동적할당.
		if (aux == NULL)
			return false;

		aux -> file = reopen_f;
		aux -> offset = offset;
		aux -> read_bytes = page_read_bytes;
		aux -> zero_bytes = page_zero_bytes;
		
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux))
			return NULL;

		struct page * p = spt_find_page(&thread_current() -> spt, start_addr);
		p -> page_count = page_count;

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		offset += page_read_bytes;
		addr += PGSIZE;
	}

	return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) { // - addr는 매핑 해제되지 않은 프로세서의 mmap에 대한 이전 호출에서 반환된 가상 주소

	//  - exit이나 다른 방법을 통해서 프로세스가 종료된 경우, 프로세스가 사용한 모든 페이지에 대해서 파일에 다시 기록되며, 기록되지 않은 파일에 대해서는 파일에 다시 기록되지 않아야 한다.
	// - 그 후, 페이지는 프로세스의 가상 페이지 목록에서 제거됨
	// - 파일을 닫거나 제거해도 매핑은 해제되지 않음
	// - munmap이 호출되거나, 프로세스가 종료될 때까지 유효
	// - 파일에 대한 독립적인 참조를 얻기 위해 `file_reopen` 를 사용
  // 	  - `file_reopen`

	uintptr_t cur_addr = addr;
	struct thread *curr = thread_current();
	struct page *p = spt_find_page(&curr -> spt, cur_addr);
	
	// 매핑된 페이지를 페이지 개수만큼 순회
	for (int c = 0; c < p -> page_count; c++) {
		printf("sys : 여서 오고\n");
		// 페이지 제거
		if (p)
			destroy(p);
		
		printf("sys : 여긴 안나올거고\n");

		// 다음 페이지
		cur_addr += PGSIZE;
		p = spt_find_page(&curr -> spt, cur_addr);
		printf("sys : %d개 째 제거\n", c);
	}

}
