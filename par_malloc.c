#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>

#include "xmalloc.h"

//block head struct
typedef struct block_head {
	pthread_mutex_t mutex;
	uint8_t map[128];
	int rank;
	struct block_head* next;
	struct block_head* prev;
	int max;
	int free;
} block_head;

//prototypes
void* opt_malloc(size_t bytes);
void opt_free(void* item);
void* opt_realloc(void* prev, size_t bytes);

//GLOBAL list of buckets
//each thread has its own listblock_head* buckets[11];
const int PAGE_SIZE = 4096;
__thread block_head* buckets[11];
void*
xmalloc(size_t bytes)
{
    return opt_malloc(bytes);
}

void
xfree(void* ptr)
{
    opt_free(ptr);
}

void*
xrealloc(void* prev, size_t bytes)
{
    return opt_realloc(prev, bytes);
}


int
bitmap_next(uint8_t* map, int start){
	int curr = start;
	while(curr < 1025){
		if((map[curr/8] & (1 << (curr%8))) == 0){
			return curr;
		}else{
			curr++;
		}
	}
	return -1;
}


void
bitmap_swap(uint8_t* map, int pos){
	int curr = ((map[pos/8] & (1 << (pos%8) )) != 0);
	curr ^= 1;
	map[pos/8] |= curr << (pos%8);
}

int
get_pow(size_t size){
	float temp = 0;
	//size += sizeof(block_head);
	temp = ceil(log(size)/log(2));
	if (temp < 2){
		return 0;
	}
	else{
		return ((int)temp) - 2;
	}
}


void*
big_malloc(int size){
	block_head* curr = mmap(0, (PAGE_SIZE * (pow(2,size +2) / PAGE_SIZE))*2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	curr->next = NULL;
	curr->prev = NULL;
	curr->rank = size + 2;
	curr->max = 1;
	curr->free = 0;
	pthread_mutex_init(&(curr->mutex), 0);
	return (void*)curr + sizeof(block_head);
}


void*
opt_malloc(size_t bytes){
	int size = get_pow(bytes);
	if(size >= 11){
		return big_malloc(size);
	}
	block_head* curr = buckets[size];
	if(curr == NULL){
		curr = mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		curr->next = NULL;
		curr->prev = NULL;
		curr->rank = size + 2;
		curr->max = (PAGE_SIZE  - sizeof(block_head))/pow(2,curr->rank) - 1;
		curr->free = (PAGE_SIZE  - sizeof(block_head))/pow(2,curr->rank) - 1;
		pthread_mutex_init(&(curr->mutex), 0);
		buckets[size] = curr;
	}else if (curr->free == 0){
		while(curr->free == 0){
			if(curr->next == NULL){
				//printf("curr null\n");
				block_head *next_page = mmap(0, PAGE_SIZE,PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
				next_page->next = NULL;
				next_page->prev = curr;
				next_page->rank = size + 2;
				next_page->max = (PAGE_SIZE - sizeof(block_head))/pow(2,next_page->rank) - 1;
				next_page->free = (PAGE_SIZE  - sizeof(block_head))/pow(2,next_page->rank) - 1;
				pthread_mutex_init(&(next_page->mutex), 0);
				curr->next = next_page;
				curr = next_page;
				//printf("%p newpage, %p bucket head\n", next_page, buckets[size]);
			}else{
				//printf("also here\n");
				curr = curr->next;
			}
		}
	}
	pthread_mutex_lock(&(curr->mutex));
	curr->free--;
	int pos = bitmap_next(curr->map, 0);
	bitmap_swap(curr->map, pos);
	pthread_mutex_unlock(&(curr->mutex));
//	printf("%d rank, %d max,%d pos, %d free\n", curr->rank, curr->max,pos, curr->free);
	return (void*)curr + sizeof(block_head) + (int)(pos * pow(2, curr->rank));
}



void
opt_free(void* item){
	block_head* curr = item - (int)((uintptr_t)item % 4096);
	pthread_mutex_lock(&(curr->mutex));
	if(curr->rank >= 11){
		munmap(curr, (PAGE_SIZE * PAGE_SIZE /pow(2,curr->rank))+ PAGE_SIZE);
		return;
	}
	int pos = ((int)((uintptr_t)item % 4096) - sizeof(block_head)) / (int)pow(2, curr->rank);//figure out this math -- not sure about correctnes
	bitmap_swap(curr->map, pos);
	pthread_mutex_unlock(&(curr->mutex));
}


void*
opt_realloc(void* prev, size_t bytes){
	if(!prev){
		return opt_malloc(bytes);
	}
	block_head* curr = prev - (int)((uintptr_t)prev % 4096);
	if( (int)pow(2,curr->rank) > bytes){
		return prev;
	}
	block_head* out;
	out = opt_malloc(bytes);
	memcpy(out,prev, (int)pow(2,curr->rank));
	opt_free(prev);
	return out;
}
