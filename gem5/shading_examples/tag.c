#include <stdio.h>

int main(int argc, char *argv[]) {
    unsigned long long int a = 0x200;

    printf("Current value:\t%#llx\n", a);

    for (size_t i = 0; i < 5; i++)
    {
        __asm__("irc %0, %1\n\t"
            : "=r" (a)
            : "r" (a)
        );

        printf("After tagging:\t%#llx\n", a);
    }
    return 0;
}
