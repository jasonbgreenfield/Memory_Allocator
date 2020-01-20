// =================================================================================================================================
/**
 * neo-alloc.c
 *
 * A recency-ordered, first-fit, non-splitting, non-coalescing allocator.
 * To compile:  gcc -std=gnu99 -ggdb -o neo-alloc neo-alloc.c
 *
 * Source code written by Scott F. Kaplan, Amherst College
 *
 * malloc(), free(), and realloc(), implemented by Jason Greenfield, Amherst College '20
 **/
// =================================================================================================================================



// =================================================================================================================================
// INCLUDES

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
// =================================================================================================================================



// =================================================================================================================================
// TYPES AND STRUCTURES

/** A header structure, to be placed at the beginning of each allocated block. */
typedef struct header {
  size_t         size;
  struct header* next;
} header_s;
// =================================================================================================================================



// =================================================================================================================================
// CONSTANTS AND MACRO FUNCTIONS.

/** The system's page size. */
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

/** The word size. */
#define WORD_SIZE sizeof(void*)

/** The double-word size. */
#define DOUBLE_WORD_SIZE (WORD_SIZE * 2)

/** Macros to easily calculate the number of bytes for larger scales (e.g., kilo, mega, gigabytes). */
#define KB(size)  ((size_t)size * 1024)
#define MB(size)  (KB(size) * 1024)
#define GB(size)  (MB(size) * 1024)

/** The virtual address space reserved for the heap. */
#define HEAP_SIZE GB(2)
// =================================================================================================================================



// =================================================================================================================================
// GLOBALS

/** The current beginning of free heap space. */
static void*     free_ptr  = NULL;

/** The beginning of the heap. */
static intptr_t  start_ptr;

/** The end of the heap. */
static intptr_t  end_ptr;

/** The head of a free list of blocks, initially empty. */
static header_s* free_list_head = NULL;
// =================================================================================================================================



// =================================================================================================================================
/**
 * Emit a simple debugging message.  Doesn't use `printf()` to avoid indirect calls to `malloc()`.
 *
 * \param msg The message to print as a null-terminated string.
 */
void debug (char* msg) {

  // Determine the length.
  unsigned int length = 0;
  while (msg[length] != '\0') {
    length += 1;
  }

  // Write it out, flushing the output.
  write(STDOUT_FILENO, msg, length);
  fsync(STDOUT_FILENO);

} // debug ()
// =================================================================================================================================



// =================================================================================================================================
/**
 * Emit an integer.
 *
 * \param value The value to be printed as a string.
 */
void debug_int (long value) {

  // Space to build the text representation of the string, safely on the stack.
  char buffer[16];

  // Build the string, digit by digit, right to left.
  int i     = 15;
  buffer[i] = '\0';
  while (value != 0) {

    i          = i - 1;
    long digit = value % 10;
    buffer[i]  = digit + '0';
    value      = value / 10;

  }

  // Print from the index of the first character in the value.
  debug(&buffer[i]);

} // debug_int ()
// =================================================================================================================================



// =================================================================================================================================
/**
 * The initialization method.  If this is the first use of the heap, initialize it.
 */

void init () {

  // Only do anything if the heap has not yet been used.
  if (free_ptr == NULL) {

    // Allocate virtual address space in which the heap will reside; free space will be allocated from where it starts.  Make it
    // un-shared and not backed by any file.  A failure to map this space is fatal.
    free_ptr = mmap(NULL, HEAP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (free_ptr == MAP_FAILED) {
      debug("mmap failed!\n");
      exit(1);
    }

    // Hold onto the boundaries of the heap as a whole.
    start_ptr = (intptr_t)free_ptr;
    end_ptr   = start_ptr + HEAP_SIZE;

    // DEBUG: Emit a message to indicate that this allocator is being called.
    debug("neo!\n");

  }

} // init ()
// =================================================================================================================================



// =================================================================================================================================
/**
 * Allocate and return `size` bytes of heap space.
 *
 * \param size The number of bytes to allocate.
 * \return A pointer to the allocated block, if successful; `NULL` if unsuccessful.
 */
void* malloc (size_t size) {

  // DEBUG: Show that malloc() is being called, and for how many bytes.
  debug("malloc(");
  debug_int(size);
  debug(") called\n");

  init();

  // Special case.
  if (size == 0) {
    return NULL;
  }

  // If there are any free blocks to consider, search the list until a sufficiently large one is found (and removed from the
  // free list, and returned), or the end of the list is reached.

  if(free_list_head != NULL){
    //loop through list to find correct size and return it
    header_s* to_return;
    header_s* current = free_list_head;
    header_s* previous = NULL;
    while(current != NULL){
      if(current->size >= size){
	previous->next = current->next; //does this actually point to the struct in the list tho?
	return current + sizeof(header_s); //might be sizeof(header_s*)
      }
      previous = current;
      current = current->next;
    }
  }

  // If we reach here, there was no free block that could satisfy the request, so we will allocate a new block at the end, adding a
  // header to it.  Pad out the total size of the block so that it is double-word aligned.
  size_t excess = 0;
  if (size % DOUBLE_WORD_SIZE != 0) {
    excess = DOUBLE_WORD_SIZE - size % DOUBLE_WORD_SIZE;
  }
  size_t block_size = size + excess;
  size_t total_size = block_size + sizeof(header_s);

  // If there is not sufficient space for this allocation, return failure.
  if ((intptr_t)free_ptr + total_size >= end_ptr) {
    return NULL;
  }

  // Move the free_ptr to create a new block of sufficient total size.  At its beginning, initialize a new header; then
  // return the block following the header.

  header_s* to_return = free_ptr;
  header_s new_header = { .size = size, .next = NULL};
  *to_return = new_header;
  to_return = to_return + sizeof(header_s);
  //free_ptr = (void*)((intptr_t)free_ptr + size);
  free_ptr = free_ptr + size + sizeof(header_s);
  return to_return; //do we need to return this + sizeof(header_s)?

} // malloc()
// =================================================================================================================================



// =================================================================================================================================
/**
 * Deallocate a given block on the heap.  This function currently does nothing, leaving freed blocks unused.
 *
 * \param ptr A pointer to the block to be deallocated.
 */
void free (void* ptr) {

  // Special case.
  if (ptr == NULL) {
    return;
  }

  // Find the header, and then insert this block at the head of the free list.
  // debug("before free");
  header_s* ptr_header = ptr - sizeof(header_s);
  header_s* temp = free_list_head;
  free_list_head = ptr_header;
  ptr_header -> next = temp;
  // debug("after free");


} // free()
// =================================================================================================================================



// =================================================================================================================================
/**
 * Allocate a block of `nmemb * size` bytes on the heap, zeroing its contents.
 *
 * \param nmemb The number of elements in the new block.
 * \param size The size, in bytes, of each of the `nmemb` elements.
 * \return A pointer to the newly allocated and zeroed block, if successful; `NULL` if unsuccessful.
 */
void* calloc (size_t nmemb, size_t size) {

  // Allocate a block of the requested size.
  size_t block_size    = nmemb * size;
  void*  new_block_ptr = malloc(block_size);

  // If the allocation succeeded, clear the entire block.
  if (new_block_ptr != NULL) {
    memset(new_block_ptr, 0, block_size);
  }

  return new_block_ptr;

} // calloc ()
// =================================================================================================================================



// =================================================================================================================================
/**
 * Update the given block at `ptr` to take on the given `size`.  Here, if `size` is a reduction for the block, then the block is
 * returned unchanged.  If the `size` is an increase for the block, then a new and larger block is allocated, and the data from the
 * old block is copied, the old block freed, and the new block returned.
 *
 * \param ptr The block to be assigned a new size.
 * \param size The new size that the block should assume.
 * \return A pointer to the resultant block, which may be `ptr` itself, or may be a newly allocated block.
 */
void* realloc (void* ptr, size_t size) {

  // Special case:  If there is no original block, then just allocate the new one of the given size.
  if (ptr == NULL) {
    return malloc(size);
  }

  // Special case: If the new size is 0, that's tantamount to freeing the block.
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  // Find the size in the header, and assign it to this new variable `block_size`.
  header_s* temp = ptr-sizeof(header_s);
  size_t block_size = temp->size; // Replace 0 with size taken from header.
  if (size <= block_size) {
    header_s* ptr_header = ptr-1;
    ptr_header->size = size; //is the header actually at (ptr-1)?
    return ptr;
  }

  // The new size is an increase.  Allocate the new, larger block, copy the contents of the old into it, and free the old.
  void* new_block_ptr = malloc(size);
  if (new_block_ptr != NULL) {
    memcpy(new_block_ptr, ptr, block_size);
    free(ptr);
  }

  return new_block_ptr;

} // realloc()
// =================================================================================================================================



#if !defined (PB_NO_MAIN)
// =================================================================================================================================
/**
 * The entry point if this code is compiled as a standalone program for testing purposes.
 */
void main () {

  // Allocate an array of 100 ints.
  int  size = 100;
  int* x    = malloc(size * sizeof(int));
  assert(x != NULL);
  printf("x = %p\n", x);

  // Assign some values and print the middle one.
  for (int i = 0; i < size; i += 1) {
    x[i] = i * 2;
  }
  printf("x[%d] = %d\n", size / 2, x[size / 2]);

  // Allocate another three.
  int* y = malloc(64);
  int* z = malloc(96);
  int* w = malloc(48);
  printf("y = %p, z = %p, w = %p\n", y, z, w);

  // Free a couple of them.
  free(x);
  free(z);

  // Allocate one more.
  int* a = malloc(72);
  printf("a = %p\n", a);

} // main()
// =================================================================================================================================
#endif
