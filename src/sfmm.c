#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include "helperHeader.h"
#include <errno.h>

sf_block *pPrologue = NULL;    //points to start of prologue block
sf_block *pEpilogue = NULL;     //points to start of epilogue block
sf_block *pCurr_block = NULL;  //points to start of current block
sf_block *pRestore_free_block = NULL;    //used for restoring the free block that was removed that shouldn't be in realloc 
int first_time_alloc = 0;
int realloc_flag_case = 0;

void *sf_malloc(size_t size) {      //size is payload
    if(size <= 0)
        return NULL;
    if(!first_time_alloc){       //first time allocating
        int first_alloc_flag;
        if((first_alloc_flag = first_alloc_init()) < 0){     //no memory is large enough
            sf_errno = ENOMEM;
            return NULL;
        }
        //first time allocating will always use
        //the wilderness block since all the free lists are initially empty
        sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.prev = pCurr_block;
        sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.next = pCurr_block;
        pCurr_block->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS - 1];     
        pCurr_block->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS - 1];
    }

    if(sf_mem_start() == sf_mem_end()){     //allocation not successful after invoking first_alloc
        sf_errno = ENOMEM;
        return NULL;
    }
    int required_size = determine_malloc_size(sizeof(sf_header) + sizeof(sf_footer) + size); 
    int free_list_index_to_use;      //returns which free list we're using to alloc
    if((free_list_index_to_use = find_free_block(required_size)) < 0){        //if find_free_block returns -1, err
        sf_errno = ENOMEM;
        return NULL;
    }
    if((pCurr_block->header & 0xffffffe0) - required_size >= BLOCK_SZ){     //split it since block is larger than required size
        if(free_list_index_to_use < NUM_FREE_LISTS - 1){
            if(split_block(0, required_size, 0) < 0);
            
        }
        else{       //wilderness index
           if(split_block(1, required_size, 0) < 0);
        }
    }
    else{           //no need to split
        remove_block_from_freelist(pCurr_block);             
        pCurr_block->header |= THIS_BLOCK_ALLOCATED;            //set alloc bit to 1
    }
    sf_footer *footer = (void*)((long)pCurr_block + (pCurr_block->header & 0xffffffe0)- 8);       //start of footer is start of pCurr_block + this block's entire size - 8
    *footer = pCurr_block->header;
    return pCurr_block->body.payload;
}

void sf_free(void *pp) {       
    pCurr_block = (sf_block *)(pp - sizeof(sf_header));     //since pp points to start of payload, we need to -8 to make it point to start of the actual block, also, pCurr may still point to some other blocks from previous functions so update it
     int invalid_ptr_check;
    if((invalid_ptr_check = check_invalid_pointers(pp)) < 0)
        abort();     

    sf_footer *prev_block_footer = (void*)((long)pCurr_block - 8);
    sf_block *prev_block = (void*)((long)pCurr_block - (*prev_block_footer & 0xffffffe0));
    sf_block *next_block = (void*)((long)pCurr_block + (pCurr_block->header & 0xffffffe0));
    //case1: prev: allocated, next: allocated
    if(((prev_block->header & THIS_BLOCK_ALLOCATED) && (next_block->header & THIS_BLOCK_ALLOCATED)) || (prev_block == pCurr_block  && (next_block->header & THIS_BLOCK_ALLOCATED))){}   //second condition checks if a block to be freed is the very first block in heap, aka it's prev is the prologue

    //case2: prev: allocated, next: free
    else if(((prev_block->header & THIS_BLOCK_ALLOCATED) && !(next_block->header & THIS_BLOCK_ALLOCATED)) || (realloc_flag_case)){
        sf_block *remove_this_block = next_block;
        remove_block_from_freelist(remove_this_block);
        coalesce(remove_this_block);
    }

    //case3: prev: free, next: allocated        in this case, pCurr block will be the one removed
    else if(!(prev_block->header & THIS_BLOCK_ALLOCATED) && (next_block->header & THIS_BLOCK_ALLOCATED)){
        sf_block *remove_this_block = pCurr_block;
        pCurr_block = prev_block;
        remove_this_block->header &= ~(1UL << 4);       //this block isn't in a free list, so just change the alloc bit
        coalesce(remove_this_block);
        remove_block_from_freelist(pCurr_block);        //remove from old free list
        
    }

    //case4: prev: free, next: free
    else if(!(prev_block->header & THIS_BLOCK_ALLOCATED) && !(next_block->header & THIS_BLOCK_ALLOCATED)) {
        sf_block *remove_this_block = pCurr_block;
        sf_block *remove_next_block = next_block;   
        pCurr_block = prev_block;
        remove_this_block->header &= ~(1UL << 4);    //this block isn't in a free list, so just change the alloc bit
        coalesce(remove_this_block);
        coalesce(remove_next_block);
        remove_block_from_freelist(pCurr_block);        //remove old one from free list
        remove_block_from_freelist(next_block);        //remove 2nd old one from free list
    }

    //Note that blocks in a free list must not be marked as allocated, so change the alloc bit to free, but after checking invalidptr
    pCurr_block->header &= ~(1UL << 4);   

    //set up new footer for the new pCurr block(AKA block to be freed)
    sf_footer *footer = (void*)((long)pCurr_block + (pCurr_block->header & 0xffffffe0)- 8);       //start of footer is start of pCurr_block + this block's entire size - 8
    *footer = pCurr_block->header;
    
    //add this new coalesced free block into an appropriate free list, OR add to wild list 
    sf_block *pTemp = (void*)((long)pCurr_block + (pCurr_block->header & 0xffffffe0));
    if(pTemp != pEpilogue){
        int i = determine_size_class(pCurr_block->header & 0xffffffe0);
        if(i < NUM_FREE_LISTS-1)            //not wild free list
            add_block_to_freelist(0, pCurr_block);
        else
            add_block_to_freelist(1, pCurr_block);
    }
    else{           //need to transfer this free block to wild list
        add_block_to_freelist(1, pCurr_block);
    }
    realloc_flag_case = 0;  //reset it to avoid side effects
}

void *sf_realloc(void *pp, size_t rsize) {
    pCurr_block = (sf_block *)(pp - sizeof(sf_header));     //since pp points to start of payload, we need to -8 to make it point to start of the actual block, also, pCurr may still point to some other blocks from previous functions so update it
    int invalid_ptr_check;
    if((invalid_ptr_check = check_invalid_pointers(pp)) < 0){
        sf_errno = EINVAL;
        return NULL;
    }
    if(rsize == 0){
        sf_free(pp);
        return NULL;
    }
    int required_size = determine_malloc_size(sizeof(sf_header) + sizeof(sf_footer) + rsize); 

    //if reallocating to a larger size
    if((pCurr_block->header & 0xffffffe0) < required_size){
        sf_block *pTemp = sf_malloc(rsize);
        if(pTemp == NULL)
            return NULL;
        memcpy(pTemp, pp, rsize);       //returns a pointer to dest
        sf_free(pp);
        return pTemp;
    }
    //re-allocating to a smaller size
    else{
        int free_list_index_to_use;      //returns which free list we're using
        if((free_list_index_to_use = find_free_block(required_size)) < 0){}       //if find_free_block returns -1, err
        int is_wild_block = free_list_index_to_use < NUM_FREE_LISTS -1? 0 : 1;
        sf_block *pTemp = (sf_block *)(pp - sizeof(sf_header));
        pCurr_block = (sf_block *)(pp - sizeof(sf_header));     //again set pCurr_block to start of parameter pointer *pp
        //two cases of splitting
        if(((pCurr_block->header & 0xffffffe0) - (rsize + sizeof(sf_header) + sizeof(sf_footer))) < BLOCK_SZ){ //unable to split due to splinters
            pTemp->header -= pTemp->header;
            pTemp->header += required_size;      //set the header to the new smaller size that realloc wants
            pTemp->header |= THIS_BLOCK_ALLOCATED;
            add_block_to_freelist(is_wild_block, pRestore_free_block);      //put the free block back to where it was
        }
        else{       //able to split
            int new_malloc_size = determine_malloc_size(required_size); 
            if(split_block(is_wild_block, new_malloc_size, 1) < 0){}
            pCurr_block = (sf_block *)(pp - sizeof(sf_header));
            pCurr_block->header = new_malloc_size;          //update new block size 
            pCurr_block->header |= THIS_BLOCK_ALLOCATED;
            sf_footer *footer = (void*)((long)pTemp + (pTemp->header & 0xffffffe0)- 8);       
            *footer = pCurr_block->header;
        }
    }
    return pp;
}

void *sf_memalign(size_t size, size_t align) {
    if(align < BLOCK_SZ || power_of_two(align)){
        sf_errno = EINVAL;
        return NULL;
    }
    int required_size = size + BLOCK_SZ + align;        //size of header and footer wll be added when invoking malloc()
    pCurr_block = sf_malloc(required_size);
    //two cases
    return pCurr_block->body.payload;
}

int first_alloc_init(){
    if (sf_mem_grow() == NULL)
        return -1;
    first_time_alloc = 1;
    //sf_free_list_heads is empty initially,  head of a freelist must be initialized before the list
    //can be used.
    for(int i = 0; i < NUM_FREE_LISTS; i++){
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
    }

    pPrologue = (sf_block*)(sf_mem_start() + INIT_PADD_SZ);          //create prologue during this initializtion
    pPrologue->header = BLOCK_SZ;       //32(min. block size)
    pPrologue->header |= THIS_BLOCK_ALLOCATED;

    pCurr_block = (sf_block*)(BLOCK_SZ + INIT_PADD_SZ + sf_mem_start());    //points to the very first block after padding in heap
    pCurr_block->header = PAGE_SZ - BLOCK_SZ - INIT_PADD_SZ; //page_sz - init_padd(24) - prologue(32)
    
    create_epilogue();
    return 0;
}

int determine_malloc_size(size_t size){
    if(size % BLOCK_SZ == 0)        //already 32-aligned
        return size;
    else
        return BLOCK_SZ + size - (size % BLOCK_SZ);     //32 + (parameter size) - remainder
}

int determine_size_class(size_t size){
    int fibSizeClass[8] = {1, 2, 3, 5, 8, 13};
    int i = 0;
    while(i < 6 && ((BLOCK_SZ * fibSizeClass[i]) < size)){      //continues up to NUM_FREE_LIST_2(i.e. 6)
        i++;
    }
    return (i == 6)? NUM_FREE_LISTS-2: i;      //return second to last block if no size class previously is large enough, else return i
}

int find_free_block(size_t size){
    int i = determine_size_class(size);   //returns the appropriate size class, i would be a number in the range[0,6]
    sf_block* pTemp;
    pCurr_block = NULL;     //reset it
    int foundBlock = 0;
    while(i < NUM_FREE_LISTS){
        if(&sf_free_list_heads[i] != sf_free_list_heads[i].body.links.next){    //this free list contains 1 or more nodes, search it
            pTemp = sf_free_list_heads[i].body.links.next;    //pTemp now points to first node in this free list
            while(pTemp != &sf_free_list_heads[i]){             //while not pointing to sentinel node
                if((pTemp->header & 0xffffffe0) < size)
                    pTemp = pTemp->body.links.next;     //traverse to next block if needed   
                else{   
                    pCurr_block = pTemp;
                    break;
                }
                
            }
            if(pCurr_block == pTemp){    //point to same block address
                foundBlock = 1;
                break;
            }
        }
        i++;
    }

    if(foundBlock){     //a block was found 
        pRestore_free_block = pCurr_block;
        remove_block_from_freelist(pCurr_block); //remove from free list because free list can only contains free blocks
        return i;
    }

    // if wildnerness block is nonempty, set pCurr to it, else pCurr is set to Pepilogue
    if(&sf_free_list_heads[NUM_FREE_LISTS-1] == sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next)
        pCurr_block = pEpilogue; 
    else
        pCurr_block = sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next;

    int expand_heap_flag = extend_heap(size);
    return (expand_heap_flag? -1: NUM_FREE_LISTS-1);  //if request_more_mem returned err, return -1, else, return the free list index to use
}

int extend_heap(size_t size){
    int current_space = pCurr_block->header & 0xffffffe0;     //will be contained in pCurr's header
    while(current_space < size){         //need to request more memory with mem_grow if even wild block isn't big enough
        void* pageGrow =  sf_mem_grow();
        if(pageGrow ==NULL){
            return -1;
        }
        pCurr_block->header = pCurr_block->header + PAGE_SZ;
        current_space = current_space + PAGE_SZ;  
        add_block_to_freelist(1, pCurr_block);          //we're adding to wild block
        create_epilogue();  //a new epilogue is created at the end of the newly added region
   
    }
    return 0;
}

void add_block_to_freelist(int is_wild_block, sf_block *block_to_be_added){
    if(!is_wild_block){         //not NUM_FREE_LIST[7]
        int index = determine_size_class((block_to_be_added->header & 0xffffffe0));
        if(&sf_free_list_heads[index] == sf_free_list_heads[index].body.links.next) {    //this free list is empty
            sf_free_list_heads[index].body.links.prev = block_to_be_added;
            sf_free_list_heads[index].body.links.next = block_to_be_added;
            block_to_be_added->body.links.prev = &sf_free_list_heads[index];
            block_to_be_added->body.links.next = &sf_free_list_heads[index];

        }
        else{       //this free list is nonempty
            block_to_be_added->body.links.prev = &sf_free_list_heads[index];     //add at front of list
            block_to_be_added->body.links.next = sf_free_list_heads[index].body.links.next;
            sf_free_list_heads[index].body.links.next = block_to_be_added;
        }
    }

    else{           //is wilderness block
        sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = block_to_be_added;
        sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = block_to_be_added;
        block_to_be_added->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];     //add at front of list
        block_to_be_added->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];

        //since heap is extended,  old epilogue becomes the header of the new block
    }
}

int check_invalid_pointers(void *ptr){
    if(ptr == NULL)
        return -1;
    if (((((void *)&pEpilogue->header) - ((void *)&pCurr_block->header)) % 32) != 0) // The pointer is not 32-byte aligned 
        return -1;
    if(pCurr_block->header != (pCurr_block->header | THIS_BLOCK_ALLOCATED))        //alloc bit is 0
        return -1;
    if((pCurr_block->header & 0xffffffe0) < BLOCK_SZ)
        return -1;
    if((pCurr_block->header & 0xffffffe0) % BLOCK_SZ != 0)
        return -1;
    if(pCurr_block <= pPrologue)
        return -1;
    if(pCurr_block >= pEpilogue)
        return -1;
    return 1;
    
}

void remove_block_from_freelist(sf_block *block_to_remove){
    sf_block *prev = block_to_remove->body.links.prev;
    sf_block *next = block_to_remove->body.links.next;
    next->body.links.prev = prev;
    prev->body.links.next = next;
}

void coalesce(sf_block *block_to_coalesce){
    pCurr_block->header += (block_to_coalesce->header & 0xffffffe0);
}

void create_epilogue(){
    //create a new epilogue block after growing the heap and make pEpilogue point to it
    pEpilogue = (sf_block*)(sf_mem_end() - sizeof(sf_header));      
    pEpilogue->header = 0;      //epilogue is a 0-sized allocated(1) block
    pEpilogue->header |= THIS_BLOCK_ALLOCATED;
}

int split_block(int is_wild_block, size_t size, int realloc_flag){
    sf_block *pRemainder_block = (sf_block *)(((void *)pCurr_block) + size); // the "lower part" (i.e. locations with lower-numbered addresses)should be used to satisfy the allocation request
       
    pRemainder_block->header = (pCurr_block->header & 0xffffffe0) - size;
    if(pRemainder_block->header < BLOCK_SZ)
        return -1;
    pCurr_block->header = size;         //new splitted smaller size 
    pCurr_block->header |= THIS_BLOCK_ALLOCATED;
    sf_footer *remainderfooter = (void*)((long)pRemainder_block + (pRemainder_block->header & 0xffffffe0)- 8);       //start of footer is start of pCurr_block + this block's entire size - 8
    *remainderfooter = pRemainder_block->header;
    if(realloc_flag){
        pRemainder_block->header |= THIS_BLOCK_ALLOCATED;
        sf_block *pRemainder_block_payload = (sf_block *)(((void *)pCurr_block) + size + 8);
        realloc_flag_case = 1;
        sf_free(pRemainder_block_payload);
        return 0;
    }

    add_block_to_freelist(is_wild_block, pRemainder_block);    
    return 0;
}

int power_of_two(int align){
    if(align <= 0)
        return -1;
    while(align % 2 == 0){
        align /= 2;
    }
    if(align == 1)
        return 0;
    else
        return -1;
    
}