#include <stdint.h>
#include <stdio.h>

uint64_t MASK = 0x1;
int main(int argc, char const *argv[]) {
    char b = 7;
    char *dest = &b;

    dest = (char *)((unsigned long)dest | 0xc000000000000000);
    printf("Address of a: %p\n", dest);
    printf("Value of a: %d\n", *dest);
    printf("Storing tag...\n");

    asm volatile (
        "stc t0, 0(%0)\n"    // Store the byte in t0 to the address in dest
        :                   // No output operands
        : "r" (dest) // Input operands: src and dest
        : 
    );

    printf("Changing address...\n");


    dest = (char *)((unsigned long)dest | 0xff00000000000000);

    printf("Address of a: %p\n", dest);

    dest[0] = 60;
}
