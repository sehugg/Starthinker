
#include "journal.h"

#include <stdlib.h>
#include <memory.h>

Journal* jbuffer = NULL;
int jbuffer_top = 0;
int jbuffer_size = 0;
int jbuffer_inc = 1024;

HashCode current_hash;

#define MCOPY(T,dst,src) *((T*)dst) = *((T*)src)
static void memcpyfast(void* dst, const void* src, int size)
{
  switch (size)
  {
    case 1: MCOPY(char,dst,src); break;
    case 2: MCOPY(short,dst,src); break;
    case 3: memcpy(dst,src,size); break;
    case 4: MCOPY(int,dst,src); break;
    default: memcpy(dst,src,size); break;
  }
}


void ai_journal_save(const void* dst, unsigned int size)
{
  // TODO: assert ai_state is correct
  assert(size>0);
  if (jbuffer_top == jbuffer_size)
  {
    jbuffer_size += jbuffer_inc;
    jbuffer = (Journal*) realloc(jbuffer, sizeof(Journal) * jbuffer_size);
  }
  Journal* j = &jbuffer[jbuffer_top++];
  JDEBUG("journal: log -> %p (%u bytes)\n", dst, size);
  j->dest = (void*)dst;
  j->size = size;
  if (size > sizeof(j->mem))
  {
    j->mem = malloc(size);
    memcpy(j->mem, dst, size);
  } else {
    memcpyfast(&j->mem, dst, size);
  }
  j->hash = current_hash;
}

void ai_journal(const void* base, const void* dst, const void* src, unsigned int size)
{
  ai_journal_save(dst, size);
  // TODO: copy and hash at same time
  int index0 = (intptr_t)dst - (intptr_t)base; // use buffer offset as part of CRC
  current_hash ^= compute_hash(src, size, index0) ^ compute_hash(dst, size, index0);
  JDEBUG("%d -> %x\n", index0, current_hash);
  memcpyfast((void*)dst, src, size);
}

static void rollback_entry(Journal* j)
{
  JDEBUG("journal: rollback %p (%u bytes)\n", j->dest, j->size);
  assert(j->size>0);
  if (j->size > sizeof(j->mem))
    memcpy(j->dest, j->mem, j->size);
  else
    memcpyfast(j->dest, &j->mem, j->size);
  current_hash = j->hash; //TODO: redundant if multiple rollbacks
}

static void dealloc_entry(Journal* j)
{
  assert(j->size>0);
  if (j->size > sizeof(j->mem))
    free(j->mem);
}

void rollback_journal(int top)
{
  while (jbuffer_top > top)
  {
    Journal* j = &jbuffer[--jbuffer_top];
    rollback_entry(j);
    dealloc_entry(j);
  }
}

void commit_journal()
{
  jbuffer_top = 0;
}

// just a marker for SETGLOBAL
// we use this address to apply an offset to addresses used in the hash function
// so that repeated runs are identical
intptr_t _GLOBAL_BASE;

