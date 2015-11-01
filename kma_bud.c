/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the buddy algorithm
 *    Author: Stefan Birrer
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    Revision 1.2  2009/10/31 21:28:52  jot836
 *    This is the current version of KMA project 3.
 *    It includes:
 *    - the most up-to-date handout (F'09)
 *    - updated skeleton including
 *        file-driven test harness,
 *        trace generator script,
 *        support for evaluating efficiency of algorithm (wasted memory),
 *        gnuplot support for plotting allocation and waste,
 *        set of traces for all students to use (including a makefile and README of the settings),
 *    - different version of the testsuite for use on the submission site, including:
 *        scoreboard Python scripts, which posts the top 5 scores on the course webpage
 *
 *    Revision 1.1  2005/10/24 16:07:09  sbirrer
 *    - skeleton
 *
 *    Revision 1.2  2004/11/05 15:45:56  sbirrer
 *    - added size as a parameter to kma_free
 *
 *    Revision 1.3  2004/12/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *

 ************************************************************************/
 
 /************************************************************************
 Project Group: NetID1, NetID2, NetID3
 
 ***************************************************************************/

#ifdef KMA_BUD
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
#include <stdio.h> 

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"



/************Global Variables*********************************************/
#define MIN_BUFFER_SIZE 32
			// index 	0  | 1  |  2  |  3  |  4  |  5   |  6   |   7
#define BUFFER_NUM 8 // 32 | 64 | 128 | 256 | 512 | 1024 | 2048 | 4096
#define BITMAP_NUM PAGESIZE / MIN_BUFFER_SIZE / sizeof(int) / 8
static kma_page_t* page_head = NULL;

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */
typedef struct block_node_struct{
	struct block_node_struct* next;
} block_node_t;

typedef struct page_node_struct {
	kma_page_t* ptr_back; // pointing back to the kma struct for free page
	unsigned int bitmap[BITMAP_NUM];
} page_node_t;

typedef struct {
	void* ptr_back;
	block_node_t* block_entry[BUFFER_NUM];
	int page_count;
} entry_page_node_t;


/************Function Prototypes******************************************/
kma_size_t find_round_size(kma_size_t);
void set_bit(unsigned int [], unsigned int);
void clear_bit(unsigned int [], unsigned int);
int get_bit(unsigned int [], unsigned int);
int find_block_index(kma_size_t);
void add_block(void*, int);
void init_free_block(kma_page_t*);
void* init_page_node(int);
void init_entry_page_node();
void* find_free_block(block_node_t* block_entry[], int);
bool page_ready_to_free(void*);
block_node_t* find_buddy(void* page, void* ptr, kma_size_t round_size);
void remove_buddy_block(block_node_t* buddy, int index);
void remove_page_blocks(block_node_t* buddy, int index);
void free_page_node(void*);
void coalesce(void* page, void* ptr, kma_size_t round_size);
/************External Declaration*****************************************/

/***********Debug Function*************************************************/
void print_bitmap(unsigned int bitmap[]) {
	int i;
	for(i = 0; i < BITMAP_NUM; i++) {
		printf("%x ", bitmap[i]);
	}
	printf("\n");
}

void print_free_block() {
	int i;
	for(i = 0; i < BUFFER_NUM; i++) {
		printf("block size %d:   ", MIN_BUFFER_SIZE << i);
		block_node_t* cur = ((entry_page_node_t*)page_head->ptr)->block_entry[i];
		while(cur != NULL) {
			printf("%p ", cur);
			cur = cur->next;
		}
		printf("\n");
	}
}


/**************Implementation***********************************************/

kma_size_t find_round_size(kma_size_t size) {
	if(size == 0) return 0;
	kma_size_t round_size = 1;
	while(round_size < size){
		round_size <<= 1;
	}
	return round_size < MIN_BUFFER_SIZE ? MIN_BUFFER_SIZE : round_size;
}

void set_bit(unsigned int A[], unsigned int i) {
	int index = i / (sizeof(unsigned int) * 8);
	int pos = i % (sizeof(unsigned int) * 8);
	unsigned int mask = 1;
	mask <<= pos;
	A[index] |= mask;
}

void clear_bit(unsigned int A[], unsigned int i) {
	int index = i / (sizeof(unsigned int) * 8);
	int pos = i % (sizeof(unsigned int) * 8);
	unsigned int mask = 1;
	mask <<= pos;
	mask = ~mask;
	A[index] &= mask;
}

int get_bit(unsigned int A[], unsigned int i) {
	int index = i / (sizeof(unsigned int) * 8);
	int pos = i % (sizeof(unsigned int) * 8);
	unsigned int mask = 1;
	mask <<= pos;
	return ((A[index] & mask) == 0) ? 0 : 1;
}

int find_block_index(kma_size_t size) {
	int index = 0;
	while(size != 0){
		index++;
		size >>= 1;
	}
	return index - 6;
}

/* This function add node at the beginning of the list */
void add_block(void* addr, int index) {
	((block_node_t*)addr)->next = ((entry_page_node_t*)page_head->ptr)->block_entry[index];
	((entry_page_node_t*)page_head->ptr)->block_entry[index] = (block_node_t*)addr;
}

/* 
 *  Split the new page into 64, 128, 256, 512, 1024, 2048, 4096 blocks
 *  The rest 64B stores the page_node_t structure
 */
void init_free_block(kma_page_t* page) {
	int size = (int)sizeof(page_node_t);
	kma_size_t cur_size = PAGESIZE;
	while(cur_size / 2 > size) {
		cur_size /= 2;
		int index = find_block_index(cur_size);
		add_block(page->ptr + cur_size, index);
	}
}

/*
 * mode = 1 to return the whole page, without splitting into blocks
 * O.W. mode = 0
 */ 

void* init_page_node(int mode) { 
	kma_page_t* page = get_page();
	page_node_t* page_node = (page_node_t*)(page->ptr);
	page_node->ptr_back = page;
	/* Init bitmap for new page */
	int i;
	for(i = 0; i < PAGESIZE / MIN_BUFFER_SIZE; i++)
		if(((sizeof(page_node_t) - 1) / MIN_BUFFER_SIZE + 1) > i || mode == 1)
			set_bit(page_node->bitmap, i);
		else
			clear_bit(page_node->bitmap, i);
	/* Split new page, Updtaes free list */
	if(mode == 0)
		init_free_block(page);
	/* Increament on page_count */
	((entry_page_node_t*)page_head->ptr)->page_count++;
	return page->ptr;
}
 

/* 
 * This function initialize the entry page 
 * Entry page stores array of different block size headers
 */
void init_entry_page_node() {
	kma_page_t* page;
	page = get_page();
	entry_page_node_t* entry_page_node = (entry_page_node_t*)(page->ptr);
	entry_page_node->ptr_back = page;
	entry_page_node->page_count = 1;
	int i;
	for(i = 0; i < BUFFER_NUM; i++){
		entry_page_node->block_entry[i] = NULL;
	}
	page_head = page;
}

/* 
 * This function find the required block from free list
 * If no available block exists, allocate new page
 */
void* find_free_block(block_node_t* block_entry[], int round_size){
	/* Find first fit free block */
	int i, c;
	unsigned int k;
	for(i = 0; i < BUFFER_NUM; i++) {
		if(round_size <= (MIN_BUFFER_SIZE << i) && block_entry[i] != NULL) {
			/* Delete this block from list */
			block_node_t* addr = block_entry[i];
			block_entry[i] = block_entry[i]->next;
			/* Split the block if possible */
			int cur_size = MIN_BUFFER_SIZE << i;
			if(cur_size / 2 >= round_size) {
				cur_size /= 2;
				int index = i - 1;
				add_block((void*)addr, index);
				add_block((void*)addr + cur_size, index);
				return find_free_block(block_entry, round_size);
			}			
			/* modify bitmap and return the address */  
			void* start_of_page = BASEADDR(addr);
			k = ((void*)addr - start_of_page) / MIN_BUFFER_SIZE;
			for(c = cur_size / MIN_BUFFER_SIZE; c >= 1; c--, k++) {
				set_bit(((page_node_t*)start_of_page)->bitmap, k);
			}
			return (void*)addr;
		}
	}
	/* No available block is found, initialize new page */
	init_page_node(0);
	return find_free_block(block_entry, round_size);
}


void* kma_malloc(kma_size_t size) {
	/* if over size */
	if((size + (int)sizeof(page_node_t)) > PAGESIZE) return NULL;

	/* if first page does not exist */  
	kma_size_t round_size = find_round_size(size);
	if(page_head == NULL) {
		init_entry_page_node();
		/* If required size is over 4096, give the whole page */
		if(size > 4096){
			void* addr = init_page_node(1);
			return addr + sizeof(page_node_t);
		} else {
			init_page_node(0);
		}
	}
	if(size > 4096){
		void* addr = init_page_node(1);
		return addr + sizeof(page_node_t);
	}

	/* find a block from free block list */
	/* O.W. create a new page and initialize it */
	return find_free_block(((entry_page_node_t*)page_head->ptr)->block_entry, round_size);
}

/* Check the bitmap to see whether the page can be freed */
bool page_ready_to_free(void* start_of_page) {
	int i;
	unsigned long long sum = 0;
	for(i = 0; i < BUFFER_NUM; i++){
		sum += ((page_node_t*)start_of_page)->bitmap[i];
	}
	return sum == 3 ? TRUE : FALSE;
}

/* Find buddy, if not exists, return NULL */
block_node_t* find_buddy(void* page, void* ptr, kma_size_t round_size) {
	block_node_t* buddy = (block_node_t*)((unsigned long int)ptr ^ (unsigned long int)round_size);
	int k = ((void*)buddy - page) / MIN_BUFFER_SIZE;
	int i = round_size / MIN_BUFFER_SIZE;
	for(; i > 0; i--, k++) {
		if(get_bit(((page_node_t*)page)->bitmap, k) == 1)
			return NULL;
	}
	return buddy;
}

/*
 *  Remove target block from the free list
 */
void remove_buddy_block(block_node_t* target, int index){
	block_node_t* head = ((entry_page_node_t*)page_head->ptr)->block_entry[index];;
	if(head == NULL) return;
	/* Check first node */
	if(head - target == 0){
		head = head->next;
		((entry_page_node_t*)page_head->ptr)->block_entry[index] = head;
		return;
	}		
	/* Check rest nodes */
	block_node_t* cur = head->next;
	block_node_t* prev = head;
	while(cur != NULL) {
		if(cur - target == 0){
			prev->next = cur->next;
			return;
		}
		cur = cur->next;
		prev = prev->next;
	}	
}

/* Remove all the blocks in a page from the free list */
void remove_page_blocks(block_node_t* start_of_page, int index){
	block_node_t* head = ((entry_page_node_t*)page_head->ptr)->block_entry[index];;
	if(head == NULL) return;
	/* Find first node which does not need to be deleted as the new head */
	while(BASEADDR(head) == (void*)start_of_page) {
		head = head->next;
		if(head == NULL){
			((entry_page_node_t*)page_head->ptr)->block_entry[index] = NULL;
			return;
		}
	}	
	((entry_page_node_t*)page_head->ptr)->block_entry[index] = head;
	/* Check rest nodes */
	block_node_t* cur = head->next;
	block_node_t* prev = head;
	while(cur != NULL) {
		if(BASEADDR(cur) == (void*)start_of_page) {
			prev->next = cur->next;
		} else {
			prev = prev->next;
		}
		cur = cur->next;
	}	
}

void free_page_node(void* start_of_page) {
	int index = 0;
	for(; index < BUFFER_NUM; index++) {
		remove_page_blocks(start_of_page, index);
	}
}

void coalesce(void* page, void* ptr, kma_size_t round_size){
	int index = find_block_index(round_size);
	while(1 == 1){
		int size = 1 << (index + 5);
		block_node_t* buddy = find_buddy(page, ptr, size);
		/* If no buddy exists */
		if(buddy == NULL)
			break;
		/* O.W. remove buddy from free list */
		remove_buddy_block(buddy, index);
		ptr = ptr - (void*)buddy > 0 ? (void*)buddy : ptr;
		index++;
	}
	add_block(ptr, index);
}

void kma_free(void* ptr, kma_size_t size) {
	void* start_of_page = BASEADDR(ptr);
	kma_size_t round_size = find_round_size(size);
	/* If round size is PAGESIZE, directly free this page */
	if(round_size == PAGESIZE) {
		free_page(((page_node_t*)start_of_page)->ptr_back);
		/* Free Entry page if it is the last page */
		if(--((entry_page_node_t*)page_head->ptr)->page_count == 1) {
			free_page(page_head);
			page_head = NULL;
		}
		return;
	}
	/* Update bitmap */ 
	int k = (ptr - start_of_page) / MIN_BUFFER_SIZE;
	int i;
	for(i = round_size / MIN_BUFFER_SIZE; i >= 1; i--, k++) {
		clear_bit(((page_node_t*)start_of_page)->bitmap, k);
	}
	/* if page can be free, No need to coalesce */
	if(page_ready_to_free(start_of_page)) {
		/* Remove blocks from free list */
		free_page_node(start_of_page);
		/* Free page */
		free_page(((page_node_t*)start_of_page)->ptr_back);
		/* Free Entry page if it is the last page */
		if(--((entry_page_node_t*)page_head->ptr)->page_count == 1) {
			free_page(page_head);
			page_head = NULL;
		}
		return;
	}
	/* O.W. Coalesce and modify free list, No need to free this page */
	coalesce(start_of_page, ptr, round_size);
}

#endif // KMA_BUD
