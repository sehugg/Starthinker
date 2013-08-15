
#ifndef _JOURNAL_H
#define _JOURNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#include "util.h"
#include "hash.h"

//#define JDEBUG DEBUG
#define JDEBUG(...)

typedef struct Journal
{
  void* dest;
  void* mem;
  unsigned int size;
  HashCode hash;
} Journal;

extern intptr_t _GLOBAL_BASE;
extern bool journal_state;
extern int jbuffer_top;
extern HashCode current_hash;

// set a state variable (relative to state container, which must have name 'state' in calling function)
#define SET(dest,src) { __typeof__ (dest) __tmp = (src); if (journal_state) ai_journal(state, &(dest), &__tmp, sizeof(__tmp)); else memcpy((void*)&(dest), &__tmp, sizeof(__tmp)); }
#define ADDTO(dest,src) SET(dest,(dest)+(src))
#define INC(dest) SET((dest),(dest)+1)
#define DEC(dest) SET((dest),(dest)-1)
// set a global variable (fixed memory address, not in state container)
#define SETGLOBAL(dest,src) { __typeof__ (dest) __tmp = (src); ai_journal(&_GLOBAL_BASE, &(dest), &__tmp, sizeof(__tmp)); }

void ai_journal_save(const void* dst, unsigned int size);

void ai_journal(const void* base, const void* dst, const void* src, unsigned int size);

void rollback_journal(int top);

void commit_journal();


#endif
