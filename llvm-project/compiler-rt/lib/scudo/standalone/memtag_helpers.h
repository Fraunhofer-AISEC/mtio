
#ifndef SCUDO_MEMTAG_HELPERS_H_
#define SCUDO_MEMTAG_HELPERS_H_

#include "internal_defs.h"
#include "string_utils.h"
#include "memtag.h"

#if SCUDO_CAN_USE_MTE
#include <sys/auxv.h>
#include <sys/prctl.h>
#endif

// Declare the following functions as extern C to avoid name mangling
extern "C" {
    
void *irgSp(uint64_t);
void setTag(void *, uint64_t);
void setTagZero(void *, uint64_t);
void *stgp(void *, uint64_t, uint64_t);
void *tagP(void *, void *, uint64_t);
uint8_t readTag(void *);
void setShade(void *, uint8_t, uint64_t);

} // extern "C"

#endif