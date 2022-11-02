#ifndef HELPERHEADER_H
#define HELPERHEADER_H

#define BLOCK_SZ 32     //minimum size of a block required
#define INIT_PADD_SZ (BLOCK_SZ - sizeof(sf_header))   //32 -8 = 24, The first word is an unused padding word so that the header of each block will start sizeof(sf_header) bytes before an alignment boundary.         

/**
 * @brief As your heap is initially empty, at the time of the first call to sf_malloc
you will need to make one call to sf_mem_grow to obtain a page of memory
within which to set up the prologue and initial epilogue.
 * @return int
 */
extern int first_alloc_init();

/**
 * @brief determine the size of the
block to be allocated by adding the header size and the size of any necessary
padding to reach a size that is a multiple of 32 to maintain proper alignment.
* @return int
 */
extern int determine_malloc_size(size_t size);

/**
 * @brief Search this free list from the beginning until the first sufficiently large
block is found.  If there is no such block, continue with the next larger
size class.
 * 
 * @param size 
 * @return int 
 */
extern int find_free_block(size_t size);

/**
 * @brief Determine which size class to find an empty block
 * 
 * @param size 
 * @return int 
 */
extern int determine_size_class(size_t size);

/**
 * @brief removes a block from a free list
 * @param block_to_remove  the block to be coalesced(i.e. case 2: pp's next block would be the one being removed)
 */
extern void remove_block_from_freelist(sf_block *block_to_remove);

extern void add_block_to_freelist(int is_wild_block, sf_block *block_to_be_added);

extern void coalesce(sf_block *block_to_coalesce);

extern void create_epilogue();

extern int split_block(int is_wild_block, size_t size, int realloc_flag);

extern int extend_heap(size_t size);

/**
 * @brief helper function for sf_free, checks if ptr returned by sf_malloc is invalid or not
 * 
 * @param ptr 
 * @return int -1 if invalid ptr, else 1
 */
extern int check_invalid_pointers(void *ptr);

/**
 * @brief Used for memalign function to check if parameter align is a power of 2
 * @param align 
 * @return int Returns 0 if success, -1 otherwise
 */
extern int power_of_two(int align);

#endif