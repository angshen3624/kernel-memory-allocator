/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the resource map algorithm
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
 ***************************************************************************/
#ifdef KMA_RM
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

typedef struct {
  int size;
  void* next;
} blk_ptr_t;

typedef struct {
  void* this;
  blk_ptr_t* free_list;
  int allocated_block;
  int freed_block;
  //remeber total_pages for traversal
  int total_pages;  
} pg_hdr_t;

/************Global Variables*********************************************/

static kma_page_t* entry_page = NULL;

/************Function Prototypes******************************************/
void* kma_malloc(kma_size_t);
void make_new_page();
void add_to_free_list(blk_ptr_t*, int);
//void remove_from_free_list(blk_ptr_t*);
blk_ptr_t* find_first_fit(int);
void PrintFreeList();
void coalesce();
void free_all();
/************External Declaration*****************************************/

/**************Implementation***********************************************/

void*
kma_malloc(kma_size_t size)
{
  if (size + sizeof(void*) > PAGESIZE) {
    return NULL;
  }

  if (entry_page == NULL) {
    kma_page_t* new_page = get_page();

    entry_page = new_page;
    // add a pointer to the page structure at the beginning of the page
    *((kma_page_t**)(new_page->ptr)) = new_page;
    //page_header point to page space
    pg_hdr_t* page_header = (pg_hdr_t*)(new_page->ptr);
    page_header->free_list = (blk_ptr_t*)((void*)page_header + sizeof(pg_hdr_t));
    page_header->allocated_block = 0;
    page_header->freed_block = 0;
    page_header->total_pages = 0;
    blk_ptr_t* pos_to_add = (blk_ptr_t*)page_header->free_list;
    int size_to_add = PAGESIZE - sizeof(pg_hdr_t);
	  add_to_free_list(pos_to_add, size_to_add);
  }
  blk_ptr_t* block;
  block = find_first_fit(size);
  pg_hdr_t* first_page = (pg_hdr_t*)(entry_page->ptr);
	(first_page->allocated_block)++;

  return (void*)block;
}
//add to free_list in an order
void add_to_free_list(blk_ptr_t* block, kma_size_t size) {
  pg_hdr_t* first_page_header = (pg_hdr_t*)(entry_page->ptr);
  blk_ptr_t* current = first_page_header->free_list;
  blk_ptr_t* prev = current;
  block->size = size;

  if (current == NULL) {
  	first_page_header->free_list = block;
    block->next = NULL;
    return;
  }
  if (block == current) {
  	first_page_header->free_list = block;
  	block->next = NULL;
  	return;
  }
  else if (block < current) {
    block->next = first_page_header->free_list;
    first_page_header->free_list = block;
    return;
  }
  else {
  	if (current->next == NULL) {
  		current->next = block;
  	}
  	else {
  		prev = first_page_header->free_list;
  		current = current->next;
		  while(current!= NULL) {
		    if (block < current) {
		      prev->next = block;
		      block->next = current;
		      return;
		    }
		    prev = current;
		    current = current->next; 
		  }
		  prev->next = block;
		  block->next = NULL;
		}
	}	
}
//we find first fit to get the block
blk_ptr_t* find_first_fit(int size) {
  int min_size = sizeof(blk_ptr_t);
  if (size < sizeof(blk_ptr_t)) {
    size = min_size;
  }

  pg_hdr_t* first_page_header = entry_page->ptr;

  blk_ptr_t* prev = NULL;
  blk_ptr_t* current = first_page_header->free_list;;
  if (current->size >= size) {
    if (current->size == size || current->size - size < min_size) {
      first_page_header->free_list = current->next; 
    }
    else {
    	first_page_header->free_list = current->next; 
    	blk_ptr_t* pos_to_add = (blk_ptr_t*)((void*)current + size);
    	int size_to_add = current->size - size;
      add_to_free_list(pos_to_add, size_to_add);
    }
    return current;
  }

  prev = first_page_header->free_list;
  current = current->next;
  while(current != NULL) {
    if (current->size >= size) {
      if (current->size == size || current->size - size < min_size) {
        prev->next = current->next; 
      }
      else {
      	prev->next = current->next; 
    		blk_ptr_t* pos_to_add = (blk_ptr_t*)((void*)current + size);
    		int size_to_add = current->size - size;
      	add_to_free_list(pos_to_add, size_to_add);
      }
      return current;
    }
    prev = current;
    current = current->next;
  }//while end
  //get a new page if there is no block found
  kma_page_t* new_page = get_page();
  *((kma_page_t**)(new_page->ptr)) = new_page;
  pg_hdr_t* page_header = (pg_hdr_t*)(new_page->ptr);  
  page_header->free_list = (blk_ptr_t*)((void*)page_header + sizeof(pg_hdr_t));
  page_header->allocated_block = 0;
  page_header->freed_block = 0;
  page_header->total_pages = 0;
  void* pos_to_add = (void*)page_header + sizeof(pg_hdr_t) + size;
  int size_to_add = PAGESIZE - sizeof(pg_hdr_t)-size;
  add_to_free_list((blk_ptr_t*)pos_to_add, size_to_add);

  (first_page_header->total_pages)++;
  //not recursion
  return (blk_ptr_t*)((void*)new_page->ptr + sizeof(pg_hdr_t));
}
 
void
kma_free(void* ptr, kma_size_t size)
{
  blk_ptr_t* block = (blk_ptr_t*)ptr;
  add_to_free_list(block, size);
 	coalesce();
  pg_hdr_t* first_page = entry_page->ptr;
  (first_page->freed_block)++;

  if (first_page->allocated_block == first_page->freed_block) {
    free_all();
  }

  return;
}
//free all pages
void free_all() {
  pg_hdr_t* first_page = (pg_hdr_t*)(entry_page->ptr);
  int num = first_page->total_pages;
  int i = 0;
  while(i <= num) {
    pg_hdr_t* current_page = (pg_hdr_t*)((void*)first_page + i*PAGESIZE);
    kma_page_t* page = (kma_page_t*)current_page->this;
    free_page(page);
    i++;
  }
  entry_page = NULL;
}
//traverse the whole free_list
void coalesce() {
	pg_hdr_t* first_page = (pg_hdr_t*)(entry_page->ptr);
	if (first_page->free_list == NULL) {
		return;
	}
	blk_ptr_t* current = first_page->free_list;
	while (current->next != NULL) {
		if ((void*)current + current->size == current->next) {
			blk_ptr_t* current_next = current->next;
			current->size = current->size + current_next->size;
			current->next = current_next->next;
			continue;
		}
		current = current->next;
	}
}


#endif // KMA_RM
