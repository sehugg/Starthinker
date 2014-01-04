#ifndef _UTIL_H
#define _UTIL_H

#include <stdint.h>

typedef uint32_t ChoiceIndex;
typedef uint64_t ChoiceMask;

// helper function for logging macro
int _ai_log(const char* fmt);

extern int verbose;
extern int debug_level;

#define CANDEBUG (verbose && debug_level <= verbose-1)
#define DEBUG(fmt, ...) \
            do { if (CANDEBUG && _ai_log(fmt)) { fprintf(stdout, fmt, __VA_ARGS__); } } while (0)
#define DEBUG2(fmt, ...) \
            do { if (CANDEBUG) { fprintf(stdout, fmt, __VA_ARGS__); } } while (0)

#define CHOICE(n) (((ChoiceMask)1)<<(n))
#define MASK(n) (CHOICE(n)-1)
#define RANGE(lo,hi) (MASK(hi) - MASK(lo))

// TODO: not a SET macro
#define SWAP(a,b) do { __typeof__(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)
#define TAKEMIN(a,b) do { if ((b) < (a)) { (a) = (b); }} while (0)
#define TAKEMAX(a,b) do { if ((b) > (a)) { (a) = (b); }} while (0)


#endif
