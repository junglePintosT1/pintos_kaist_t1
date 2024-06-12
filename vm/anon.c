/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* TODO: swap table 만들기~~ */
struct bitmap *swap_table;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages - 익명 페이지 서브시스템 초기화 함수 */
void vm_anon_init(void)
{
	/**
	 * @brief GITBOOK
	 * 이 함수에서 당신은 익명 페이지에 관련된 어떤 것이든 설정할 수 있다.
	 */
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	swap_table = bitmap_create(disk_size(swap_disk));
}

/* Initialize the file mapping - 익명 페이지를 위한 초기화 함수 */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/**
	 * @brief GITBOOK
	 * 현재 비어있는 구조체인 anon_page의 정보를 업데이트할 필요가 있다.
	 * 이 함수는 익명 페이지를 초기화하는데 사용된다. (i.e. VM_ANON)
	 */
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_table_idx = -1;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
	// swap table 참조 swap_table_idx 사용
	// 해당 디스크 내용 읽어오기 disk_read -> frame에 8번 복사
	size_t start_idx = anon_page->swap_table_idx;
	for (int i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
	{
		disk_read(swap_disk, start_idx + i, kva + DISK_SECTOR_SIZE * i);
	}
	// bitmap 초기화
	bitmap_set_multiple(swap_table, start_idx, PGSIZE / DISK_SECTOR_SIZE, false);

	anon_page -> swap_table_idx = -1;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
	uint64_t *pml4 = page->owner->pml4;
	void *upage = page->va;

	// 빈 슬롯 찾을 때 swap table 사용 - bitmap_scan
	size_t start_idx = bitmap_scan_and_flip(swap_table, 0, PGSIZE / DISK_SECTOR_SIZE, false);

	// 스왑 영역 - page 매핑 - bitmap idx 저장
	anon_page->swap_table_idx = start_idx;

	// swap_disk에 복사 - disk_write
	for (int i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
	{
		disk_write(swap_disk, start_idx + i, page->frame->kva + DISK_SECTOR_SIZE * i);
	}

	// frame - page 매핑 해제
	page->frame = NULL;
	list_remove(&page->f_elem);
	pml4_clear_page(pml4, upage);

	/* TODO: swap 영역에 빈 공간 없으면 PANIC */

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	list_remove(&page->f_elem);

	if (anon_page->swap_table_idx != -1) 
		bitmap_set_multiple(swap_table, anon_page->swap_table_idx, PGSIZE / DISK_SECTOR_SIZE, false);
}
