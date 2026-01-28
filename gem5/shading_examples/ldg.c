
#include <stdint.h>
#include <stdio.h>

uint64_t MASK = 0x1;
int main(int argc, char const *argv[])
{
    int a = 3;
    int *ptr = &a;

    printf("Address of a: %p\nValue of a: %d\n", ptr, *ptr);


    // ptr = (int *)((unsigned long)ptr | 0xc000000000000000);
    // At this point, usage of ptr would fail
    printf("Address of a: %p\nValue of a: %d\n", ptr, *ptr);

    printf("Storing tag for %p...\n", ptr);
    __asm__("stc %0, 0(%1)\n\t"
        : "=r" (ptr)
        : "r" (ptr)
    );



    // ptr = (int *)((unsigned long)ptr | 0xff00000000000000);

    printf("New address of a: %p\nLoading old tag...\n", ptr);
    // align ptr to 16
    ptr = (int *)((unsigned long)ptr & 0xFFFFFFFFFFFFFFF0);


    __asm__("ldg %0, 0(%0)\n\t"
        : "=r" (ptr)
        : "r" (ptr)
    );


    printf("Address of a: %p\n", ptr);
    // print all argcs
    for (int i = 0; i < argc; i++) {
        printf("arg[%d]: %s\n", i, argv[i]);
    }
}
