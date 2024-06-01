/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

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
	 * @brief GITBOOk
	 * 이 함수에서 당신은 익명 페이지에 관련된 어떤 것이든 설정할 수 있다.
	 */
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;
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
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}
