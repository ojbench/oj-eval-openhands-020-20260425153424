#include "buddy.h"
#include <stdlib.h>
#define NULL ((void *)0)

#define MAX_RANK 16
#define PAGE_SIZE 4096

typedef struct block {
    struct block *next;
    int rank;
    int allocated;
} block_t;

static void *memory_start = NULL;
static int total_pages = 0;
static block_t *free_lists[MAX_RANK + 1];
static block_t *blocks = NULL;

int init_page(void *p, int pgcount) {
    if (pgcount <= 0) return -EINVAL;
    
    memory_start = p;
    total_pages = pgcount;
    
    // Initialize free lists
    for (int i = 1; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }
    
    // Create block metadata array
    blocks = (block_t *)malloc(pgcount * sizeof(block_t));
    if (!blocks) return -EINVAL;
    
    // Initialize all blocks
    for (int i = 0; i < pgcount; i++) {
        blocks[i].next = NULL;
        blocks[i].rank = 1;
        blocks[i].allocated = 0;
    }
    
    // Find the maximum rank that can contain all pages
    int max_rank = 1;
    int pages_needed = pgcount;
    while (pages_needed > 1 && max_rank < MAX_RANK) {
        pages_needed = (pages_needed + 1) / 2;
        max_rank++;
    }
    
    // Add the entire memory pool to the appropriate free list
    blocks[0].rank = max_rank;
    blocks[0].next = NULL;
    free_lists[max_rank] = &blocks[0];
    
    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) return ERR_PTR(-EINVAL);
    
    // Find a free block of the required rank or larger
    int current_rank = rank;
    while (current_rank <= MAX_RANK && !free_lists[current_rank]) {
        current_rank++;
    }
    
    if (current_rank > MAX_RANK) return ERR_PTR(-ENOSPC);
    
    // Remove block from free list
    block_t *block = free_lists[current_rank];
    free_lists[current_rank] = block->next;
    block->next = NULL;
    
    // Split blocks if necessary
    while (current_rank > rank) {
        current_rank--;
        
        // Calculate the buddy block index
        int block_size = 1 << (current_rank - 1);
        int block_index = block - blocks;
        int buddy_index = block_index + block_size;
        
        if (buddy_index < total_pages) {
            // Create buddy block
            block_t *buddy = &blocks[buddy_index];
            buddy->rank = current_rank;
            buddy->allocated = 0;
            buddy->next = free_lists[current_rank];
            free_lists[current_rank] = buddy;
        }
        
        // Update current block
        block->rank = current_rank;
    }
    
    block->allocated = 1;
    
    // Calculate the memory address
    int block_index = block - blocks;
    return (char *)memory_start + block_index * PAGE_SIZE;
}

int return_pages(void *p) {
    if (!p) return -EINVAL;
    
    // Check if the address is within our memory range
    if (p < memory_start || p >= (void *)((char *)memory_start + total_pages * PAGE_SIZE)) {
        return -EINVAL;
    }
    
    // Calculate block index
    int block_index = ((char *)p - (char *)memory_start) / PAGE_SIZE;
    if (block_index < 0 || block_index >= total_pages || 
        ((char *)p - (char *)memory_start) % PAGE_SIZE != 0) {
        return -EINVAL;
    }
    
    block_t *block = &blocks[block_index];
    
    // Check if block is allocated
    if (!block->allocated) {
        return -EINVAL;
    }
    
    block->allocated = 0;
    int current_rank = block->rank;
    
    // Try to merge with buddy
    while (current_rank < MAX_RANK) {
        int block_size = 1 << (current_rank - 1);
        int block_index = block - blocks;
        
        // Calculate buddy index
        int buddy_index;
        if (block_index % (block_size * 2) == 0) {
            buddy_index = block_index + block_size;
        } else {
            buddy_index = block_index - block_size;
        }
        
        // Check if buddy exists and is free
        if (buddy_index < 0 || buddy_index >= total_pages) {
            break;
        }
        
        block_t *buddy = &blocks[buddy_index];
        if (buddy->allocated || buddy->rank != current_rank) {
            break;
        }
        
        // Remove buddy from free list
        block_t **prev = &free_lists[current_rank];
        while (*prev && *prev != buddy) {
            prev = &(*prev)->next;
        }
        if (*prev) {
            *prev = buddy->next;
        }
        
        // Merge blocks
        if (block_index > buddy_index) {
            block = buddy;
            block_index = buddy_index;
        }
        block->rank = current_rank + 1;
        current_rank++;
    }
    
    // Add merged block to free list
    block->next = free_lists[current_rank];
    free_lists[current_rank] = block;
    
    return OK;
}

int query_ranks(void *p) {
    if (!p) return -EINVAL;
    
    // Check if the address is within our memory range
    if (p < memory_start || p >= (void *)((char *)memory_start + total_pages * PAGE_SIZE)) {
        return -EINVAL;
    }
    
    // Calculate block index
    int block_index = ((char *)p - (char *)memory_start) / PAGE_SIZE;
    if (block_index < 0 || block_index >= total_pages || 
        ((char *)p - (char *)memory_start) % PAGE_SIZE != 0) {
        return -EINVAL;
    }
    
    block_t *block = &blocks[block_index];
    
    if (block->allocated) {
        return block->rank;
    } else {
        // For unallocated blocks, find the rank of the free block containing this page
        // Search through free lists to find which block contains this page
        for (int rank = MAX_RANK; rank >= 1; rank--) {
            int block_size = 1 << (rank - 1);
            block_t *current = free_lists[rank];
            while (current) {
                int current_index = current - blocks;
                if (block_index >= current_index && block_index < current_index + block_size) {
                    return rank;
                }
                current = current->next;
            }
        }
        // If not found in any free list, this page is part of a larger allocated block
        // that was split, so return its individual rank
        return block->rank;
    }
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) return -EINVAL;
    
    int count = 0;
    block_t *current = free_lists[rank];
    while (current) {
        count++;
        current = current->next;
    }
    
    return count;
}
