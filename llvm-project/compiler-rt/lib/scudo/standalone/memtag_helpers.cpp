#include "memtag_helpers.h"

// Declare the following functions as extern C to avoid name mangling
extern "C" {

#ifndef COLOR_SIZE
#error
#endif

  // Generate a randomly tagged stack base pointer.
  void *irgSp(uint64_t baseptr) {
    static uint8_t color = 0;
    color = (color + 1) % ((1 << COLOR_SIZE) - 1);
    uint64_t shifted_color = (uint64_t)color << (64U - COLOR_SIZE);
    // scudo::Printf("irg\t\t\tbaseptr: 0x%p\n", (void *)(shifted_color + baseptr));
    
    return (void *)(shifted_color + baseptr);
  }

  // Update allocation tags for the memory range to match the tag in the pointer
  // argument.
  void setTag(void *ptr, uint64_t range) {
    scudo::storeTags((uint64_t)ptr, (uint64_t)ptr + range);
  }

  void setShade(void *ptr, uint8_t shade, uint64_t size) {
    // Read Tag with ldg
    // scudo::Printf("setShade\t\tptr: %p shade: %x size: %lu\n", ptr, shade, size);

    uint64_t shade_mask = (uint64_t)(shade) << (64U - TAG_SIZE);
    ptr = (void *)((uint64_t)ptr | shade_mask);

    // scudo::Printf("setShade\t\tptr: %p\n", ptr);
    scudo::storeShades((uint64_t)ptr, (uint64_t)ptr + size);


  }

  uint8_t readTag(void *ptr) {
    // Read Tag with ldg
    // scudo::Printf("readTag\t\tptr: %p\n", ptr);
    uint8_t tag = scudo::extractTag((uint64_t)ptr);
    // scudo::Printf("readTag\t\ttag: %x\n", tag);
    return tag;
  }

  // Update allocation tags for the memory range to match the tag in the pointer
  // argument, and set memory contents to zero.
  void setTagZero(void *ptr, uint64_t val) {
    // TODO_kh also set to zero
    setTag(ptr, val);    
    memset(ptr, 0, val);
  }

  // Update allocation tags for 16-aligned, 16-sized memory region, and store a
  // pair 8-byte values.
  void *stgp(void *ptr, uint64_t val1, uint64_t val2) {
    scudo::Printf("stgp\t\tptr: %p\tval1: %lu\tval2: %lu\n", ptr, val1, val2);
    return (void *)1;
  }

  // Transfer pointer tag with offset.
  void *tagP(void *ptr0, void *baseptr, uint64_t tag_offset) {
    uint8_t tag = scudo::extractTag((uint64_t)baseptr);
    uint64_t new_tag = ((tag + tag_offset) % ((1 << COLOR_SIZE) - 1)) << (64U - COLOR_SIZE);
    uint64_t addr = scudo::untagPointer((uint64_t)ptr0);
    return (void *)(new_tag + addr);
  }
}