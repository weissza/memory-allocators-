
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include "xmalloc.h"

/*
  typedef struct hm_stats {
  long pages_mapped;
  long pages_unmapped;
  long chunks_allocated;
  long chunks_freed;
  long free_length;
  } hm_stats;
*/

typedef struct node_t{
	int size;
	struct node_t* next;
} node;

typedef struct header_t{
	int size;
}header;

pthread_mutex_t lock;

//statics and global variables
const size_t PAGE_SIZE = 4096;
//static hm_stats stats; // This initializes the stats to 0.
node* head;

//protoypes
void hfree_r(node* in, node* place);
void* hmalloc_r(node* previous, node* start, size_t pages, size_t size);
void* hrealloc(void* ptr, size_t size);


long
free_list_length()
{
	node *cur = head;
	long size = 0;
	while(cur != 0){
		//printf("%d\n", cur->size);
		size++;
		cur = cur->next;
	}
	return size;
}
/*
hm_stats*
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

void
hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}
*/

static
size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}


void*
hmalloc(size_t size)
{
//	stats.chunks_allocated += 1;
	size += sizeof(size_t);
	size_t pages = div_up(size + sizeof(node), PAGE_SIZE);
	pthread_mutex_lock(&lock);
	return hmalloc_r(0, head, pages, size);
}


void*
hmalloc_r(node* previous, node* start, size_t pages, size_t size){
	if(head == NULL){
//		stats.pages_mapped += pages;
		header* out = mmap(0, PAGE_SIZE * pages, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
		out->size = size;
		head = (node*)((char*)out + size + sizeof(header));
		head->size = PAGE_SIZE * pages - size - sizeof(header);
		head->next = 0;
		pthread_mutex_unlock(&lock);
		return (void*)out + sizeof(header);
	}else if(start == NULL){
//		stats.pages_mapped += pages;
		header* out = mmap(0, PAGE_SIZE * pages, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
		start = (node*)((void*)out + size + sizeof(header));
		out->size = size;
		start->size = PAGE_SIZE * pages - size - sizeof(header);
		node* cur = head;
		if(start < head){
			start->next = head->next;
			head = start;
		}else{
			while(cur!= NULL){
				if (cur < start && start < cur->next){
					node* temp = cur->next;
					start->next = temp;
					cur->next = start;
				}else if ( cur < start && cur->next == 0){
					start->next = 0;
					cur->next = start;
				}else{
					cur = cur->next;
				}
			}
		}
		start->next = 0;
	//	printf("start %p head, %p prev, %p cur\n", head, previous, start);
		pthread_mutex_unlock(&lock);
		return (void*)out + sizeof(header);
	}else if(start->size > (size  + sizeof(header) + sizeof(node))){
	//	printf("move\n");
		//save rest of the size and the next node
		int sizeLeft = start->size - size - sizeof(header);
		node* next = start->next;
		//printf("%p\n", start->next);
		//making the header
		header *out = ((header*)start);
		out->size = size;
		//make the new head node
		node* newStart = (void*)start + size + sizeof(header);
		newStart->size = sizeLeft;
		newStart->next = next;
		if(previous == (node*)0){
			newStart->next = head->next;
			head = newStart;
		}else{
			previous->next = newStart;
		}
		//printf("%p start, %p start->next\n", newStart, newStart->next);
		pthread_mutex_unlock(&lock);
		return (void*)out + sizeof(header);
	}else{
		return hmalloc_r(start, start->next, pages, size);
	}
	//return (void*) -1;
}

void
hfree(void* item)
{

	//stats.chunks_freed += 1;
	if( ((header*) (item - sizeof(header)))->size  >= PAGE_SIZE){
//		stats.pages_unmapped += div_up((((header*)(item - sizeof(header)))->size + sizeof(header)),PAGE_SIZE);
		pthread_mutex_lock(&lock);
		if( ((header*)(item - sizeof(header)))->size + item  == head){
			//printf("%p head, %p head->next, %p item", head, head->next, item + ((header*)(item- sizeof(header)))->size);
			head = head->next;
		}
		munmap(item - sizeof(header), ((header*)( item - sizeof(header)))->size + sizeof(header));
		pthread_mutex_unlock(&lock);
	}else{
	
		if((void*)head > (void*)(item - sizeof(header))){
			//printf(" %p head, %p item \n", head, item);
			pthread_mutex_lock(&lock);
			node *new_chunk = (node*)((void*)item - sizeof(header));
			new_chunk->size = ((header*)(item - sizeof(header)))->size + sizeof(header) - sizeof(node);
			new_chunk->next = head;
			head = new_chunk;
			pthread_mutex_unlock(&lock);
			//printf(" %p head, %p next \n\n", head, head->next);
			return;
		}
	}
}


void*
hrealloc(void* ptr, size_t size){
   if(!ptr){
      return hmalloc(size);
   }

   header* cell = (void*)ptr - sizeof(header);
   if(cell->size >= size){
      return ptr;
   }

   void *ret;
   ret = hmalloc(size);

   memcpy(ret, ptr, cell->size);
   hfree(ptr);
   return ret;
}


