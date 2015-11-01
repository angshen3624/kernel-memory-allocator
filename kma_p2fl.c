/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the power-of-two free list
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

#ifdef KMA_P2FL
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */
#define MINPOWER 4 //2^4 = 16
#define MINSIZE 16 //min block size
#define HDRSIZE 10 //we need an array of size 10 to store 10 diff buffer sizes

typedef struct blk_ptr{
  struct blk_ptr* next;
} blk_ptr_t;


typedef struct pg_hdr{
  kma_page_t* this;
  struct pg_hdr* prev;
  struct pg_hdr* next;
  //the space we can use for this page
  int f_size; 
} pg_hdr_t;

//buffer list struct
typedef struct {
  int size;
  blk_ptr_t* next;
} bf_lst_t;

//controller for free_list and page_list;
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
void free_all();
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
//need to consider block pointer for extra space
void* kma_malloc(kma_size_t size) {
  if (size + sizeof(void*) > PAGESIZE)  
    return NULL;

  if (entry_page == NULL)
    init_page();

  //size need to consider the header of block
  size += sizeof(blk_ptr_t);
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
  
  controller->page_list = (pg_hdr_t*)((void*)new_page->ptr + sizeof(kma_page_t*) + sizeof(mem_ctrl_t));
  // use this to point to the kma_page_t struct for free_page()
  controller->page_list->this = (kma_page_t*)new_page->ptr;
  controller->page_list->prev = NULL;
  controller->page_list->next = NULL;
  //the free space for this page
  controller->page_list->f_size = PAGESIZE - sizeof(kma_page_t*) - sizeof(mem_ctrl_t) - sizeof(pg_hdr_t);
  int i;
  //initialize the free_list for each buffer size
  for (i = 0; i < HDRSIZE; i++) {
    controller->free_list[i].size = 1 << (i + MINPOWER);
    controller->free_list[i].next = NULL;
  } 
  controller->allocated = 0;
  controller->freed = 0;
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
//find the free block in the corresponding buffer size list of free_list.
//if the free block not found, to request a new free block in this page.
//else if the page has not enough space for this request, get a new page.
void* find_fit(kma_size_t size) {
  mem_ctrl_t* controller = pg_master();

  int ind = get_index(size);
  void* blk = NULL;
  bf_lst_t lst = controller->free_list[ind];
  if (lst.next) {
    blk = (void*)lst.next;
    //remove from free_list
    controller->free_list[ind].next = controller->free_list[ind].next->next;
  }
  else {
    blk = get_new_free_block(size);
  }

  return blk;
}
//get a new free block.
void* get_new_free_block(kma_size_t size) {
  mem_ctrl_t* controller = pg_master();
  pg_hdr_t* current_page = controller->page_list;

  while (current_page) {
    //check if request size <= 4096 and this page has enough size
    if (size <= 4096 && current_page->f_size > size) {
      current_page->f_size = current_page->f_size - size;
      return (void*)((void*)current_page->this + (PAGESIZE - current_page->f_size) - size);
    }
    else 
      current_page = current_page->next;
  }

  //get a new page, because it is not the enrty_page, so we can get extra space
  //for not including mem_ctrl_t structure any more.
  kma_page_t* new_page = get_page();
  *((kma_page_t**)new_page->ptr) = new_page;
  pg_hdr_t* current = (pg_hdr_t*)((void*)new_page->ptr + sizeof(kma_page_t*));
  current->this = (kma_page_t*)(new_page->ptr);
  current->next = NULL;
  current->f_size = PAGESIZE - sizeof(kma_page_t*) - sizeof(pg_hdr_t);
  //add this page to the page_list
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
    current->f_size = 0;
    return (void*)((void*)current + sizeof(pg_hdr_t));
  }
  else {
    current->f_size -= size;
    return (void*)((void*)new_page->ptr + (PAGESIZE - current->f_size) - size); 
  }
}
//add block to the free_list
void add_to_free_list(void* block, int size) {
  mem_ctrl_t* controller = pg_master();
  int ind = get_index(size);
  // we just add the free_block in front of the free_list
  ((blk_ptr_t*)block)->next = controller->free_list[ind].next;
  controller->free_list[ind].next = (blk_ptr_t*)block;
  return;
}

void kma_free(void* ptr, kma_size_t size)
{ 
  size += sizeof(blk_ptr_t);
  if (size < MINSIZE) 
    size = MINSIZE;
  // same measurement as kma_malloc
  size = next_power_of_two(size);

  add_to_free_list(ptr, size);
  mem_ctrl_t* controller = pg_master();
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
#endif // KMA_P2FL
