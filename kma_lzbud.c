/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the SVR4 lazy budy
 *             algorithm
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
 *    Revision 1.1  2004/11/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *
 *    Revision 1.3  2004/12/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *
 ************************************************************************/

/************************************************************************
 Project Group: NetID1, NetID2, NetID3
 
 ***************************************************************************/
 
#ifdef KMA_LZBUD
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */
#define MINPOWER 5 //2^5 = 32
#define MINSIZE 32 //min block size
#define HDRSIZE 9 //we need an array of size 9 to store 9 diff buffer sizes
#define MAPSIZE (PAGESIZE/MINSIZE)/(sizeof(int)*8)

typedef struct blk_ptr{
  struct blk_ptr* next;
} blk_ptr_t;

//2 int is sizeof(int) = 8 *4 = 32 byte
typedef struct pg_hdr{
  kma_page_t* this;
  //bitmap to decide if buddy is free.
  unsigned int bitmap[MAPSIZE];
  struct pg_hdr* prev;
  struct pg_hdr* next;
} pg_hdr_t;

//buffer list struct
typedef struct {
	//slack to control lazy buddy system 
	int slack;
  int size;
  blk_ptr_t* next;
} bf_lst_t;

//controller for free_list and page_list.
typedef struct {
  int allocated;
  int freed;
  bf_lst_t free_list[HDRSIZE];
  pg_hdr_t* page_list;
} mem_ctrl_t;

/************Global Variables*********************************************/
static kma_page_t* entry_page = NULL;
/************Function Prototypes******************************************/
mem_ctrl_t* pg_master();
int next_power_of_two(int);
void* kma_malloc(kma_size_t);
void kma_free(void*, kma_size_t);
void* find_fit(kma_size_t);
void init_page();
void* get_new_free_block(kma_size_t);
void add_to_free_list(void*, int);
void delete_block(void*, int);
void set_bit(unsigned int[], int);
void unset_bit(unsigned int[], int);
int get_bit(unsigned int[], int);
int get_pos(void*);
int get_index(int);
void set_bitmap(void*, kma_size_t);
void unset_bitmap(void*, kma_size_t);
void* find_buddy(void*, int);
bool is_free(void*, int);
bool is_locally_free(void*, int);
void* find_locally_free_block(kma_size_t);
void coalesce(void*, kma_size_t);
void split_block(kma_size_t, int);
/************External Declaration*****************************************/

/**************Implementation***********************************************/
//The manager of the allocater, keep tracking the free_list and page_list
mem_ctrl_t* pg_master(){
  return (mem_ctrl_t*)((void*)entry_page->ptr + sizeof(kma_page_t*));
}
//get the next power of two of the size
int next_power_of_two(int n) {
  int p = 1;
  if (n && !(n & (n-1)))
    return n;

  while (p < n) {
    p <<= 1;
  }
  return p;
}
//---------KMA_MALLOC-----------//
void* kma_malloc(kma_size_t size) {
  if (size + sizeof(void*) > PAGESIZE)  
    return NULL;

  if (entry_page == NULL)
    init_page();
  if (size < MINSIZE)
  	size = MINSIZE;
  //all operations after round up size can have a benefit for not caring about the size.
  size = next_power_of_two(size);

  mem_ctrl_t* controller = pg_master();
  void* block = find_fit(size);
  controller->allocated++;

  return block;
}
//initialize the entry_page
void init_page() {
  kma_page_t* new_page = get_page();
  entry_page = new_page;
  *((kma_page_t**)new_page->ptr) = new_page;

  mem_ctrl_t* controller = pg_master();
  
  controller->page_list = (pg_hdr_t*)((void*)controller + sizeof(mem_ctrl_t));
  // use this to point to the kma_page_t struct for free_page()
  controller->page_list->this = (kma_page_t*)new_page->ptr;
  controller->page_list->prev = NULL;
  controller->page_list->next = NULL;

  int i;
  for (i = 0; i < HDRSIZE; i++) {
    controller->free_list[i].size = (1 << (i+MINPOWER));
    controller->free_list[i].next = NULL;
    controller->free_list[i].next = NULL;
  }
  //because we use a block in front of the entry_page
  //to store some info of the page and allocator
  //we round up the size to power of two
  //and add (2^i > pre_alloc_size) to free_list
  int pre_alloc = sizeof(kma_page_t*) + sizeof(mem_ctrl_t) + sizeof(pg_hdr_t);
  pre_alloc = next_power_of_two(pre_alloc);
  void* start = (void*)new_page->ptr + pre_alloc;
  void* end = (void*)new_page->ptr + PAGESIZE;
  int sz = pre_alloc;
  while (start < end) {
  	add_to_free_list(start, sz);
  	start += sz;
  	sz = sz * 2;	
  }
  //initialize the bitmap
  for (i = 0; i < MAPSIZE; i++) {
  	controller->page_list->bitmap[i] = 0;
  }
  int pre_alloc_pos = pre_alloc/MINSIZE;
  for (i = 0; i < pre_alloc_pos; i++) {
  	set_bit(controller->page_list->bitmap, i);
  }  
  controller->allocated = 0;
  controller->freed = 0;

}
//i = 0: 0-31
//i = 1: 32-63...
//set one bit to one.
void set_bit(unsigned int bitmap[], int pos) {
	int i = pos/(sizeof(int)*8);
	int offset = pos%(sizeof(int)*8);
	unsigned int flag = 1;
	flag = flag << offset;
	bitmap[i] = bitmap[i] | flag;
}
//set one bit to zero.
void unset_bit(unsigned int bitmap[], int pos) {
	int i = pos/(sizeof(int)*8);
	int offset = pos%(sizeof(int)*8);
	unsigned int flag = 1;
	flag = flag << offset;
	flag = ~flag;
	bitmap[i] = bitmap[i] & flag;
}
//get the value of one bit
int get_bit(unsigned int bitmap[], int pos) {
	int i = pos/(sizeof(int)*8);
	int offset = pos%(sizeof(int)*8);
	unsigned int flag = 1;
	flag = flag << offset;
	if (bitmap[i] & flag)
		return 1;
	else
		return 0;
}
//get the start position of ptr in the bitmap
int get_pos(void* ptr) {
	return (ptr - BASEADDR(ptr))/MINSIZE;
}
//set the bitmap for one blk, set all their corresponding bit to one.
void set_bitmap(void* blk, kma_size_t size) {
	size = next_power_of_two(size);
	pg_hdr_t* current_page;
	if (BASEADDR(blk) == entry_page->ptr)
		current_page = (pg_hdr_t*)(BASEADDR(blk) + sizeof(kma_page_t*) + sizeof(mem_ctrl_t));
	else
		current_page = (pg_hdr_t*)(BASEADDR(blk) + sizeof(kma_page_t*));
	int pos = get_pos(blk);//the start positon on the bitmap;
	int i;
	for (i = 0; i < size/MINSIZE; i++)
		set_bit(current_page->bitmap, pos+i);
}
//unset the bitmap for one blk, set all their corresponding bit to zero.
void unset_bitmap(void* blk, kma_size_t size) {
	size = next_power_of_two(size);
	pg_hdr_t* current_page;
	if (BASEADDR(blk) == entry_page->ptr)
		current_page = (pg_hdr_t*)(BASEADDR(blk) + sizeof(kma_page_t*) + sizeof(mem_ctrl_t));
	else
		current_page = (pg_hdr_t*)(BASEADDR(blk) + sizeof(kma_page_t*));
	int pos = get_pos(blk);//the start positon on the bitmap;
	int i;
	for (i = 0; i < size/MINSIZE; i++)
		unset_bit(current_page->bitmap, pos+i);
}
//get the index for each size. e.g. index(16) = 0, index(32) = 1.
int get_index(int n) {
  n = next_power_of_two(n);
  int count = 0;
  while(n) {
    count++;
    n >>= 1;
  }
  return count - MINPOWER - 1;
}
//if the corresponding bits of request block in bitmap are all ones.
//the result is versus to the is_free
bool is_locally_free(void* ptr, int size) {
	pg_hdr_t* current_page;
	if (BASEADDR(ptr) == entry_page->ptr)
		current_page = (pg_hdr_t*)(BASEADDR(ptr) + sizeof(kma_page_t*) + sizeof(mem_ctrl_t));
	else
		current_page = (pg_hdr_t*)(BASEADDR(ptr) + sizeof(kma_page_t*));
	int offset = (ptr-BASEADDR(ptr))/MINSIZE;
	int i;
	int flag;
	for (i = 0; i < size/MINSIZE; i++) {
		flag = get_bit(current_page->bitmap, offset+i);
		if (flag != 1) 
			return FALSE;
	}
	return TRUE;
}

//find the free block in the corresponding buffer size list of free_list.
//if the free block not found, to request a new free block from the larger size block
//we split the larger block into two and add them into free_list (recursively)
//until we can find the request size block. 
//else if there is no larger block in the free_list for this request, get a new page.
void* find_fit(kma_size_t size) {
  mem_ctrl_t* controller = pg_master();

  int ind = get_index(size);
  void* blk = NULL;
  void* bud = NULL;
  bf_lst_t lst = controller->free_list[ind];
  if (lst.next) {
    blk = (void*)lst.next;
    //remove free block and set the corresponding bits in bitmap to one.
    controller->free_list[ind].next = controller->free_list[ind].next->next;
  	//this part is for lazy body
  	//there is any free block, select one to allocate
  	//if the selected block is locally free: slack += 2
  	//else slack += 1
  	if (is_locally_free(blk, size)) {
    	controller->free_list[ind].slack += 2;
    }
    else {
    	controller->free_list[ind].slack += 1;
    }
    set_bitmap(blk, size);//set globally free
    //check the buddy because we need to mark the other block locally free
    //when we split larger block into two (recursively)
    bud = find_buddy(blk, size);
    //check whether the next one is from split_block or not. 
    if (controller->free_list[ind].next == bud)
    	set_bitmap(bud, size);
  }
  else {
    blk = get_new_free_block(size);
  }
  return blk;
}
void split_block(kma_size_t size, int index) {
	mem_ctrl_t* controller = pg_master();
	bf_lst_t lst = controller->free_list[index];
	blk_ptr_t* current = lst.next;
	//larger size/2
	int sz = (1 << (index + MINPOWER -1));
	//remove larger one, split into small one and add to free_list.
	controller->free_list[index].next = controller->free_list[index].next->next;
	add_to_free_list((void*)current + sz, sz);
	add_to_free_list((void*)current, sz);
}
//add block to the free_list
void add_to_free_list(void* block, int size) {
  mem_ctrl_t* controller = pg_master();
  int ind = get_index(size);
  ((blk_ptr_t*)block)->next = controller->free_list[ind].next;
  controller->free_list[ind].next = (blk_ptr_t*)block;
  return;
}
//get a new free block
void* get_new_free_block(kma_size_t size) {;
  int ind = get_index(size);
  mem_ctrl_t* controller = pg_master();

  int i;
  //check the larger size buffer list, not include 8192
  for (i = ind + 1; i < HDRSIZE - 1; i++) {
  	bf_lst_t lst = controller->free_list[i];
  	if (lst.next) {
  		//split block and re-search the free_list
			split_block(size, i);
			return find_fit(size); 
   	}
  }
  //get a new page, because it is not the enrty_page, so we can get extra space
  //for not including mem_ctrl_t structure any more.
  //so the pre_alloc_space is smaller than entry_page
  kma_page_t* new_page = get_page();
  *((kma_page_t**)new_page->ptr) = new_page;
  pg_hdr_t* current = (pg_hdr_t*)((void*)new_page->ptr + sizeof(kma_page_t*));
  current->this = (kma_page_t*)(new_page->ptr);
  current->next = NULL;
  //add this page to page_list
  pg_hdr_t* previous = controller->page_list;
  while (previous) {
    if (previous->next == NULL) {
      current->prev = previous;
      previous->next = current;
      break;
    }
    else
      previous = previous->next;
  }

  if (size > 4096) {
  	// if size > 4096, just return this page to the request
    return (void*)((void*)current + sizeof(pg_hdr_t));
  }
  else {
  	int pre_alloc = sizeof(kma_page_t*) + sizeof(pg_hdr_t);
	  pre_alloc = next_power_of_two(pre_alloc);
	  void* start = (void*)new_page->ptr + pre_alloc;
	  void* end = (void*)new_page->ptr + PAGESIZE;
	  int sz = pre_alloc;
	  while (start < end) {
	  	add_to_free_list(start, sz);
	  	start += sz;
	  	sz = sz * 2;  	
	  }
	  //init bitmap
	  int pre_alloc_pos = pre_alloc/MINSIZE;
	  for (i = 0; i < MAPSIZE; i++) {
	  	current->bitmap[i] = 0;
	  }
	  //set bitmap
	  for (i = 0; i < pre_alloc_pos; i++) {
	  	set_bit(current->bitmap, i);
	  }
	  return find_fit(size); 
  }
}
//find buddy of request block, return the buddy address
void* find_buddy(void* ptr, int size) {
	unsigned long offset = ptr-(BASEADDR(ptr));
	int i = get_index(size);
	unsigned long bud = offset ^ (1UL << (i+MINPOWER));
	return (void*)(BASEADDR(ptr) + bud);

}
//check if the corresponding bits of request block in bitmap are all zero.
//if all zeros, return true, means this block is globaly free.
//else this block is locally free(is_locally_freefor lzbud) 
bool is_free(void* ptr, int size) {
	pg_hdr_t* current_page;
	if (BASEADDR(ptr) == entry_page->ptr)
		current_page = (pg_hdr_t*)(BASEADDR(ptr) + sizeof(kma_page_t*) + sizeof(mem_ctrl_t));
	else
		current_page = (pg_hdr_t*)(BASEADDR(ptr) + sizeof(kma_page_t*));
	int offset = (ptr-BASEADDR(ptr))/MINSIZE;
	int i;
	int flag;
	for (i = 0; i < size/MINSIZE; i++) {
		flag = get_bit(current_page->bitmap, offset+i);
		if (flag != 0) 
			return FALSE;
	}
	return TRUE;
}
//use to delete block in the free_list.
//when you coalesce two block, you need to delete two blocks
//after that, add one larger to the free_list
//no need to set or unset bitmap
void delete_block(void* ptr, int size) {
	mem_ctrl_t* controller = pg_master();
	int i = get_index(size);
	bf_lst_t lst = controller->free_list[i];
	blk_ptr_t* cur = lst.next;
	blk_ptr_t* prev;
	if (cur == NULL)
		return;
	if (cur == ptr) {
		controller->free_list[i].next = controller->free_list[i].next->next;
		cur->next = NULL;
		return;
	}
	else {
		prev = cur;
		cur = cur->next;
		while(cur != NULL) {
			if (cur == ptr) {
				prev->next = cur->next;
				cur->next = NULL;
				return;
			}
			prev = cur;
			cur = cur->next;
		}
	} 
}
//coalesce buddy blocks recursively
void coalesce(void* ptr, kma_size_t size) {
	void* bud = find_buddy(ptr, size);
	if (is_free(bud, size)) {
		delete_block(ptr, size);
		delete_block(bud, size);
		void* new_blk;
		if (ptr < bud)
			new_blk = ptr;
		else
			new_blk = bud;
		int new_size = 2 * size;
		//we don't want to care about 8192
		//just ignore it!
		if (new_size > 4096)
			return;
		add_to_free_list(new_blk, new_size);
		coalesce(new_blk, new_size);
	}
}
//find a locally_free_block of a given size
//or return NULL
void* find_locally_free_block(kma_size_t size) {
	mem_ctrl_t* controller = pg_master();
	int ind = get_index(size);
	bf_lst_t lst = controller->free_list[ind];
	blk_ptr_t* cur = lst.next;
	while (cur != NULL) {
		if (is_locally_free(cur, size))
			return cur;
		cur = cur->next;
	}
	return (void*)cur;
}

void kma_free(void* ptr, kma_size_t size)
{ 
	if (size < MINSIZE) 
		size = MINSIZE;
	size = next_power_of_two(size);
	mem_ctrl_t* controller = pg_master();
	int ind = get_index(size);
	int slck = controller->free_list[ind].slack;
	//if slack >= 2
	//mark it locally free and free it locally
	//slack -= 2.
	if (slck >= 2) {
		add_to_free_list(ptr, size);
		controller->free_list[ind].slack -= 2;
	}
	//if slack = 1
	//mark it globally free and free it globally; coalesce if possible
	//slack = 0.
	else if (slck == 1){
		add_to_free_list(ptr, size);
		unset_bitmap(ptr, size);
  	coalesce(ptr, size);
  	controller->free_list[ind].slack = 0;
	}
	//if slack = 0
	//mark it globally free and free it globally; coalesce if possible
	//select one locally free block of size 2^i and free it globally; coalesce if possible
	//slack = 0.
	else if (slck == 0) {
		add_to_free_list(ptr, size);
  	unset_bitmap(ptr, size);
  	coalesce(ptr, size);
  	controller->free_list[ind].slack = 0;
  	void* locally_free_block = find_locally_free_block(size);
  	if (locally_free_block != NULL) {
  		unset_bitmap(locally_free_block, size);
  		coalesce(locally_free_block, size);
  	}
  	controller->free_list[ind].slack = 0;
	}

  controller->freed++;
  //if free operations and alloc operations are the same amounts
  //free all pages
  if (controller->freed == controller->allocated){
    pg_hdr_t* current_page = controller->page_list;
  	while (current_page) {
    	kma_page_t* page = *(kma_page_t**)current_page->this;
    	current_page = current_page->next;
    	free_page(page);
  	}
  	entry_page = NULL;
  }
  return;
}

#endif // KMA_LZBUD
