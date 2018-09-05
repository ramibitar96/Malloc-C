//
// CS252: MyMalloc Project
//
// The current implementation gets memory from the OS
// every time memory is requested and never frees memory.
//
// You will implement the allocator as indicated in the handout.
// 
// Also you will need to add the necessary locking mechanisms to
// support multi-threaded programs.
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "MyMalloc.h"

static pthread_mutex_t mutex;

const int ArenaSize = 2097152;
const int NumberOfFreeLists = 1;

// Header of an object. Used both when the object is allocated and freed
struct ObjectHeader {
    size_t _objectSize;         // Real size of the object.
    int _allocated;             // 1 = yes, 0 = no 2 = sentinel
    struct ObjectHeader * _next;       // Points to the next object in the freelist (if free).
    struct ObjectHeader * _prev;       // Points to the previous object.
};

struct ObjectFooter {
    size_t _objectSize;
    int _allocated;
};


  //STATE of the allocator

  // Size of the heap
  static size_t _heapSize;

  // initial memory pool
  static void * _memStart;

  // number of chunks request from OS
  static int _numChunks;

  // True if heap has been initialized
  static int _initialized;

  // Verbose mode
  static int _verbose;

  // # malloc calls
  static int _mallocCalls;

  // # free calls
  static int _freeCalls;

  // # realloc calls
  static int _reallocCalls;
  
  // # realloc calls
  static int _callocCalls;

  // Free list is a sentinel
  static struct ObjectHeader _freeListSentinel; // Sentinel is used to simplify list operations
  static struct ObjectHeader *_freeList;


  //FUNCTIONS

  //Initializes the heap
  void initialize();

  // Allocates an object 
  void * allocateObject( size_t size );

  // Frees an object
  void freeObject( void * ptr );

  // Returns the size of an object
  size_t objectSize( void * ptr );

  // At exit handler
  void atExitHandler();

  //Prints the heap size and other information about the allocator
  void print();
  void print_list();

  // Gets memory from the OS
  void * getMemoryFromOS( size_t size );

  void increaseMallocCalls() { _mallocCalls++; }

  void increaseReallocCalls() { _reallocCalls++; }

  void increaseCallocCalls() { _callocCalls++; }

  void increaseFreeCalls() { _freeCalls++; }

extern void
atExitHandlerInC()
{
  atExitHandler();
}

void initialize()
{
  // Environment var VERBOSE prints stats at end and turns on debugging
  // Default is on
  _verbose = 1;
  const char * envverbose = getenv( "MALLOCVERBOSE" );
  if ( envverbose && !strcmp( envverbose, "NO") ) {
    _verbose = 0;
  }

  pthread_mutex_init(&mutex, NULL);
  void * _mem = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)) );

  // In verbose mode register also printing statistics at exit
  atexit( atExitHandlerInC );

  //establish fence posts
  struct ObjectFooter * fencepost1 = (struct ObjectFooter *)_mem;
  fencepost1->_allocated = 1;
  fencepost1->_objectSize = 123456789;
  char * temp = 
      (char *)_mem + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp;
  fencepost2->_allocated = 1;
  fencepost2->_objectSize = 123456789;
  fencepost2->_next = NULL;
  fencepost2->_prev = NULL;

  //initialize the list to point to the _mem
  temp = (char *) _mem + sizeof(struct ObjectFooter);
  struct ObjectHeader * currentHeader = (struct ObjectHeader *) temp;
  temp = (char *)_mem + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp;
  _freeList = &_freeListSentinel;
  currentHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
  currentHeader->_allocated = 0;
  currentHeader->_next = _freeList;
  currentHeader->_prev = _freeList;
  currentFooter->_allocated = 0;
  currentFooter->_objectSize = currentHeader->_objectSize;
  _freeList->_prev = currentHeader;
  _freeList->_next = currentHeader; 
  _freeList->_allocated = 2; // sentinel. no coalescing.
  _freeList->_objectSize = 0;
  _memStart = (char*) currentHeader;
}

void * allocateObject( size_t size )
{
  int found = 0;
  char * temp;
  char * temp2;
  if (size <= 0)
    return NULL;

  //Make sure that allocator is initialized
  if ( !_initialized ) {
    _initialized = 1;
    initialize();
  }

  // Add the ObjectHeader/Footer to the size and round the total size up to a multiple of
  // 8 bytes for alignment.
  size_t roundedSize = (size + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter) + 7) & ~7;
  
  //run through free list to try and find appropriate chunk of memory set variable to avoid accessing OS.
  struct ObjectHeader * ptr = _freeList->_next;
  while(ptr != _freeList) {
        if (ptr->_objectSize >= roundedSize && ptr->_allocated == 0) {
        if ((ptr->_objectSize - roundedSize) <= 56) {
         ptr->_allocated == 1;
         char * temp4 = ((char *) ((char *)ptr + (ptr->_objectSize - sizeof(struct ObjectFooter))));
         struct ObjectFooter * footer = (struct ObjectFooter *) temp4;
         footer->_allocated = 1;
         struct ObjectHeader * tempHeader = ptr->_prev;
         ptr->_prev->_next = ptr->_next;
         ptr->_next->_prev = tempHeader;
         pthread_mutex_unlock(&mutex);
         return (void *) (ptr + 1);
        }   
      temp = ((char *) ptr + roundedSize - sizeof(struct ObjectFooter));
      temp2 = ((char *) ((char *)ptr + (ptr->_objectSize - sizeof(struct ObjectFooter))));
      size_t oldSize = ptr->_objectSize;
      struct ObjectFooter * newFooter = (struct ObjectFooter *) temp;
      struct ObjectFooter * oldFooter = (struct ObjectFooter *) temp2;
      struct ObjectHeader * oldHeader = ptr;
      oldFooter->_objectSize = oldFooter->_objectSize - roundedSize;
      newFooter->_allocated = 1;
      newFooter->_objectSize = roundedSize;
      oldHeader->_objectSize = roundedSize;
      oldHeader->_allocated = 1;
      temp = temp + sizeof(struct ObjectFooter);
      struct ObjectHeader * newHeader = (struct ObjectHeader *) temp;
      newHeader->_objectSize =  oldSize - roundedSize;
      newHeader->_allocated = 0;
      newHeader->_next = oldHeader->_next;
      newHeader->_prev = oldHeader->_prev;
      oldHeader->_prev->_next = newHeader;
      oldHeader->_next->_prev = newHeader;
      pthread_mutex_unlock(&mutex);
      return (void *) (oldHeader+1);
    }
    ptr = ptr->_next;
  }

  void * _mem = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)));
  struct ObjectFooter * fencepost1 = (struct ObjectFooter *)_mem;
  fencepost1->_allocated = 1;
  fencepost1->_objectSize = 123456789;
  char * temp3 = (char *)_mem + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp3;
  fencepost2->_allocated = 1;
  fencepost2->_objectSize = 123456789;
  fencepost2->_next = NULL;
  fencepost2->_prev = NULL;

  temp3 = (char *) _mem + sizeof(struct ObjectFooter);
  struct ObjectHeader * currentHeader = (struct ObjectHeader *) temp3;
  temp3 = (char *)_mem + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp3;
  currentHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
  currentHeader->_allocated = 0;
  currentHeader->_next = _freeList;
  currentHeader->_prev = _freeList->_prev;
  currentFooter->_allocated = 0;
  currentFooter->_objectSize = currentHeader->_objectSize;
  _freeList->_prev->_next = currentHeader;
  _freeList->_prev = currentHeader;


  return allocateObject(size);

}

void freeObject( void * ptr )
{
  // Add your code here
  char * temp = (char * ) ptr - sizeof(struct ObjectHeader);
  struct ObjectHeader * centerHead = (struct ObjectHeader *) temp;
  temp = temp - sizeof(struct ObjectFooter);
  struct ObjectFooter * leftFooter = (struct ObjectFooter *) temp;
  temp = temp + sizeof(struct ObjectFooter) + centerHead->_objectSize;
  struct ObjectHeader * rightHead = (struct ObjectHeader *) temp;
  
  if (leftFooter->_allocated == 0 && rightHead->_allocated == 0) {
    size_t newSize = leftFooter->_objectSize + centerHead->_objectSize + rightHead->_objectSize;
    char * temp7 = (char *)leftFooter - leftFooter->_objectSize + sizeof(struct ObjectFooter);
    struct ObjectHeader * leftHead = (struct ObjectHeader *) temp7;
    char * temp4 = (char *) rightHead + rightHead->_objectSize - sizeof(struct ObjectFooter);
    struct ObjectFooter * rightFooter = (struct ObjectFooter *) temp4;
    leftHead->_objectSize = newSize;
    rightFooter->_objectSize = newSize;
    rightHead->_prev->_next = rightHead->_next;
    rightHead->_next->_prev = rightHead->_prev;
  } else if (leftFooter->_allocated == 0 && rightHead->_allocated == 1) {
    //add size of the center and the left together
    size_t newSize = leftFooter->_objectSize + centerHead->_objectSize;
    char * temp7 = (char *)leftFooter - leftFooter->_objectSize + sizeof(struct ObjectFooter);
    struct ObjectHeader * leftHead = (struct ObjectHeader *) temp7;
    leftHead->_objectSize = newSize;
    char * temp8 = (char *)centerHead + centerHead->_objectSize - sizeof(struct ObjectFooter);
    struct ObjectFooter * centerFooter = (struct ObjectFooter *) temp8;
    centerFooter->_objectSize = newSize;
    centerFooter->_allocated = 0;
  } else if (leftFooter->_allocated == 1 && rightHead->_allocated == 0) {
    //add size of the center and the right together
    centerHead->_allocated = 0;
    size_t newSize = centerHead->_objectSize + rightHead->_objectSize;
    centerHead->_objectSize = newSize;
    char * temp4 = (char *) rightHead + rightHead->_objectSize - sizeof(struct ObjectFooter);
    struct ObjectFooter * rightFooter = (struct ObjectFooter *) temp4;
    rightFooter->_allocated = 0;
    rightFooter->_objectSize = newSize;
    struct ObjectHeader * tempHead1 = rightHead->_prev;
    struct ObjectHeader * tempHead2 = rightHead->_next;
    centerHead->_next = rightHead->_next;
    centerHead->_prev = rightHead->_prev;
    tempHead1->_next = centerHead;
    tempHead2->_prev = centerHead;
  } else {
    // simply free the block as it has no free neighbors in memory
    struct ObjectHeader * temp2 = _freeList->_prev->_prev;
    struct ObjectHeader * temp4 = _freeList->_prev;
    _freeList->_prev->_prev = centerHead;
    centerHead->_next = temp4;
    temp2->_next = centerHead; 
    centerHead->_prev = temp2;
    centerHead->_allocated = 0;
    char * temp3 = (char *) centerHead + centerHead->_objectSize - sizeof(struct ObjectFooter);
    struct ObjectFooter * centerFooter = (struct ObjectFooter *) temp3;
    centerFooter->_allocated = 0;
  }
  
  return;
}

size_t objectSize( void * ptr )
{
  // Return the size of the object pointed by ptr. We assume that ptr is a valid obejct.
  struct ObjectHeader * o =
    (struct ObjectHeader *) ( (char *) ptr - sizeof(struct ObjectHeader) );

  // Substract the size of the header
  return o->_objectSize;
}

void print()
{
  printf("\n-------------------\n");

  printf("HeapSize:\t%zd bytes\n", _heapSize );
  printf("# mallocs:\t%d\n", _mallocCalls );
  printf("# reallocs:\t%d\n", _reallocCalls );
  printf("# callocs:\t%d\n", _callocCalls );
  printf("# frees:\t%d\n", _freeCalls );

  printf("\n-------------------\n");
}

void print_list()
{
  printf("FreeList: ");
  if ( !_initialized ) {
    _initialized = 1;
    initialize();
  }
  struct ObjectHeader * ptr = _freeList->_next;
  while(ptr != _freeList){
      long offset = (long)ptr - (long)_memStart;
      printf("[offset:%ld,size:%zd]",offset,ptr->_objectSize);
      ptr = ptr->_next;
      if(ptr != NULL){
          printf("->");
      }
  }
  printf("\n");
}

void * getMemoryFromOS( size_t size )
{
  // Use sbrk() to get memory from OS
  _heapSize += size;
 
  void * _mem = sbrk( size );

  if(!_initialized){
      _memStart = _mem;
  }

  _numChunks++;

  return _mem;
}

void atExitHandler()
{
  // Print statistics when exit
  if ( _verbose ) {
    print();
  }
}

//
// C interface
//

extern void *
malloc(size_t size)
{
  pthread_mutex_lock(&mutex);
  increaseMallocCalls();
  
  return allocateObject( size );
}

extern void
free(void *ptr)
{
  pthread_mutex_lock(&mutex);
  increaseFreeCalls();
  
  if ( ptr == 0 ) {
    // No object to free
    pthread_mutex_unlock(&mutex);
    return;
  }
  
  freeObject( ptr );
}

extern void *
realloc(void *ptr, size_t size)
{
  pthread_mutex_lock(&mutex);
  increaseReallocCalls();
    
  // Allocate new object
  void * newptr = allocateObject( size );

  // Copy old object only if ptr != 0
  if ( ptr != 0 ) {
    
    // copy only the minimum number of bytes
    size_t sizeToCopy =  objectSize( ptr );
    if ( sizeToCopy > size ) {
      sizeToCopy = size;
    }
    
    memcpy( newptr, ptr, sizeToCopy );

    //Free old object
    freeObject( ptr );
  }

  return newptr;
}

extern void *
calloc(size_t nelem, size_t elsize)
{
  pthread_mutex_lock(&mutex);
  increaseCallocCalls();
    
  // calloc allocates and initializes
  size_t size = nelem * elsize;

  void * ptr = allocateObject( size );

  if ( ptr ) {
    // No error
    // Initialize chunk with 0s
    memset( ptr, 0, size );
  }

  return ptr;
}

