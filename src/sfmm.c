#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include <errno.h>
int malloc_initialize();
int search_free_list(size_t free_block_size);
int remove_from_free_list(void *pp);

int sf_malloc_init = 0;

int malloc_initialize()
{
    void *sf_mem_grow_ptr = sf_mem_grow(); 
    if (sf_mem_grow_ptr == NULL)
    {
        return 1;
    }
    sf_malloc_init++;

    //Initializes free list nodes, points dumby node to itself
    for (int i = 0; i < NUM_FREE_LISTS; i++)
    {
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }

    //Initializes prologue block
    sf_block prologue;
    sf_block *prologue_ptr = sf_mem_start();
    prologue.header = (sf_header)0; //initializes all bits to 0, including first 32 unused bits
    prologue.header |= ((sf_header)sizeof(prologue)); //sets size of prologue in header 
    prologue.header = prologue.header >> 4 << 4;
    prologue.header |= (sf_header)0x8; //sets al to 1 and other unused bits to 0
    *prologue_ptr = prologue;
    
    //Initializes epilogue 
    sf_block epilogue; 
    sf_block *epilogue_ptr = sf_mem_end() - (char)(sizeof(sf_header) * 2);
    epilogue.header = (sf_header)0;
    epilogue.header = epilogue.header >> 4 << 4;
    epilogue.header |= (sf_header)0x8; //sets alloc bit to 1, other bits to 0, including prv_alloc which initially is 0
    *epilogue_ptr = epilogue;

    //Initializes wilderness block
    sf_block wilderness_block;
    size_t page_size = (size_t)sf_mem_end() - (size_t)sf_mem_start();
    sf_block *wilderness_block_ptr = sf_mem_start() + (char)sizeof(prologue);
    //Sets prologues footer
    wilderness_block.prev_footer = (sf_header)0; 
    wilderness_block.prev_footer |= ((sf_header)sizeof(prologue)); 
    wilderness_block.prev_footer = wilderness_block.prev_footer >> 4 << 4;
    wilderness_block.prev_footer |= (sf_header)0x8; 
    //Sets wilderness blocks header
    wilderness_block.header = (sf_header)0x0;
    wilderness_block.header |=  (sf_header)(page_size - (sizeof(prologue) + sizeof(epilogue.header))); //sets size of wilderness block
    wilderness_block.header = wilderness_block.header >> 4 << 4;
    wilderness_block.header |= (sf_header)0x4; //sets prev_alloc to 1, others to 0

    //Sets wilderness blocks footer
    epilogue.prev_footer = (sf_header)0;
    epilogue.prev_footer |= (sf_header)(page_size - (sizeof(prologue) + sizeof(epilogue.header))); //sets size of wilderness block
    epilogue.prev_footer = epilogue.prev_footer >> 4 << 4;
    epilogue.prev_footer |= (sf_header)0x4; //sets prev_alloc to 1, others to 0

    *wilderness_block_ptr = wilderness_block;
    //Puts wilderness block in free list
    sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.next = wilderness_block_ptr;
    sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.prev = wilderness_block_ptr;
    wilderness_block.body.links.next = &sf_free_list_heads[NUM_FREE_LISTS - 1];
    wilderness_block.body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS - 1];

    *prologue_ptr = prologue;
    *epilogue_ptr = epilogue;
    *wilderness_block_ptr = wilderness_block;
    return 0;
}

int search_free_list(size_t free_block_size)
{
    int fib_prev = 1, fib_current = 1, fib_next;
    int free_list_found = -1;
    for (int i = 0; i < (NUM_FREE_LISTS - 2) && (free_list_found != 0); i++)
    {
        if (free_block_size <= (size_t)(fib_current * sizeof(sf_block)))
        {
            free_list_found = 0;
            return i;
        }
        fib_next = fib_prev + fib_current;
        fib_prev = fib_current;
        fib_current = fib_next;
    }
    return NUM_FREE_LISTS - 2;
}

int remove_from_free_list(void *pp)
{
    sf_block *pp_block = pp;
    int exit = -1;
    for (int i = 0; i < NUM_FREE_LISTS; i++)
    {
        if (sf_free_list_heads[i].body.links.next != &sf_free_list_heads[i])
        {
            sf_block *current_block = sf_free_list_heads[i].body.links.next;
            int counter = 0;
            while (current_block != &sf_free_list_heads[i] && exit == -1)
            {
                if ((uintptr_t)current_block == (uintptr_t)pp_block)
                {
                    pp_block->body.links.prev->body.links.next = pp_block->body.links.next;
                    pp_block->body.links.next->body.links.prev = pp_block->body.links.prev;
                    exit = 0;
                }
                current_block = current_block->body.links.next;
                counter++;
            }
        }
    }
    return 0;
}

void *sf_malloc(size_t size) {
    // To be implemented.
    int free_block_found = -1;
    //Initializes malloc if needed
    if (sf_malloc_init == 0)
    {
        int malloc_init_return = malloc_initialize();
        if (malloc_init_return == 1)
        {
            sf_errno = ENOMEM;
            return NULL;
        }
    }
    //Check if requested size is zero, if so, returns NULL and doesn't set sf_errno
    if (size == (size_t)0)
    {
        return NULL;
    }

    size_t size_of_block_alloc = 16;
    //Determine size of block to be allocated 
    //(by adding the header size and the size of any necessary padding to reach a size that is a multiple of 16 to maintain proper alignment)
    if (size < (size_t)16)
    {
        size_of_block_alloc += size;
        size_t min_block_size = 32;
        size_of_block_alloc += (min_block_size - size_of_block_alloc);
    }
    else
    {
        size_of_block_alloc += size;
        if (size_of_block_alloc % 16 != 0)
        {
            size_of_block_alloc += 16 - (size_of_block_alloc % 16);
        }
    }
    void *malloc_return;
    //Find free block of proper size to allocate to in free list 
    int fib_prev = 1, fib_current = 1, fib_next;
    for (int i = 0; i < NUM_FREE_LISTS && (free_block_found != 0); i++)
    {
        //M, (M, 2M], (2M, 3M], (3M, 5M], (5M, 8M], (8M, 13M], (13M, 21M], (21M, 34M], (34M, inf / 55M], ... , (Wilderness Block)
        if ((size_of_block_alloc <= (size_t)(fib_current * sizeof(sf_block))) || (i = NUM_FREE_LISTS - 2))
        {
            //Searching through regular free lists, checking if non-empty
            sf_block *current_block = &sf_free_list_heads[i];
            if (current_block->body.links.next != &sf_free_list_heads[i])
            {
                current_block = sf_free_list_heads[i].body.links.next;
                while(current_block != &sf_free_list_heads[i] && (free_block_found != 0))
                {
                    sf_header current_block_size = current_block->header << 32 >> 36 << 4;
                    if (size_of_block_alloc <= current_block_size)
                    {
                        //Removes free block from free list
                        current_block->body.links.prev->body.links.next = current_block->body.links.next;
                        current_block->body.links.next->body.links.prev = current_block->body.links.prev;

                        //Check for splintering as to pad accordingly
                        size_t splinter_size = current_block_size - size_of_block_alloc;
                        size_t padding = 0;
                        if (splinter_size > 0 && splinter_size < 32)
                            padding = splinter_size;
                        size_of_block_alloc += padding;
                        //Sets header of allocated block
                        sf_block *malloc_block = current_block;
                        malloc_block->prev_footer = current_block->prev_footer;
                        malloc_block->header = (sf_header)0;
                        malloc_block->header |= (sf_header)size << 32; //Sets payload size for malloc block
                        malloc_block->header |= (sf_header)(size_of_block_alloc); //Sets block size for malloc block
                        malloc_block->header = malloc_block->header >> 4 << 4; //Clears last 4 bits of malloc block
                        if (malloc_block->prev_footer >= 0x8)
                            malloc_block->header |= (sf_header)0xC; //Sets pal = 1 and al = 1
                        else
                            malloc_block->header |= (sf_header)0x8; //Sets al = 1   
                        //Inputs spliced free block back into free list
                        if ((current_block_size - size_of_block_alloc) != 0)
                        {
                            size_t free_block_size = (current_block_size) - (size_of_block_alloc + padding);
                            //Initializes free block
                            sf_block free_block;
                            sf_block *free_block_ptr = (void*)current_block + (malloc_block->header << 32 >> 36 << 4);
                            free_block.prev_footer = (sf_header)0;
                            free_block.prev_footer |= malloc_block->header; //Set footer of allocated block

                            //Sets header of free block
                            free_block.header = (sf_header)0x0;
                            free_block.header |=  (sf_header)(free_block_size); //sets size of free block
                            free_block.header = free_block.header >> 4 << 4;
                            free_block.header |= (sf_header)0x4; //sets prev_alloc to 1, others to 0
                            //Sets footer of free block
                            sf_header *free_block_footer = (void*)free_block_ptr + (free_block.header << 32 >> 36 << 4);
                            *free_block_footer = (sf_header)0;
                            *free_block_footer |= free_block.header;

                            *free_block_ptr = free_block;
                            //Adds free block to free list
                            int free_list_index;
                            if (i == (NUM_FREE_LISTS - 1))
                                free_list_index = i;
                            else
                            {
                                remove_from_free_list(current_block);
                                free_list_index = search_free_list(free_block_size);
                            }
                            free_block.body.links.next = sf_free_list_heads[free_list_index].body.links.next;
                            free_block.body.links.prev = &sf_free_list_heads[free_list_index];
                            free_block.body.links.prev->body.links.next = free_block_ptr;
                            free_block.body.links.next->body.links.prev = free_block_ptr;
                            *free_block_ptr = free_block;
                        }
                        else
                        {
                            remove_from_free_list(current_block);
                            //Sets malloc block footer
                            sf_footer *current_block_footer = (void*)current_block + (malloc_block->header << 32 >> 36 << 4);
                            *current_block_footer = (sf_header)0;
                            *current_block_footer |= malloc_block->header;
                            //Sets next blocks header/footer pal to 1
                            sf_header *next_block_header = (void*)current_block + (malloc_block->header << 32 >> 36 << 4) + (char)8;
                            *next_block_header |= 0x4; //Sets pal to 1 in header
                            sf_footer *next_block_footer = (void*)next_block_header + (*next_block_header << 32 >> 36 <<4) - (char)8;
                            *next_block_footer = *next_block_header; //Sets footer to be equal to header in next block
                        }
                        malloc_return = current_block->body.payload;
                        free_block_found = 0;
                    }
                    current_block = current_block->body.links.next;
                }
            }
        }
        if (i >= (NUM_FREE_LISTS - 1) && free_block_found != 0)
        {
            sf_block *epilogue_ptr = sf_mem_end() - (char)(sizeof(sf_header) * 2);

            size_t total_space_available = 0;
            size_t prev_page_end = (size_t)sf_mem_end();
            void *sf_mem_grow_ptr = sf_mem_grow(); 
            if (sf_mem_grow_ptr == NULL)
            {
                sf_errno = ENOMEM;
                return NULL;
            }
            size_t added_space = (size_t)sf_mem_end() - prev_page_end;
            total_space_available += added_space;
            if ((epilogue_ptr->header << 61 >> 63) == 0x0)
            {
                //Coalesce added page with preceding free block
                size_t size_preceding_free_block = epilogue_ptr->prev_footer << 32 >> 36 << 4;
                total_space_available += size_preceding_free_block;
                sf_header *preceding_block_header = (void*)epilogue_ptr - size_preceding_free_block + (char)8;
                *preceding_block_header = (sf_header)(*preceding_block_header << 60 >> 60);
                *preceding_block_header |= (sf_header)total_space_available;
                sf_block *new_epilogue_ptr = sf_mem_end() - (char)(sizeof(sf_header) * 2);
                sf_block new_epilogue;
                new_epilogue.prev_footer = (sf_header)0;
                new_epilogue.prev_footer |= (sf_header)*preceding_block_header;
                new_epilogue.header = (sf_header)0;
                new_epilogue.header = (sf_header)epilogue_ptr->header;
                *new_epilogue_ptr = new_epilogue;
                epilogue_ptr->header = (sf_header)0;
                epilogue_ptr->prev_footer = (sf_header)0;
            }
            else
            {
               sf_block *new_epilogue_ptr = sf_mem_end() - (char)(sizeof(sf_header) * 2);
               sf_block new_epilogue;
               new_epilogue.prev_footer = (sf_header)0;
               new_epilogue.prev_footer = (sf_header)epilogue_ptr->prev_footer;
               new_epilogue.header = (sf_header)0;
               new_epilogue.header = (sf_header)epilogue_ptr->header;
               epilogue_ptr->header = (sf_header)0;
               epilogue_ptr->prev_footer = (sf_header)0;
               *new_epilogue_ptr = new_epilogue;
            }
            i--;
        }
        fib_next = fib_prev + fib_current;
        fib_prev = fib_current;
        fib_current = fib_next;
    }
    free_block_found = -1;
    return malloc_return;
}

void sf_free(void *pp) {
    // To be implemented.
    //Heap / malloc hasn't been initialized e.g malloc has not been called yet
    if (sf_malloc_init == 0)
        abort();
    //The pointer is NULL.
    if (pp == NULL)
        abort();
    //The pointer is not 16-byte aligned.
    if ((uintptr_t)(sf_block*)pp % 16 != 0)
        abort();
    //The block size is less than the minimum block size of 32.
    sf_block *pp_block_ptr = pp - (char)16;
    if ((sf_header)(pp_block_ptr->header << 32 >> 36 << 4) < (sf_header)32)
        abort();
    //The block size is not a multiple of 16
    if ((sf_header)(pp_block_ptr->header << 32 >> 36 << 4) % (sf_header)16 != (sf_header)0)
        abort();
    //The header of the block is before the start of the first block of the heap, or the footer of the block is after the end of the last block in the heap.
    if ((void*)&pp_block_ptr->header < (void*)(sf_mem_start() + sizeof(sf_block)))
        abort();
    sf_header *pp_block_footer = (void*)pp_block_ptr + (char)(pp_block_ptr->header << 32 >> 36 << 4);
    if ((void*)pp_block_footer > (void*)(sf_mem_end() - sizeof(sf_header)))
        abort();
    //The allocated bit in the header is 0.
    if ((pp_block_ptr->header << 60 >> 63) == 0x0)
        abort();
    //The prev_alloc field in the header is 0, indicating that the previous block is free, but the alloc field of the previous block header is not 0.
    if ((pp_block_ptr->header << 61 >> 63) == 0x0)
    {
        if ((pp_block_ptr->prev_footer << 60 >> 63) != 0x0)
            abort();
    }
    //Coalesce with preceding and following free blocks if present
    sf_header current_block_size = pp_block_ptr->header << 32 >> 36 << 4;
    pp_block_ptr->header &= ~0x8; //Sets block to free
    pp_block_ptr->header = pp_block_ptr->header << 32 >> 32; // Clears payload
    *pp_block_footer = pp_block_ptr->header;
    //Coalesce with next block
    int noCoalesce = 0;
    int doubleCoalesce = -1;
    sf_header *next_block_header = (void*)pp_block_ptr + current_block_size + (char)8;
    if ((sf_header)(*next_block_header << 60 >> 63) == 0x0)
    {
        noCoalesce = -1;
        doubleCoalesce = 0;
        //Removes block to be coalesced from the free list
        sf_block *next_block =  (sf_block*)((void*)pp_block_ptr + current_block_size);
        remove_from_free_list(next_block);
        sf_header *next_block_header = (sf_header*)((void*)pp_block_ptr + current_block_size + (char)8);
        sf_header size_next_free_block = *next_block_header << 32 >> 36 << 4;
        sf_header total_size = size_next_free_block + current_block_size;
        pp_block_ptr->header = pp_block_ptr->header << 60 >> 60;
        pp_block_ptr->header |= (sf_header)total_size;
        pp_block_ptr->header &= ~0x8;

        sf_header *next_block_footer = ((void*)pp_block_ptr + current_block_size + (*next_block_header << 32 >> 36 << 4));
        int isWilderness = -1;
        if (next_block_footer == (sf_mem_end() - (char)16))
            isWilderness = 0;
        *next_block_footer = (sf_header)0x0;
        *next_block_footer = pp_block_ptr->header;
        *next_block_header = 0x0;
        sf_header *next_block_prev_footer = (sf_header*)((void*)pp_block_ptr + current_block_size);
        *next_block_prev_footer = 0x0;
        
        //Adds free block to free list
        sf_block pp_block;
        pp_block.header = pp_block_ptr->header;
        pp_block.prev_footer = pp_block_ptr->prev_footer;
        int free_list_index;

        if (isWilderness == 0)
            free_list_index = (NUM_FREE_LISTS - 1);
        else
            free_list_index = search_free_list(total_size);

        pp_block.body.links.next = sf_free_list_heads[free_list_index].body.links.next;
        pp_block.body.links.prev = &sf_free_list_heads[free_list_index];
        pp_block.body.links.prev->body.links.next = pp_block_ptr;
        pp_block.body.links.next->body.links.prev = pp_block_ptr;
        *pp_block_ptr = pp_block;
    }
    //Coalesce with preceding block
    if ((pp_block_ptr->prev_footer << 60 >> 63) == 0x0)
    {
        noCoalesce = -1;
        if (doubleCoalesce == 0)
            remove_from_free_list(pp_block_ptr);
        sf_header size_preceding_free_block = pp_block_ptr->prev_footer << 32 >> 36 << 4;
        sf_header total_size = size_preceding_free_block + (pp_block_ptr->header << 32 >> 36 << 4);
        sf_header *preceding_block_header = (void*)pp_block_ptr - size_preceding_free_block + (char)8;
        *preceding_block_header = (sf_header)(*preceding_block_header << 60 >> 60);
        *preceding_block_header |= (sf_header)total_size;
        *preceding_block_header &= ~0x8;
        pp_block_ptr->header = (sf_header)0;
        pp_block_ptr->prev_footer = (sf_header)0;
        sf_block *preceding_block = (void*)pp_block_ptr - size_preceding_free_block;
        remove_from_free_list(preceding_block);
        pp_block_ptr = (void*)preceding_block_header - (char)8;

        sf_header *next_block_header = (sf_header*)((void*)pp_block_ptr + total_size + (char)8);
        sf_header *next_block_footer = ((void*)pp_block_ptr + total_size + (*next_block_header << 32 >> 36 << 4));
        int isWilderness = -1;
        if (next_block_footer == (sf_mem_end() - (char)16))
            isWilderness = 0;
        //Adds free block to free list
        sf_block pp_block;
        pp_block.header = pp_block_ptr->header;
        pp_block.prev_footer = pp_block_ptr->prev_footer;
        int free_list_index;

        if (isWilderness == 0)
            free_list_index = (NUM_FREE_LISTS - 1);
        else
            free_list_index = search_free_list(total_size);

        pp_block.body.links.next = sf_free_list_heads[free_list_index].body.links.next;
        pp_block.body.links.prev = &sf_free_list_heads[free_list_index];
        pp_block.body.links.prev->body.links.next = pp_block_ptr;
        pp_block.body.links.next->body.links.prev = pp_block_ptr;
        *pp_block_ptr = pp_block;
    }
    if (noCoalesce == 0)
    {
        sf_header *next_block_header = (sf_header*)((void*)pp_block_ptr + current_block_size + (char)8);
        sf_header *next_block_footer = ((void*)pp_block_ptr + current_block_size + (*next_block_header << 32 >> 36 << 4));
        int isWilderness = -1;
        if (next_block_footer == (sf_mem_end() - (char)16))
            isWilderness = 0;
        //Adds free block to free list
        sf_block pp_block;
        pp_block.header = pp_block_ptr->header;
        pp_block.prev_footer = pp_block_ptr->prev_footer;
        int free_list_index;

        if (isWilderness == 0)
            free_list_index = (NUM_FREE_LISTS - 1);
        else
            free_list_index = search_free_list(current_block_size);

        pp_block.body.links.next = sf_free_list_heads[free_list_index].body.links.next;
        pp_block.body.links.prev = &sf_free_list_heads[free_list_index];
        pp_block.body.links.prev->body.links.next = pp_block_ptr;
        pp_block.body.links.next->body.links.prev = pp_block_ptr;
        *pp_block_ptr = pp_block;
    }
    //Change next blocks header pal and sets footer
    sf_header block_size = (pp_block_ptr->header << 32 >> 36 << 4);
    sf_footer *free_block_footer = (void*)pp_block_ptr + block_size;
    *free_block_footer = pp_block_ptr->header;

    next_block_header = (void*)free_block_footer + (char)8;
    *next_block_header &= ~0x4;
}

void *sf_realloc(void *pp, size_t rsize) {
    // To be implemented.
    //Heap / malloc hasn't been initialized e.g malloc has not been called yet
    if (sf_malloc_init == 0)
        abort();
    //The pointer is NULL.
    if (pp == NULL)
        abort();
    //The pointer is not 16-byte aligned.
    if ((uintptr_t)(sf_block*)pp % 16 != 0)
        abort();
    //The block size is less than the minimum block size of 32.
    sf_block *pp_block_ptr = pp - (char)16;
    if ((sf_header)(pp_block_ptr->header << 32 >> 36 << 4) < (sf_header)32)
        abort();
    //The block size is not a multiple of 16
    if ((sf_header)(pp_block_ptr->header << 32 >> 36 << 4) % (sf_header)16 != (sf_header)0)
        abort();
    //The header of the block is before the start of the first block of the heap, or the footer of the block is after the end of the last block in the heap.
    if ((void*)&pp_block_ptr->header < (void*)(sf_mem_start() + sizeof(sf_block)))
        abort();
    sf_header *pp_block_footer = (void*)pp_block_ptr + (char)(pp_block_ptr->header << 32 >> 36 << 4);
    if ((void*)pp_block_footer > (void*)(sf_mem_end() - sizeof(sf_header)))
        abort();
    //The allocated bit in the header is 0.
    if ((pp_block_ptr->header << 60 >> 63) == 0x0)
        abort();
    //The prev_alloc field in the header is 0, indicating that the previous block is free, but the alloc field of the previous block header is not 0.
    if ((pp_block_ptr->header << 61 >> 63) == 0x0)
    {
        if ((pp_block_ptr->prev_footer << 60 >> 63) != 0x0)
            abort();
    }
    //If the pointer is valid but the size parameter is 0, frees the block and returns NULL
    
    if (rsize == (size_t)0)
    {
        sf_free(pp);
        return NULL;
    }
    //Determine size of block to be allocated 
    size_t size_of_block_realloc = 16;
    if (rsize < (size_t)16)
    {
        size_of_block_realloc += rsize;
        size_t min_block_size = 32;
        size_of_block_realloc += (min_block_size - size_of_block_realloc);
    }
    else
    {
        size_of_block_realloc += rsize;
        if (size_of_block_realloc % 16 != 0)
        {
            size_of_block_realloc += 16 - (size_of_block_realloc % 16);
        }
    }
    sf_block *r_block_ptr = pp - (char)16;
    if ((r_block_ptr->header << 32 >> 36 << 4) < size_of_block_realloc)
    {
        //Reallocating to a Larger Size
        //Call sf_malloc to obtain a larger block.
        sf_block *larger_block = sf_malloc(rsize);
        if (larger_block == NULL)
            return NULL;
        larger_block = (void*)larger_block - (char)16;

        //Call memcpy to copy the data in the block given by the client to the block returned by sf_malloc.  Be sure to copy the entire payload area, but no more.
        size_t r_block_payload = r_block_ptr->header >> 32;
        memcpy(larger_block->body.payload, r_block_ptr->body.payload, r_block_payload);

        //Call sf_free on the block given by the client (inserting into a freelist and coalescing if required).
        sf_free(pp);

        //Return the block given to you by sf_malloc to the client.
        return larger_block->body.payload;
    }
    else if ((r_block_ptr->header << 32 >> 36 << 4) > size_of_block_realloc)
    {
        //!Reallocating to a Smaller Size
        size_t orig_size = r_block_ptr->header << 32 >> 36 << 4;
        void *payload = pp;
        //Check for splintering as to pad accordingly
        size_t splinter_size = (r_block_ptr->header << 32 >> 36 << 4) - size_of_block_realloc;
        size_t padding = 0;
        if (splinter_size > 0 && splinter_size < 32)
            padding = splinter_size;
        size_of_block_realloc += padding;

        r_block_ptr->header |= 0x8;
        r_block_ptr->header = r_block_ptr->header << 60 >> 60;
        r_block_ptr->header |= (sf_header)size_of_block_realloc;
        r_block_ptr->header |= (sf_header)(rsize << 32);
        sf_block *free_block = (void*)r_block_ptr + (r_block_ptr->header << 32 >> 36 << 4);
        free_block->prev_footer = (sf_header)0x0;
        free_block->prev_footer = (sf_header)r_block_ptr->header;
        if ((orig_size - size_of_block_realloc) >= 32) 
        {
            free_block->header = (sf_header)0x0;
            free_block->header |= (sf_header)((orig_size) - (size_of_block_realloc));
            free_block->header |= 0xc;
            sf_footer *free_block_footer = (void*)free_block + (free_block->header << 32 >> 36 << 4);
            *free_block_footer = (sf_header)0x0;
            *free_block_footer |= free_block->header;
            memcpy(pp, payload, rsize);
            void *ptr = free_block->body.payload;
            sf_free(ptr);
        }

        return pp;
    }
    else
    {
        return pp;
    }
    return NULL;
}

double sf_fragmentation() {
    // To be implemented.
    double fragmentation = 0;
    double total_payload_amount = 0;
    double total_size_alloc_blocks = 0;
    if (sf_malloc_init == 0)
        return 0.0;
    sf_block *block_ptr = sf_mem_start() + (char)sizeof(sf_block);
    while (block_ptr != (sf_mem_end() - (char)(sizeof(sf_header) * 2)))
    {
        if ((block_ptr->header << 60 >> 63) == (sf_header)1)
        {
            total_size_alloc_blocks += (double)(block_ptr->header << 32 >> 36 << 4);
            total_payload_amount += (double)(block_ptr->header >> 32);
        }
        block_ptr = (void*)block_ptr + (block_ptr->header << 32 >> 36 << 4);
    }
    fragmentation = total_payload_amount / total_size_alloc_blocks;
    return fragmentation;
}

double sf_utilization() {
    // To be implemented.
    if (sf_malloc_init == 0)   
        return 0;
    size_t max_aggregate_payload = 0;
    sf_block *block_ptr = sf_mem_start() + (char)sizeof(sf_block);
    while (block_ptr != (sf_mem_end() - (char)(sizeof(sf_header) * 2)))
    {
        if ((block_ptr->header << 60 >> 63) == (sf_header)1)
        {
            if ((sf_header)(block_ptr->header >> 32) > (sf_header)max_aggregate_payload)
                max_aggregate_payload = (size_t)(block_ptr->header >> 32);
        }
        block_ptr = (void*)block_ptr + (block_ptr->header << 32 >> 36 << 4);
    }
    size_t heap_size = (size_t)sf_mem_end() - (size_t)sf_mem_start();
    double peak_utilization = ((double)max_aggregate_payload / (double)heap_size);
    return peak_utilization;
}
