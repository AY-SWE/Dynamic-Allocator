#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"
#define TEST_TIMEOUT 15

#include "helperHeader.h"

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == (bp->header & ~0x1f))
		cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

/*
 * Assert that the free list with a specified index has the specified number of
 * blocks in it.
 */
void assert_free_list_size(int index, int size) {
    int cnt = 0;
    sf_block *bp = sf_free_list_heads[index].body.links.next;
    while(bp != &sf_free_list_heads[index]) {
	cnt++;
	bp = bp->body.links.next;
    }
    cr_assert_eq(cnt, size, "Free list %d has wrong number of free blocks (exp=%d, found=%d)",
		 index, size, cnt);
}

Test(sfmm_basecode_suite, malloc_an_int, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	int *x = sf_malloc(sizeof(int));

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_free_block_count(0, 1);
	assert_free_block_count(1952, 1);
	assert_free_list_size(7, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_basecode_suite, malloc_four_pages, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;

	// We want to allocate up to exactly four pages.
	void *x = sf_malloc(16288);
	cr_assert_not_null(x, "x is NULL!");
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sfmm_basecode_suite, malloc_too_large, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
        void *x = sf_malloc(PAGE_SZ * 100);

	cr_assert_null(x, "x is not NULL!");
	assert_free_block_count(0, 1);
	assert_free_block_count(36800, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sfmm_basecode_suite, free_no_coalesce, .timeout = TEST_TIMEOUT) {		//case 1 of coalesce
	sf_errno = 0;
	/* void *x = */ sf_malloc(8);
	void *y = sf_malloc(200);
	/* void *z = */ sf_malloc(1);

	sf_free(y);

	assert_free_block_count(0, 2);
	assert_free_block_count(224, 1);
	assert_free_block_count(1696, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_coalesce, .timeout = TEST_TIMEOUT) {		//coalesce happens with sf_free(x), case 2: prev(allocate), next(free)
	sf_errno = 0;
	/* void *w = */ sf_malloc(8);
	void *x = sf_malloc(200);
	void *y = sf_malloc(300);
	/* void *z = */ sf_malloc(4);

	sf_free(y);
	sf_free(x);

	assert_free_block_count(0, 2);
	assert_free_block_count(544, 1);
	assert_free_block_count(1376, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, freelist, .timeout = TEST_TIMEOUT) {
	void *u = sf_malloc(200);
	/* void *v = */ sf_malloc(300);
	void *w = sf_malloc(200);
	/* void *x = */ sf_malloc(500);
	void *y = sf_malloc(200);
	/* void *z = */ sf_malloc(700);		//this malloc will require us to grow the heap

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_free_block_count(0, 4);
	assert_free_block_count(224, 3);
	assert_free_block_count(1760, 1);
	assert_free_list_size(4, 3);
	assert_free_list_size(7, 1);

	// First block in list should be the most recently freed block.
	int i = 4;
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	cr_assert_eq(bp, (char *)y - sizeof(sf_header),
		     "Wrong first block in free list %d: (found=%p, exp=%p)",
                     i, bp, (char *)y - sizeof(sf_header));
}

Test(sfmm_basecode_suite, realloc_larger_block, .timeout = TEST_TIMEOUT) {
	void *x = sf_malloc(sizeof(int));
	/* void *y = */ sf_malloc(10);
	x = sf_realloc(x, sizeof(int) * 20);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x1f) == 96, "Realloc'ed block size not what was expected!");

	assert_free_block_count(0, 2);
	assert_free_block_count(1824, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_splinter, .timeout = TEST_TIMEOUT) {
	void *x = sf_malloc(sizeof(int) * 20);
	void *y = sf_realloc(x, sizeof(int) * 16);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char*)y - sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x1f) == 96, "Block size not what was expected!");

	// There should be only one free block.
	assert_free_block_count(0, 1);
	assert_free_block_count(1888, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_free_block, .timeout = TEST_TIMEOUT) {
	void *x = sf_malloc(sizeof(double) * 8);
	void *y = sf_realloc(x, sizeof(int));

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char*)y - sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x1f) == 32, "Realloc'ed block size not what was expected!");

	// After realloc'ing x, we can return a block of size 32 to the freelist.
	// This block will be coalesced.
	assert_free_block_count(0, 1);
	assert_free_block_count(1952, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE THESE COMMENTS
//############################################

//Test(sfmm_student_suite, student_test_1, .timeout = TEST_TIMEOUT) {
//}

Test(sfmm_student_suite, student_test_5, .timeout = TEST_TIMEOUT) {			//return NULL since size is 0
sf_errno = 0;
	size_t size = 0;
	int *x = sf_malloc(size);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(x == NULL, "x is not NULL!");
}

Test(sfmm_student_suite, student_test_4, .timeout = TEST_TIMEOUT) {		//case 3 and case 4 coalesce happens 
sf_errno = 0;
	int *x = sf_malloc(300);

	cr_assert_not_null(x, "x is NULL!");

int *y = sf_malloc(200);
cr_assert_not_null(y, "y is NULL!");
int *z = sf_malloc(500);
cr_assert_not_null(z, "z is NULL!");
	sf_free(x);
	sf_free(y);
	sf_free(z);
	assert_free_block_count(0, 1);	
	assert_free_block_count(1984, 1);
	assert_free_list_size(7, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_student_suite, student_test_7, .timeout = TEST_TIMEOUT) {		//case 3 and case 4 coalesce happens 
sf_errno = 0;
	int *x = sf_malloc(300);

	cr_assert_not_null(x, "x is NULL!");

	int *y = sf_malloc(200);
	cr_assert_not_null(y, "y is NULL!");
	int *z = sf_malloc(500);
	cr_assert_not_null(z, "z is NULL!");
	sf_free(x);
	sf_free(y);
	sf_free(z);

	int *a = sf_malloc(150);

	cr_assert_not_null(a, "a is NULL!");

	int *b = sf_malloc(200);
	cr_assert_not_null(b, "b is NULL!");
	int *c = sf_malloc(328);
	cr_assert_not_null(c, "c is NULL!");
	sf_free(a);						//worked when free in order (abc), (cab), (bac), (cba), (acb)
	sf_free(c);
	sf_free(b);

	assert_free_block_count(0, 1);	
	assert_free_block_count(1984, 1);
	assert_free_list_size(7, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_student_suite, student_test_8, .timeout = TEST_TIMEOUT) {		 //malloc(900) will cause extend heap
sf_errno = 0;
	int *x = sf_malloc(300);
	cr_assert_not_null(x, "x is NULL!");

int *y = sf_malloc(200);
cr_assert_not_null(y, "y is NULL!");
int *z = sf_malloc(500);
cr_assert_not_null(z, "z is NULL!");
	int *a = sf_malloc(900);

	cr_assert_not_null(a, "a is NULL!");

	assert_free_block_count(0, 1);	
	assert_free_block_count(2016, 1);
	assert_free_list_size(7, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_student_suite, student_test_9, .timeout = TEST_TIMEOUT) {			//malloc exactly 11 pages
	sf_errno = 0;

	// We want to allocate up to exactly 11 pages.
	void *x = sf_malloc(22448);
	cr_assert_not_null(x, "x is NULL!");
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
	cr_assert(sf_mem_start() + (PAGE_SZ *11) == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_student_suite, student_test_10, .timeout = TEST_TIMEOUT) {			//malloc exactly 11 pages
	sf_errno = 0;

	// We want to allocate up to exactly 11 pages.
	void *r = sf_malloc(22448);
	cr_assert_not_null(r, "r is NULL!");
	int *x = sf_malloc(300);
	cr_assert_not_null(x, "x is NULL!");
	int *y = sf_malloc(200);
	cr_assert_not_null(y, "y is NULL!");
	int *z = sf_malloc(502);
	cr_assert_not_null(z, "z is NULL!");
	sf_free(z);
	sf_free(x);
	sf_free(y);
	int *a = sf_malloc(150);
	cr_assert_not_null(a, "a is NULL!");
	int *b = sf_malloc(200);
	cr_assert_not_null(b, "b is NULL!");
	int *c = sf_malloc(328);
	cr_assert_not_null(c, "c is NULL!");
	sf_free(c);						
	sf_free(b);
	sf_free(a);

	assert_free_block_count(0, 1);	
	assert_free_block_count(2048, 1);
	assert_free_list_size(7, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + (12 * PAGE_SZ) == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_student_suite, student_test_11, .timeout = TEST_TIMEOUT) {			//malloc exactly 11 pages
	sf_errno = 0;

	// We want to allocate up to exactly 11 pages.
	void *r = sf_malloc(22448);
	cr_assert_not_null(r, "r is NULL!");
	int *x = sf_malloc(300);
	cr_assert_not_null(x, "x is NULL!");
	int *y = sf_malloc(200);
	cr_assert_not_null(y, "y is NULL!");
	int *z = sf_malloc(502);
	cr_assert_not_null(z, "z is NULL!");
	int *newZ = sf_realloc(z, 50);
	cr_assert_not_null(newZ, "newZ is NULL!");
	sf_free(z);
	sf_free(x);
	sf_free(y);
	int *a = sf_malloc(150);
	cr_assert_not_null(a, "a is NULL!");
	int *b = sf_malloc(200);
	cr_assert_not_null(b, "b is NULL!");
	int *c = sf_malloc(328);
	cr_assert_not_null(c, "c is NULL!");
	int *newB = sf_realloc(b, 902);
	cr_assert_not_null(newB, "newB is NULL!");
	sf_free(c);						
	sf_free(newB);
	sf_free(a);

	assert_free_block_count(0, 1);	
	assert_free_block_count(2048, 1);
	assert_free_list_size(7, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + (12 * PAGE_SZ) == sf_mem_end(), "Allocated more than necessary!");
}


Test(sfmm_student_suite, student_test_6, .timeout = TEST_TIMEOUT) {		//case 3 coalesce happens will free(y)
sf_errno = 0;
	int *x = sf_malloc(300);

	cr_assert_not_null(x, "x is NULL!");

int *y = sf_malloc(200);
cr_assert_not_null(y, "y is NULL!");
int *z = sf_malloc(500);
cr_assert_not_null(z, "z is NULL!");
	sf_free(x);
	sf_free(y);
	assert_free_block_count(0, 2);	
	assert_free_block_count(896, 1);	//in wild list
	assert_free_block_count(544, 1);	//in free list[6]
	assert_free_list_size(6, 1);
	assert_free_list_size(7, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_student_suite, student_test_1, .timeout = TEST_TIMEOUT) {
sf_errno = 0;
	int *x = sf_malloc(sizeof(double));

	cr_assert_not_null(x, "x is NULL!");

	*x = 8;

	cr_assert(*x == 8, "sf_malloc failed to give proper space for an int!");

	assert_free_block_count(0, 1);
	assert_free_block_count(1952, 1);
	assert_free_list_size(7, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_student_suite, student_test_2, .timeout = TEST_TIMEOUT) {
sf_errno = 0;
	int *x = sf_malloc(sizeof(double));

	cr_assert_not_null(x, "x is NULL!");

	*x = 8;

	cr_assert(*x == 8, "sf_malloc failed to give proper space for an double!");
	int *y = sf_malloc(200);
	cr_assert_not_null(y, "y is NULL!");
	int *z = sf_malloc(300);
	cr_assert_not_null(z, "z is NULL!");

	int *a = sf_malloc(4);
	cr_assert_not_null(a, "a is NULL!");

	assert_free_block_count(0, 1);
	assert_free_block_count(1376, 1);
	assert_free_list_size(7, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_student_suite, student_test_3, .timeout = TEST_TIMEOUT) {
sf_errno = 0;
	int *x = sf_malloc(sizeof(double));

	cr_assert_not_null(x, "x is NULL!");

	*x = 8;

	cr_assert(*x == 8, "sf_malloc failed to give proper space for an double!");

int *y = sf_malloc(200);
cr_assert_not_null(y, "y is NULL!");
int *z = sf_malloc(1);
cr_assert_not_null(z, "z is NULL!");

	assert_free_block_count(0, 1);	
	assert_free_block_count(1696, 1);
	assert_free_list_size(7, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

